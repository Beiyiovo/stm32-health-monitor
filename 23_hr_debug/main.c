/**
 ******************************************************************************
 *  23_hr_debug —— ★★★ 心率算法内部诊断（串口 + OLED 双输出）
 ******************************************************************************
 *  ★ 2026-10-06 改进：同时驱动 OLED，中文显示，不用来回烧程序
 *
 *  OLED 显示（4 行中文）：
 *      心率诊断
 *      信号 正常/弱/无
 *      峰数 3
 *      心率 75 / 无数据
 *
 *  串口同时打印完整数据（每秒一行）：
 *      时间 N IR DC AC ACmax peaks int HR SpO2 OVF
 *
 *  接线：
 *      MAX30102：VIN→3.3V、SDA→B7、SCL→B6、GND→G
 *      OLED：    GND→G、VCC→3.3、SCL→B6、SDA→B7
 *      ★ 两个 4.7kΩ 上拉：B6↔3.3、B7↔3.3
 *      USB-TTL：TXD→A10、RXD→A9、GND→G
 ******************************************************************************
 */
#include "bsp.h"
#include "i2c.h"
#include "oled.h"
#include "max30102.h"

int main(void)
{
    uint32_t tick = 0;
    uint32_t sec  = 0;
    uint8_t  i;
    uint8_t  oled_ok = 0;
    char     buf[16];

    bsp_init();
    uart1_init();
    i2c_init();

    printf("\r\n\r\n");
    printf("================================================\r\n");
    printf("  501 毕设 —— ★ 心率算法诊断（OLED + 串口）\r\n");
    printf("================================================\r\n");
    printf("  MAX30102：VIN->3.3  SDA->B7  SCL->B6  GND->G\r\n");
    printf("  OLED：    GND->G  VCC->3.3  SCL->B6  SDA->B7\r\n");
    printf("  ★ 4.7kohm x2: B6<->3.3, B7<->3.3\r\n");
    printf("================================================\r\n\r\n");

    /* ---- OLED ---- */
    if (i2c_probe(0x3C) == I2C_OK) {
        oled_init();
        oled_clear();
        oled_draw_cn(32, 1, "心率诊断");
        oled_draw_cn(16, 4, "请放手指");
        oled_refresh();
        oled_ok = 1;
        printf("√ OLED 就绪\r\n");
    } else {
        printf("X OLED 找不到（不影响串口诊断）\r\n");
    }

    /* ---- MAX30102 ---- */
    printf("【初始化 MAX30102】... ");
    if (!max30102_init()) {
        printf("★ 失败！\r\n");
        if (oled_ok) {
            oled_clear();
            oled_draw_cn(16, 2, "传感器");
            oled_draw_cn(32, 3, "待测量");
            oled_refresh();
        }
        while (1) { led_onboard(1); delay_ms(100); led_onboard(0); delay_ms(900); }
    }
    printf("√ 成功\r\n\r\n");

    printf("★ 食指指腹【轻贴】在 MAX30102 玻璃窗上，保持 20 秒不动\r\n\r\n");
    printf("  时间  N      IR       DC       AC    ACmax  peaks int  HR SpO2 OVF\r\n");
    printf("  --------------------------------------------------------------------\r\n");

    while (1) {
        max30102_data_t d;
        max30102_dbg_t  g;

        /* ★ 尽量频繁调用，别让 FIFO 积压 */
        max30102_update();

        tick++;

        /* ---------- 每 500ms：刷 OLED ---------- */
        if (oled_ok && (tick % 50) == 0) {
            d = max30102_get();
            max30102_get_debug(&g);

            oled_clear();

            /* ---------------- 第 1 行：心率 ---------------- */
            oled_draw_cn(0, 0, "心率");
            if (d.hr_valid) {
                sprintf(buf, "%u", d.hr);
                oled_draw_str(48, 1, buf);
                oled_draw_str(72, 1, "bpm");
            } else {
                oled_draw_cn(48, 0, "无数据");
            }

            /* ---------------- 第 2 行：血氧 ---------------- */
            oled_draw_cn(0, 2, "血氧");
            if (d.spo2_valid) {
                sprintf(buf, "%u", d.spo2);
                oled_draw_str(48, 3, buf);
                oled_draw_str(68, 3, "%");
            } else {
                oled_draw_cn(48, 2, "无数据");
            }

            /* ---------------- 第 3 行：信号强度 ---------------- */
            oled_draw_cn(0, 4, "信号");
            if (g.ac_max_ever < 30)        oled_draw_cn(48, 4, "太弱");
            else if (g.ac_max_ever < 200)  oled_draw_cn(48, 4, "偏弱");
            else                           oled_draw_cn(48, 4, "正常");

            /* ---------------- 第 4 行：峰数 + 最大 AC 幅度 ---------------- */
            oled_draw_cn(0, 6, "峰数");
            sprintf(buf, "%u", g.peak_cnt);
            oled_draw_str(48, 7, buf);
            sprintf(buf, "%ld", (long)g.ac_max_ever);
            oled_draw_str(70, 7, buf);

            oled_refresh();
        }

        /* ---------- 每 1 秒：串口打印 ---------- */
        if ((tick % 100) == 0) {
            sec++;
            d = max30102_get();
            max30102_get_debug(&g);

            printf("  %3us  %-6lu %-8lu %-8ld %-6ld %-6ld %-4u %-4u %-2u %-4u %u\r\n",
                   (unsigned)sec,
                   (unsigned long)g.sample_cnt,
                   (unsigned long)d.ir,
                   (long)g.ir_dc,
                   (long)g.ir_ac,
                   (long)g.ac_max_ever,
                   g.peak_cnt,
                   (unsigned)(g.peak_cnt > 0 ? g.peak_interval[(g.peak_idx + 8 - 1) % 8] : 0),
                   d.hr_valid ? d.hr : 0,
                   d.spo2_valid ? d.spo2 : 0,
                   g.fifo_ovf);

            if ((sec % 5) == 0) {
                printf("       [判读] ");
                if (!d.finger) {
                    printf("★ 没检测到手指（直流 %ld < 50000）-> 手指完全盖住玻璃窗\r\n",
                           (long)g.ir_dc);
                } else if (g.ac_max_ever < 30) {
                    printf("★ 信号太弱（ACmax=%ld < 30）-> 贴紧点，别漏光\r\n",
                           (long)g.ac_max_ever);
                } else if (g.peak_cnt == 0) {
                    printf("★ 有信号(ACmax=%ld)但没峰 -> 峰值检测问题\r\n",
                           (long)g.ac_max_ever);
                } else if (!d.hr_valid) {
                    printf("★ 检测到 %u 个峰但心率没出来 -> 间隔不合理\r\n", g.peak_cnt);
                } else {
                    printf("√√√ 心率 = %u bpm，血氧 = %s\r\n",
                           d.hr, d.spo2_valid ? "有效" : "计算中");
                }
                if (g.fifo_ovf > 0) {
                    printf("       ★★ FIFO 溢出 %u 次！主循环太慢，丢样本了\r\n", g.fifo_ovf);
                }
            }
        }

        for (i = 0; i < 10; i++) delay_ms(1);
    }
}

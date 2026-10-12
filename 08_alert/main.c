/**
 ******************************************************************************
 *  08_alert —— 步骤 8：蜂鸣器 + 红色 LED + 按键 验证
 ******************************************************************************
 *  目的：
 *      1. 确认蜂鸣器会响（★ 低电平触发，已实测）
 *      2. 确认红色 LED 会亮
 *      3. 确认三个按键能读到
 *      4. 验证告警逻辑（手动模拟超阈值的数据）
 *
 *  接线：
 *      蜂鸣器：GND→G   I/O→STM32 印着【B0】的孔   VCC→【5V】
 *      红色LED：长脚→220Ω→STM32 印着【B1】的孔   短脚→GND
 *      按键：   一端→【A1】/【A4】/【A5】          另一端→GND
 ******************************************************************************
 */
#include "bsp.h"
#include "alert.h"

int main(void)
{
    uint8_t  k1, k2, k3;
    uint8_t  k1_last = 0, k2_last = 0, k3_last = 0;
    uint8_t  alert_type = ALERT_NONE;
    uint32_t ms = 0;
    uint8_t  hr = 75;
    int16_t  temp10 = 365;

    bsp_init();
    uart1_init();
    alert_init();

    printf("\r\n\r\n========================================\r\n");
    printf("  501 个人健康助手 —— 步骤 8：告警验证\r\n");
    printf("========================================\r\n\r\n");
    printf("接线：\r\n");
    printf("  蜂鸣器   GND→G   I/O→【B0】   VCC→【5V】\r\n");
    printf("  红色LED  长脚→220Ω→【B1】     短脚→GND\r\n");
    printf("  按键     【A1】/【A4】/【A5】 → 另一端接 GND\r\n\r\n");

    /* ---- 上电自检：证明蜂鸣器和 LED 都能工作 ---- */
    printf("【自检】蜂鸣器响一声 + 红 LED 亮一下...\r\n");
    alert_test();
    printf("  如果你的蜂鸣器响了、LED 亮了 = 硬件都正常\r\n");
    printf("  没响 → 检查：蜂鸣器 I/O 接【B0】了吗？VCC 接【5V】了吗？\r\n\r\n");

    printf("【按键】按一下试试（每 100ms 检测一次）\r\n");
    printf("  KEY1(A1) = 切到「正常」\r\n");
    printf("  KEY2(A4) = 切到「心率过高 120」→ 应该报警\r\n");
    printf("  KEY3(A5) = 切到「体温过高 38.5」→ 应该报警\r\n\r\n");

    while (1) {
        /* ---- 读按键（做边沿检测，按下才算一次） ---- */
        k1 = key1_pressed();
        k2 = key2_pressed();
        k3 = key3_pressed();

        if (k1 && !k1_last) {
            hr = 75; temp10 = 365;
            printf("  [KEY1] 切到正常（心率 75，体温 36.5）\r\n");
        }
        if (k2 && !k2_last) {
            hr = 120; temp10 = 365;
            printf("  [KEY2] ★ 切到心率过高（120 bpm）→ 应该报警\r\n");
        }
        if (k3 && !k3_last) {
            hr = 75; temp10 = 385;
            printf("  [KEY3] ★ 切到体温过高（38.5 C）→ 应该报警\r\n");
        }
        k1_last = k1; k2_last = k2; k3_last = k3;

        /* ---- 告警判断 ---- */
        alert_type = alert_update(hr, 1, temp10, 1);

        /* ---- 每 1 秒打印状态 ---- */
        ms += 100;
        if ((ms % 1000) == 0) {
            printf("[%5u] 心率 %3u bpm  体温 %d.%d C  状态: %s%s\r\n",
                   (unsigned)(ms / 1000), hr,
                   (int)(temp10 / 10), (int)(temp10 % 10),
                   alert_text(alert_type),
                   alert_is_active() ? "  ★ 蜂鸣器响 + 红灯亮" : "");
        }

        /* ---- 板载 LED 心跳（证明程序在跑） ---- */
        led_onboard(((ms / 100) % 10) < 1 ? 1 : 0);

        delay_ms(100);
    }
}

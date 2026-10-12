/**
 ******************************************************************************
 *  14_max_test —— MAX30102 血氧模块诊断（★ A/B 测试用）
 ******************************************************************************
 *  目的：
 *      验证 I2C 总线和 MAX30102 模块。
 *
 *      ★ 如果这个能读到 PART_ID = 0x15：
 *           → I2C 总线是好的 → 那个 OLED 模块确实是坏的
 *      ★ 如果这个也读不到：
 *           → 问题在 STM32 / 上拉电阻 / 面包板这一侧（不是模块的事）
 *
 *  ★ 接线（MAX30102）：
 *      下排 4 个脚（★ 顺序是 VIN · SDA · SCL · GND，SDA 在 SCL 前面）：
 *          VIN  →  STM32 上排印着【3.3】的针
 *          SDA  →  STM32 下排印着【B7】的针
 *          SCL  →  STM32 下排印着【B6】的针
 *          GND  →  STM32 下排印着【G】的针
 *      上排 4 个脚（GND / RD / IRD / INT）→ ★ 全部不接
 *
 *      ★ 两个 4.7kΩ 上拉电阻（必须）：
 *          STM32 的【B6】←→【3.3】
 *          STM32 的【B7】←→【3.3】
 *
 *      USB-TTL（烧录+看打印）：TXD→A10、RXD→A9、GND→G
 ******************************************************************************
 */
#include "bsp.h"
#include "i2c.h"

#define MAX_ADDR    0x57

/* MAX30102 寄存器 */
#define REG_INT_ST1     0x00
#define REG_INT_ST2     0x01
#define REG_INT_EN1     0x02
#define REG_INT_EN2     0x03
#define REG_FIFO_WR     0x04
#define REG_OVF_CNT     0x05
#define REG_FIFO_RD     0x06
#define REG_FIFO_DATA   0x07
#define REG_FIFO_CFG    0x08
#define REG_MODE_CFG    0x09
#define REG_SPO2_CFG    0x0A
#define REG_LED1_PA     0x0C
#define REG_LED2_PA     0x0D
#define REG_PART_ID     0xFF
#define REG_REV_ID      0xFE

static void print_banner(void)
{
    printf("\r\n\r\n");
    printf("============================================\r\n");
    printf("  501 毕设 —— MAX30102 血氧模块诊断\r\n");
    printf("============================================\r\n");
    printf("  接线：\r\n");
    printf("    MAX30102 VIN -> STM32 上排【3.3】\r\n");
    printf("    MAX30102 SDA -> STM32 下排【B7】\r\n");
    printf("    MAX30102 SCL -> STM32 下排【B6】\r\n");
    printf("    MAX30102 GND -> STM32 下排【G】\r\n");
    printf("    ★ 上排 4 脚（GND/RD/IRD/INT）全部不接\r\n");
    printf("    ★ 两个 4.7kΩ：B6<->3.3、B7<->3.3\r\n");
    printf("============================================\r\n");
}

int main(void)
{
    uint8_t  addr, n;
    uint8_t  v;
    uint32_t round = 0;
    uint8_t  i;

    bsp_init();
    uart1_init();
    i2c_init();

    print_banner();

    while (1) {
        round++;
        printf("\r\n\r\n██████ 第 %u 轮 ██████\r\n", (unsigned)round);

        /* ============ 1. 先扫全部地址 ============ */
        printf("\r\n【1】I2C 扫描（0x01~0x7F）\r\n");
        printf("----------------------------------------\r\n");
        n = 0;
        for (addr = 0x01; addr < 0x80; addr++) {
            if (i2c_probe(addr) == I2C_OK) {
                printf("    发现设备: 0x%02X", addr);
                if (addr == 0x3C || addr == 0x3D) printf("  <- SSD1306 OLED");
                if (addr == 0x57) printf("  <- ★ MAX30102");
                if (addr == 0x68 || addr == 0x69) printf("  <- MPU6050");
                printf("\r\n");
                n++;
            }
        }
        printf("    共发现 %u 个设备\r\n", n);

        /* ============ 2. 直接测 MAX30102 ============ */
        printf("\r\n【2】直接测 MAX30102（0x57）\r\n");
        printf("----------------------------------------\r\n");

        if (i2c_probe(MAX_ADDR) == I2C_OK) {
            printf("    √ 0x57 有应答！模块在线\r\n\r\n");

            /* 读 PART_ID */
            if (i2c_read_reg(MAX_ADDR, REG_PART_ID, &v) == I2C_OK) {
                printf("    PART_ID (0xFF) = 0x%02X", v);
                if (v == 0x15) printf("  ★★★ 正确！MAX30102 是好的！\r\n");
                else           printf("  ⚠ 期望 0x15，但是兼容模块也可能不同\r\n");
            } else {
                printf("    PART_ID 读取失败\r\n");
            }

            /* 读 REV_ID */
            if (i2c_read_reg(MAX_ADDR, REG_REV_ID, &v) == I2C_OK) {
                printf("    REV_ID  (0xFE) = 0x%02X\r\n", v);
            }

            /* 读几个关键寄存器 */
            printf("\r\n    读几个寄存器看 I2C 读功能是否正常：\r\n");
            {
                static const uint8_t regs[] = {0x00, 0x01, 0x04, 0x05, 0x06, 0x08, 0x09, 0x0A};
                for (i = 0; i < sizeof(regs); i++) {
                    if (i2c_read_reg(MAX_ADDR, regs[i], &v) == I2C_OK) {
                        printf("      0x%02X = 0x%02X\r\n", regs[i], v);
                    } else {
                        printf("      0x%02X = 读失败\r\n", regs[i]);
                    }
                }
            }

            /* ============ 3. 初始化并读数据 ============ */
            printf("\r\n【3】初始化 MAX30102 并读原始数据\r\n");
            printf("----------------------------------------\r\n");

            i2c_write_reg(MAX_ADDR, REG_MODE_CFG, 0x40);   /* 复位 */
            delay_ms(100);
            i2c_write_reg(MAX_ADDR, REG_INT_EN1, 0x00);
            i2c_write_reg(MAX_ADDR, REG_INT_EN2, 0x00);
            i2c_write_reg(MAX_ADDR, REG_FIFO_WR, 0x00);
            i2c_write_reg(MAX_ADDR, REG_OVF_CNT, 0x00);
            i2c_write_reg(MAX_ADDR, REG_FIFO_RD, 0x00);
            i2c_write_reg(MAX_ADDR, REG_FIFO_CFG, 0x4F);
            i2c_write_reg(MAX_ADDR, REG_MODE_CFG, 0x03);   /* SpO2 模式 */
            i2c_write_reg(MAX_ADDR, REG_SPO2_CFG, 0x27);
            i2c_write_reg(MAX_ADDR, REG_LED1_PA, 0x24);
            i2c_write_reg(MAX_ADDR, REG_LED2_PA, 0x24);
            delay_ms(100);

            printf("    初始化完成。下面每 500ms 读一次 FIFO：\r\n");
            printf("    ★ 把手指【轻轻】按在传感器玻璃面上\r\n");
            printf("    ★ 手指按上去时 IR 值会【明显变大】（几万以上）\r\n\r\n");
            printf("      次数 |   IR 原始值  |  RED 原始值  |  说明\r\n");
            printf("      -----+--------------+--------------+------------------\r\n");
            {
                uint8_t  buf[6];
                uint32_t k;
                for (k = 1; k <= 40; k++) {
                    uint8_t wr, rd, cnt;

                    i2c_read_reg(MAX_ADDR, REG_FIFO_WR, &wr);
                    i2c_read_reg(MAX_ADDR, REG_FIFO_RD, &rd);
                    cnt = (uint8_t)((wr - rd) & 0x1F);

                    if (cnt > 0 && cnt <= 16) {
                        uint8_t j;
                        for (j = 0; j < cnt; j++) {
                            if (i2c_read_regs(MAX_ADDR, REG_FIFO_DATA, buf, 6) != I2C_OK) break;
                            {
                                uint32_t red = (((uint32_t)buf[0] << 16) | ((uint32_t)buf[1] << 8) | buf[2]) & 0x3FFFF;
                                uint32_t ir  = (((uint32_t)buf[3] << 16) | ((uint32_t)buf[4] << 8) | buf[5]) & 0x3FFFF;
                                if (j == cnt - 1) {
                                    printf("      %4u |  %10u  |  %10u  |  %s\r\n",
                                           (unsigned)k, (unsigned)ir, (unsigned)red,
                                           ir > 50000 ? "★ 有手指（信号强）" :
                                           ir > 5000  ? "有东西挡着" : "空（没手指）");
                                }
                            }
                        }
                    } else {
                        printf("      %4u |  （FIFO 空，cnt=%u）\r\n", (unsigned)k, cnt);
                    }
                    delay_ms(500);
                }
            }

            printf("\r\n  ★★★ 结论：MAX30102 是好的，I2C 总线也是好的！\r\n");
            printf("      → 那个 OLED 模块确实是坏的，换一个\r\n");
            printf("      → 或者 OLED 的接线和 MAX30102 不一样，再检查一下\r\n");

        } else {
            printf("    ✗ 0x57 无应答 —— MAX30102 也没扫到\r\n\r\n");
            printf("    ★★ 这说明问题【不在模块】，而在：\r\n");
            printf("       ① 两个 4.7kΩ 上拉电阻接了吗？\r\n");
            printf("           STM32 的 B6 <-> 3.3\r\n");
            printf("           STM32 的 B7 <-> 3.3\r\n");
            printf("       ② MAX30102 的脚序接对了吗？\r\n");
            printf("           ★ 下排顺序是 VIN · SDA · SCL · GND（SDA 在前面！）\r\n");
            printf("           VIN->3.3  SDA->B7  SCL->B6  GND->G\r\n");
            printf("       ③ 上排 4 个脚（GND/RD/IRD/INT）有没有误接？\r\n");
            printf("           ★ 那 4 个脚要【全部空着】\r\n");
            printf("       ④ 万用表通断档量：\r\n");
            printf("           STM32 的 B6 针 <-> MAX30102 的 SCL 脚   应该响\r\n");
            printf("           STM32 的 B7 针 <-> MAX30102 的 SDA 脚   应该响\r\n");
            printf("           MAX30102 的 GND <-> STM32 的 G          应该响\r\n");
            printf("           MAX30102 的 VIN <-> STM32 的 3.3        应该响\r\n");
            printf("       ⑤ 面包板列插对了吗（同一列才连通）\r\n");
        }

        printf("\r\n  15 秒后重新测试...\r\n");

        for (i = 0; i < 150; i++) {
            led_onboard((i % 25) < 2 ? 1 : 0);
            delay_ms(100);
        }
    }
}

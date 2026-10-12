/**
 ******************************************************************************
 *  03_i2c_scan —— 步骤 3：I2C 总线扫描（★ 用统一的软件 I2C 驱动）
 ******************************************************************************
 *  ★★ 2026-10-06 重写：
 *      原版自己写了一套硬件 I2C 代码，地址换算有 bug
 *      （ST 的 I2C_Send7bitAddress 期望 8 位地址，我传了 7 位）
 *      → 扫到的地址显示成 0x78/0x79（8 位），而不是 0x3C（7 位）
 *
 *      现在改用 _common/i2c.c 的【软件 I2C】驱动：
 *        · 接口简单：i2c_probe(7位地址)
 *        · 速率可调（I2C_DELAY_US，默认 20us ≈ 25kHz，本套硬件实测可用）
 *        · 显示正确的 7 位地址
 *
 *  期望结果：
 *      0x3C  = SSD1306 OLED
 *      0x57  = MAX30102（心率血氧）
 *      0x68  = MPU6050（六轴加速度）
 *
 *  接线：模块 SCL→STM32 的 B6，SDA→B7，GND→G，VCC→3.3
 *        ★ 两个 4.7kΩ 上拉：B6←→3.3、B7←→3.3
 *        USB-TTL：TXD→A10、RXD→A9、GND→G
 ******************************************************************************
 */
#include "bsp.h"
#include "i2c.h"

static const char *known(uint8_t a)
{
    switch (a) {
        case 0x3C: return "  <- ★ SSD1306 OLED";
        case 0x3D: return "  <- SSD1306 OLED（地址跳线版）";
        case 0x57: return "  <- ★ MAX30102 心率血氧";
        case 0x68: return "  <- ★ MPU6050 六轴";
        case 0x69: return "  <- MPU6050（AD0 接 VCC）";
        case 0x76: return "  <- BMP280/BME280";
        case 0x1E: return "  <- HMC5883 磁力计";
        default:   return "";
    }
}

int main(void)
{
    uint8_t  addr;
    uint8_t  n;
    uint32_t round = 0;
    uint8_t  i;
    int      has_oled, has_max, has_mpu;

    bsp_init();
    uart1_init();
    i2c_init();

    printf("\r\n\r\n");
    printf("========================================\r\n");
    printf("  501 个人健康助手 —— 步骤 3\r\n");
    printf("  I2C 总线扫描（软件 I2C）\r\n");
    printf("========================================\r\n");
    printf("  SCL = STM32 下排【B6】\r\n");
    printf("  SDA = STM32 下排【B7】\r\n");
    printf("  电源 = 3V3 / GND\r\n");
    printf("  ★ 两个 4.7kΩ 上拉：B6<->3.3、B7<->3.3\r\n");
    printf("========================================\r\n\r\n");

    while (1) {
        round++;
        printf("-------- 第 %u 次扫描 --------\r\n", (unsigned)round);

        n = 0;
        for (addr = 0x01; addr < 0x80; addr++) {
            if (i2c_probe(addr) == I2C_OK) {
                printf("  发现设备: 0x%02X%s\r\n", addr, known(addr));
                n++;
            }
        }

        printf("\r\n  共发现 %u 个设备\r\n\r\n", n);

        /* ---- 判定三个关键传感器 ---- */
        has_oled = (i2c_probe(0x3C) == I2C_OK || i2c_probe(0x3D) == I2C_OK);
        has_max  = (i2c_probe(0x57) == I2C_OK);
        has_mpu  = (i2c_probe(0x68) == I2C_OK || i2c_probe(0x69) == I2C_OK);

        printf("  [%s] OLED      (0x3C)\r\n", has_oled ? "√" : "X");
        printf("  [%s] MAX30102  (0x57)\r\n", has_max  ? "√" : "X");
        printf("  [%s] MPU6050   (0x68)\r\n", has_mpu  ? "√" : "X");
        printf("\r\n");

        if (has_oled && has_max && has_mpu) {
            printf("  ★★★ 三个都在！接线全对，可以往下做。\r\n");
        } else if (has_oled || has_max || has_mpu) {
            printf("  ★ 至少有一个在线 —— I2C 总线是通的！\r\n");
            printf("     没扫到的那个，检查它的 4 根线（SCL/SDA/GND/VCC）\r\n");
        } else {
            printf("  ! 一个都没扫到。排查：\r\n");
            printf("     1. 两个 4.7kΩ 上拉接了吗（B6<->3.3、B7<->3.3）\r\n");
            printf("     2. 模块的 SCL/SDA/GND/VCC 四根线都通吗\r\n");
            printf("     3. ★ MAX30102 脚序是 VIN·SDA·SCL·GND（SDA 在 SCL 前面）\r\n");
            printf("     4. 面包板同一列才连通\r\n");
        }

        printf("\r\n========================================\r\n\r\n");

        /* LED 心跳 + 每 5 秒重扫 */
        led_onboard(1); delay_ms(100); led_onboard(0);

        for (i = 0; i < 49; i++) delay_ms(100);
    }
}

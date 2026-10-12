/**
 ******************************************************************************
 *  07_max30102 —— 步骤 7：MAX30102 心率 + 血氧验证
 ******************************************************************************
 *  目的：手指按在传感器上，读心率（bpm）和血氧（%）
 *
 *  接线：MAX30102 下排 4 个脚（★ 顺序是 VIN·SDA·SCL·GND）
 *          VIN → 3.3V
 *          SDA → STM32 印着【B7】的孔
 *          SCL → STM32 印着【B6】的孔
 *          GND → GND
 *        ★ 上排 4 个脚（GND/RD/IRD/INT）全部不用
 *
 *  使用：手指【轻轻】按在传感器玻璃面上（别用力压，压了血流不畅读不到）
 ******************************************************************************
 */
#include "bsp.h"
#include "i2c.h"
#include "max30102.h"

int main(void)
{
    max30102_data_t d;
    uint32_t n = 0, i;
    uint32_t last_print = 0;
    uint8_t  last_finger = 0;

    bsp_init();
    uart1_init();
    i2c_init();

    printf("\r\n\r\n========================================\r\n");
    printf("  501 个人健康助手 —— 步骤 7：MAX30102\r\n");
    printf("========================================\r\n\r\n");
    printf("接线：VIN→3.3  SDA→B7  SCL→B6  GND→G\r\n");
    printf("      （★ 顺序是 VIN·SDA·SCL·GND，SDA 在前）\r\n\r\n");

    if (!max30102_init()) {
        printf("X 找不到 MAX30102（0x57）！\r\n");
        printf("  检查：\r\n");
        printf("    1. ★ 脚序：VIN·SDA·SCL·GND（SDA 在 SCL 前面！）\r\n");
        printf("    2. SDA→B7、SCL→B6（别接反）\r\n");
        printf("    3. VIN 接 3.3V（不是 5V）\r\n");
        printf("    4. 用 03_i2c_scan 扫一下，看 0x57 在不在\r\n");
        printf("\r\n  程序停在这，接好线按复位重启。\r\n");
        while (1) {
            led_onboard(1); delay_ms(150);
            led_onboard(0); delay_ms(150);
        }
    }
    printf("√ MAX30102 初始化成功\r\n\r\n");
    printf("★ 把手指【轻轻】按在传感器玻璃面上（别用力压）\r\n");
    printf("  等 5~10 秒就能读到数据\r\n\r\n");
    printf("  序号 | 手指 |  IR直流  |  IR交流 | 心率 | 血氧\r\n");
    printf("  -----+------+----------+---------+------+-----\r\n");

    while (1) {
        /* ★ 每 10ms 调一次，匹配 100Hz 采样率 */
        max30102_update();
        n++;

        if ((n - last_print) >= 50) {       /* 每 500ms 打印一次 */
            last_print = n;
            d = max30102_get();

            if (d.finger != last_finger) {
                last_finger = d.finger;
                if (d.finger) {
                    printf("\r\n  ★ 检测到手指，开始测量...\r\n\r\n");
                    max30102_reset_algo();
                } else {
                    printf("\r\n  ⚠ 手指拿开了\r\n\r\n");
                }
            }

            printf("  %4u |  %s   | %8u | %7d | %4s | %4s\r\n",
                   (unsigned)n,
                   d.finger ? "有" : "无",
                   (unsigned)d.ir_dc,
                   (int)d.ir_ac,
                   d.hr_valid ? "有" : "--",
                   d.spo2_valid ? "有" : "--");

            if (d.hr_valid && d.spo2_valid) {
                printf("         >>> 心率 %u bpm，血氧 %u %%\r\n", d.hr, d.spo2);
            }
        }

        /* 10ms 周期 */
        for (i = 0; i < 10; i++) delay_ms(1);

        /* 板载 LED 指示：有手指就慢闪 */
        led_onboard(d.finger ? ((n % 100) < 10) : 0);
    }
}

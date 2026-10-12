/**
 ******************************************************************************
 *  17_led_test —— ★ 最简单的 LED 测试（验证 烧录 → 运行 全链路）
 ******************************************************************************
 *  这个程序【只用板载 LED】，不依赖任何外部模块。
 *
 *  ★ 上电后你会看到：
 *      ① 板载 LED（PC13）常亮 2 秒
 *      ② 灭 1 秒
 *      ③ 快闪 3 次（每次 150ms）
 *      ④ 停 1 秒
 *      ⑤ 回到 ①，循环
 *
 *  ★ 同时串口会打印每次循环的次数
 *
 *  接线（★ 只需要 USB-TTL，不用接任何模块）：
 *      USB-TTL TXD → STM32 下排印着【A10】的针
 *      USB-TTL RXD → STM32 下排印着【A9】的针
 *      USB-TTL GND → STM32 下排印着【G】的针
 *      USB-TTL VCC → 不接
 *      STM32 插 Micro USB 供电
 *
 *  ★ 板载 LED 在 STM32 板上，标着 PC13 或 LED，靠近复位键
 ******************************************************************************
 */
#include "bsp.h"

int main(void)
{
    uint32_t n = 1;
    uint8_t  i;

    bsp_init();
    uart1_init();

    printf("\r\n\r\n");
    printf("========================================\r\n");
    printf("  501 毕设 —— LED 测试\r\n");
    printf("========================================\r\n");
    printf("  ★ 看 STM32 板上的板载 LED（PC13）\r\n");
    printf("     应该：常亮2秒 → 灭1秒 → 快闪3次 → 循环\r\n");
    printf("  ★ 如果 LED 在动，说明烧录+运行都正常\r\n");
    printf("========================================\r\n\r\n");

    while (1) {
        printf("[第 %u 轮] 常亮 2 秒\r\n", (unsigned)n);
        led_onboard(1);
        for (i = 0; i < 20; i++) delay_ms(100);

        printf("           灭 1 秒\r\n");
        led_onboard(0);
        for (i = 0; i < 10; i++) delay_ms(100);

        printf("           快闪 3 次\r\n");
        {
            uint8_t k;
            for (k = 0; k < 3; k++) {
                led_onboard(1); delay_ms(150);
                led_onboard(0); delay_ms(150);
            }
        }

        printf("           停 1 秒\r\n\r\n");
        for (i = 0; i < 10; i++) delay_ms(100);

        n++;
        if (n > 9999) n = 1;
    }
}

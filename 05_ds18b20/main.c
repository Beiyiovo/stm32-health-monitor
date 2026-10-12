/**
 ******************************************************************************
 *  05_ds18b20 —— 步骤 5：DS18B20 温度采集验证
 ******************************************************************************
 *  目的：确认能读到正确的室温
 *
 *  接线：DS18B20 三个脚（面对平面，从左到右）：
 *          1 = GND  → 面包板 GND
 *          2 = DQ   → STM32 印着【A0】的孔
 *          3 = VCC  → 3.3V
 *        ★ DQ 和 3.3V 之间接一个 4.7kΩ 上拉电阻（必须！）
 ******************************************************************************
 */
#include "bsp.h"
#include "ds18b20.h"

int main(void)
{
    int16_t  t;
    uint32_t n = 0;

    bsp_init();
    uart1_init();
    ds18b20_init();

    printf("\r\n\r\n========================================\r\n");
    printf("  501 个人健康助手 —— 步骤 5：DS18B20 温度\r\n");
    printf("========================================\r\n\r\n");
    printf("接线：GND→G   DQ→A0   VCC→3.3   ★ DQ 上拉 4.7kΩ 到 3.3V\r\n\r\n");

    /* 先检测器件在不在 */
    if (!ds18b20_start()) {
        printf("X 找不到 DS18B20！\r\n");
        printf("  检查这 4 点：\r\n");
        printf("    1. DQ 接在 STM32 印着【A0】的孔上\r\n");
        printf("    2. ★ 4.7kΩ 上拉电阻接了吗（DQ 到 3.3V）\r\n");
        printf("    3. DS18B20 脚序：1=GND  2=DQ  3=VCC\r\n");
        printf("    4. 是不是接反了（1 脚和 3 脚对调）\r\n");
        printf("\r\n  程序停在这，接好线按复位重启。\r\n");
        while (1) {
            led_onboard(1); delay_ms(150);
            led_onboard(0); delay_ms(150);
        }
    }
    printf("√ 找到 DS18B20\r\n\r\n");

    while (1) {
        n++;
        t = ds18b20_get();

        if (t == DS18B20_ERR) {
            printf("[%u] 读取失败\r\n", (unsigned)n);
        } else {
            printf("[%u] 温度 = %d.%d C\r\n",
                   (unsigned)n, (int)(t / 10), (int)(t % 10 < 0 ? -t % 10 : t % 10));
        }

        led_onboard(1); delay_ms(100);
        led_onboard(0); delay_ms(1900);
    }
}

/**
 ******************************************************************************
 *  24_key_test —— 按键硬件测试
 ******************************************************************************
 *  用途：判断"按键按了没反应"到底是【硬件接线问题】还是【软件问题】
 *
 *  这个程序做三件事：
 *    ① 每秒打印三个按键引脚的【原始电平】（不看消抖，直接读 GPIO）
 *    ② 实时检测电平变化，一有变化立刻打印（能看出抖动）
 *    ③ 用正式的消抖逻辑判断短按/长按，和原始电平对照
 *
 *  ★ 怎么用：
 *     烧进去 → 打开 serial.bat → 什么都不按，看三个引脚的原始电平
 *        · 应该都是 0（未按下 → 引脚被上拉为高，代码返回 0）
 *        · 如果哪个是 1 → ★ 那个按键接线有问题（可能接到了 GND，
 *          或者接了同侧两脚导致永远导通）
 *     然后按 KEY1 / KEY2 / KEY3：
 *        · 按下时应该变成 1，松开回到 0
 *        · 如果按了电平不变 → ★ 线没接好（或接错引脚）
 ******************************************************************************
 */
#include "bsp.h"
#include <stdio.h>

int main(void)
{
    uint8_t  last_raw[3] = { 9, 9, 9 };
    uint32_t ms = 0;
    uint8_t  i;

    bsp_init();
    uart1_init();

    printf("\r\n\r\n");
    printf("========================================\r\n");
    printf("  24 按键硬件测试\r\n");
    printf("  判断按键是接线问题还是软件问题\r\n");
    printf("========================================\r\n\r\n");

    printf("【接线说明】\r\n");
    printf("  按键一端 -> 对应引脚，另一端 -> GND\r\n");
    printf("  KEY1 -> PA1（板丝印 A1）\r\n");
    printf("  KEY2 -> PA4（板丝印 A4）\r\n");
    printf("  KEY3 -> PA5（板丝印 A5）\r\n");
    printf("  ★ 用轻触开关【对角的两只脚】，同侧那对永远导通\r\n\r\n");

    printf("【第一步】先别按，看下面三个引脚的原始电平\r\n");
    printf("  ★ 正常应该是 0（没按下）\r\n");
    printf("  ★ 如果哪个是 1 -> 那个按键的接线有问题\r\n\r\n");

    delay_ms(1500);

    printf("【第二步】按下按键，看电平是否变成 1\r\n");
    printf("--------------------------------------------------------------\r\n");
    printf("   时间    KEY1   KEY2   KEY3    说明\r\n");
    printf("--------------------------------------------------------------\r\n");

    while (1) {
        uint8_t raw[3];
        uint8_t changed = 0;
        uint8_t e1, e2, e3;

        /* --- 直接读 GPIO（不经任何消抖）--- */
        raw[0] = (GPIO_ReadInputDataBit(GPIOA, KEY1_PIN) == Bit_RESET) ? 1 : 0;
        raw[1] = (GPIO_ReadInputDataBit(GPIOA, KEY2_PIN) == Bit_RESET) ? 1 : 0;
        raw[2] = (GPIO_ReadInputDataBit(GPIOA, KEY3_PIN) == Bit_RESET) ? 1 : 0;

        for (i = 0; i < 3; i++) {
            if (raw[i] != last_raw[i]) changed = 1;
        }

        if (changed) {
            printf("  %5lu     %u      %u      %u     ★ ",
                   (unsigned long)(ms / 1000), raw[0], raw[1], raw[2]);
            for (i = 0; i < 3; i++) {
                if (raw[i] != last_raw[i]) {
                    printf("KEY%u%s  ", (unsigned)(i + 1), raw[i] ? "按下" : "松开");
                }
            }
            printf("\r\n");
            for (i = 0; i < 3; i++) last_raw[i] = raw[i];

            /* ★ 按键反馈：板载 LED 亮 100ms */
            led_onboard(1);
            delay_ms(80);
            led_onboard(0);
        }

        /* --- 每 2 秒打印一次（即使没变化，也说明程序在跑）--- */
        ms += 50;
        if ((ms % 2000) == 0) {
            e1 = key_get_event(1);
            e2 = key_get_event(2);
            e3 = key_get_event(3);

            printf("  %5lu     %u      %u      %u      运行中（消抖后读取）",
                   (unsigned long)(ms / 1000),
                   (GPIO_ReadInputDataBit(GPIOA, KEY1_PIN) == Bit_RESET) ? 1 : 0,
                   (GPIO_ReadInputDataBit(GPIOA, KEY2_PIN) == Bit_RESET) ? 1 : 0,
                   (GPIO_ReadInputDataBit(GPIOA, KEY3_PIN) == Bit_RESET) ? 1 : 0);

            if (e1 == 1)      printf("  [KEY1 短按]");
            else if (e1 == 2) printf("  [KEY1 长按]");
            if (e2 == 1)      printf("  [KEY2 短按]");
            else if (e2 == 2) printf("  [KEY2 长按]");
            if (e3 == 1)      printf("  [KEY3 短按]");
            else if (e3 == 2) printf("  [KEY3 长按]");

            printf("\r\n");
        }

        delay_ms(50);
    }
}

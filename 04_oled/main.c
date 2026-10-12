/**
 ******************************************************************************
 *  04_oled —— 步骤 4：OLED 显示验证
 ******************************************************************************
 *  目的：确认 OLED 能亮、能显示
 *
 *  接线：OLED VCC→3.3  GND→G  SCL→B6  SDA→B7
 *        （如果三个模块都接好了，这个程序会顺便扫一遍 I2C）
 ******************************************************************************
 */
#include "bsp.h"
#include "i2c.h"
#include "oled.h"

int main(void)
{
    uint8_t addr, n = 0;
    uint32_t sec = 0;

    bsp_init();
    uart1_init();
    i2c_init();

    printf("\r\n\r\n========================================\r\n");
    printf("  501 个人健康助手 —— 步骤 4：OLED 显示\r\n");
    printf("========================================\r\n\r\n");

    /* ---- 先扫一遍 I2C，看看都有谁在 ---- */
    printf("I2C 扫描：\r\n");
    for (addr = 0x01; addr < 0x80; addr++) {
        if (i2c_probe(addr) == I2C_OK) {
            printf("  发现 0x%02X\r\n", addr);
            n++;
        }
    }
    printf("共 %u 个设备\r\n\r\n", n);

    if (i2c_probe(OLED_ADDR) != I2C_OK) {
        printf("X 找不到 OLED（0x3C）！\r\n");
        printf("  检查：VCC→3.3、GND→G、SCL→B6、SDA→B7\r\n");
        printf("  程序停在这，接好线按复位重启。\r\n");
        while (1) {
            led_onboard(1); delay_ms(100);
            led_onboard(0); delay_ms(100);
        }
    }
    printf("√ 找到 OLED\r\n\r\n");

    /* ---- 初始化屏幕 ---- */
    oled_init();
    oled_clear();

    oled_draw_str(0, 0, "501 Health Helper");
    oled_draw_str(0, 2, "OLED OK!");
    oled_draw_str(0, 4, "SCL=PB6  SDA=PB7");
    oled_draw_str(0, 6, "Addr: 0x3C");
    oled_refresh();

    printf("√ OLED 已初始化，屏幕应该亮了\r\n");
    printf("  如果屏幕没亮：调一下 OLED 上的电位器/看是不是 VCC 没接\r\n\r\n");

    /* ---- 主循环：每秒更新一次计数，验证刷新正常 ---- */
    while (1) {
        oled_printf(0, 7, "Run: %u s      ", (unsigned)sec);
        oled_refresh();

        printf("[%u] OLED 刷新中\r\n", (unsigned)sec);
        sec++;

        led_onboard(1); delay_ms(100);
        led_onboard(0); delay_ms(900);
    }
}

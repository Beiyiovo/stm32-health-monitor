/**
 ******************************************************************************
 *  10_i2c_debug —— I2C 硬件诊断（扫不到设备时用这个）
 ******************************************************************************
 *  它做这些测试：
 *      ① 把 B6/B7 当普通 IO，测能不能拉高拉低（查短路）
 *      ② 读 B6/B7 的输入电平（查上拉电阻）
 *      ③ 读 OLED 的 SCL/SDA 侧电平（查线通不通）
 *      ④ 最后才做 I2C 扫描
 *
 *  ★ 用法：烧进去，看串口打印，把结果发我
 ******************************************************************************
 */
#include "bsp.h"
#include "i2c.h"

/* ==================== 把引脚临时配成普通 IO ==================== */

static void pin_init(GPIO_TypeDef *port, uint16_t pin, GPIOMode_TypeDef mode)
{
    GPIO_InitTypeDef gpio;
    gpio.GPIO_Pin   = pin;
    gpio.GPIO_Mode  = mode;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(port, &gpio);
}

/* 输出模式下设置电平并读回（★ 读回用 ODR，测短路要读 IDR，所以配成输入读） */
static uint8_t test_pin(GPIO_TypeDef *port, uint16_t pin, const char *name)
{
    uint8_t ok = 1;

    printf("\r\n  ---- 测试 %s ----\r\n", name);

    /* --- 测 1：能不能拉低 --- */
    pin_init(port, pin, GPIO_Mode_Out_PP);
    GPIO_ResetBits(port, pin);
    delay_ms(10);
    /* 读回：要配成输入才能读真实电平 */
    pin_init(port, pin, GPIO_Mode_IN_FLOATING);
    delay_ms(5);
    {
        uint8_t v = GPIO_ReadInputDataBit(port, pin);
        printf("    拉低后读回: %u  %s\r\n", v, v == 0 ? "√ 正常" : "X 拉不低！可能被上拉到 3.3V");
        if (v != 0) ok = 0;
    }

    /* --- 测 2：能不能拉高 --- */
    pin_init(port, pin, GPIO_Mode_Out_PP);
    GPIO_SetBits(port, pin);
    delay_ms(10);
    pin_init(port, pin, GPIO_Mode_IN_FLOATING);
    delay_ms(5);
    {
        uint8_t v = GPIO_ReadInputDataBit(port, pin);
        printf("    拉高后读回: %u  %s\r\n", v, v == 1 ? "√ 正常" : "X 拉不高！★★ 可能被短路到 GND 了");
        if (v != 1) ok = 0;
    }

    /* --- 测 3：悬空时读电平（查上拉） --- */
    pin_init(port, pin, GPIO_Mode_IN_FLOATING);
    delay_ms(10);
    {
        uint8_t v = GPIO_ReadInputDataBit(port, pin);
        printf("    悬空时电平: %u  ", v);
        if (v == 1) {
            printf("√ 有上拉（正常，I2C 需要）\r\n");
        } else {
            printf("★ 读到 0 —— 没有上拉电阻，或者被拉到 GND 了\r\n");
            printf("       → 需要加 4.7kΩ 上拉：这根线到 3.3V 之间接一个\r\n");
        }
    }

    return ok;
}

/* ==================== 主程序 ==================== */
int main(void)
{
    uint8_t addr, n = 0;
    uint32_t round = 0;

    bsp_init();
    uart1_init();

    printf("\r\n\r\n");
    printf("==========================================\r\n");
    printf("  501 毕设 —— I2C 硬件诊断\r\n");
    printf("==========================================\r\n");
    printf("  这个程序会测：\r\n");
    printf("    1. B6/B7 引脚能不能正常拉高拉低（查短路）\r\n");
    printf("    2. 悬空时的电平（查有没有上拉电阻）\r\n");
    printf("    3. I2C 扫描\r\n");
    printf("==========================================\r\n");

    while (1) {
        round++;
        printf("\r\n\r\n########## 第 %u 轮诊断 ##########\r\n", (unsigned)round);

        /* ===== 测试 1、2、3 ===== */
        test_pin(GPIOB, GPIO_Pin_6, "B6（应该是 SCL）");
        test_pin(GPIOB, GPIO_Pin_7, "B7（应该是 SDA）");

        /* ===== 测试 4：I2C 扫描 ===== */
        printf("\r\n  ---- I2C 扫描 ----\r\n");
        i2c_init();

        n = 0;
        for (addr = 0x01; addr < 0x80; addr++) {
            if (i2c_probe(addr) == I2C_OK) {
                printf("    发现设备: 0x%02X", addr);
                if (addr == 0x3C || addr == 0x3D) printf("  <- SSD1306 OLED");
                if (addr == 0x57) printf("  <- MAX30102");
                if (addr == 0x68 || addr == 0x69) printf("  <- MPU6050");
                printf("\r\n");
                n++;
            }
        }
        printf("    共发现 %u 个设备\r\n", n);

        /* ===== 结论 ===== */
        printf("\r\n  ======== 结论 ========\r\n");
        if (n == 0) {
            printf("    一个都没扫到。看上面 B6/B7 的测试结果：\r\n");
            printf("      · 如果\"悬空时电平 = 0\" → ★ 缺上拉电阻（最常见的坑）\r\n");
            printf("      · 如果\"拉不高\" → ★ 线路短路到 GND 了，检查接线\r\n");
            printf("      · 如果两项都正常 → 检查 OLED 的 SCL/SDA 是不是接在\r\n");
            printf("        和 B6/B7 相同的【面包板列】上\r\n");
        } else {
            printf("    ★ 扫到 %u 个设备，I2C 总线是好的\r\n", n);
        }
        printf("  ======================\r\n");

        printf("\r\n  每 8 秒重新诊断一次...\r\n");
        delay_ms(8000);
    }
}

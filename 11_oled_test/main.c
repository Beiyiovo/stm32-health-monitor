/**
 ******************************************************************************
 *  11_oled_test —— OLED 模块硬件测试（★ 软件 I2C，手动控制每一步）
 ******************************************************************************
 *  为什么用软件 I2C：
 *      硬件 I2C 出问题时，你只知道"扫不到"，不知道卡在哪一步。
 *      软件 I2C 能【逐步打印】：
 *        ① 引脚能不能拉高拉低
 *        ② 总线空闲时是不是高电平（有没有上拉）
 *        ③ START 时序对不对
 *        ④ 发地址后有没有 ACK（★ 这一步直接判定模块死活）
 *
 *  接线：OLED GND→G  VCC→3.3  SCL→B6  SDA→B7
 *        ★ SCL/SDA 各需要一个 4.7kΩ 上拉到 3.3V
 *
 *  看串口（115200）输出的结论
 ******************************************************************************
 */
#include "bsp.h"

/* ==================== 引脚操作 ==================== */

#define SCL_PORT    GPIOB
#define SCL_PIN     GPIO_Pin_6          /* B6 */
#define SDA_PORT    GPIOB
#define SDA_PIN     GPIO_Pin_7          /* B7 */

#define SCL_H()     GPIO_SetBits(SCL_PORT, SCL_PIN)
#define SCL_L()     GPIO_ResetBits(SCL_PORT, SCL_PIN)
#define SDA_H()     GPIO_SetBits(SDA_PORT, SDA_PIN)
#define SDA_L()     GPIO_ResetBits(SDA_PORT, SDA_PIN)

/* ★ I2C 是开漏总线：输出低 = 强拉低；输出高 = 实际上"释放"（靠上拉拉高） */
static void scl_out(void) { GPIO_InitTypeDef g; g.GPIO_Pin=SCL_PIN; g.GPIO_Mode=GPIO_Mode_Out_OD; g.GPIO_Speed=GPIO_Speed_50MHz; GPIO_Init(SCL_PORT,&g); }
static void sda_out(void) { GPIO_InitTypeDef g; g.GPIO_Pin=SDA_PIN; g.GPIO_Mode=GPIO_Mode_Out_OD; g.GPIO_Speed=GPIO_Speed_50MHz; GPIO_Init(SDA_PORT,&g); }
static void sda_in(void)  { GPIO_InitTypeDef g; g.GPIO_Pin=SDA_PIN; g.GPIO_Mode=GPIO_Mode_IPU;    g.GPIO_Speed=GPIO_Speed_50MHz; GPIO_Init(SDA_PORT,&g); }

#define SDA_READ()  GPIO_ReadInputDataBit(SDA_PORT, SDA_PIN)

/* ==================== 软件 I2C 时序 ==================== */

static void i2c_delay(void) { delay_us(5); }    /* 约 100kHz */

static void i2c_start(void)
{
    sda_out(); scl_out();
    SDA_H(); SCL_H(); i2c_delay();
    SDA_L(); i2c_delay();           /* SCL 高时 SDA 下降 = START */
    SCL_L(); i2c_delay();
}

static void i2c_stop(void)
{
    sda_out(); scl_out();
    SDA_L(); i2c_delay();
    SCL_H(); i2c_delay();
    SDA_H(); i2c_delay();           /* SCL 高时 SDA 上升 = STOP */
}

/* 写一个字节，返回 0 = 收到 ACK，1 = 没收到 */
static uint8_t i2c_write_byte(uint8_t b)
{
    uint8_t i, ack;

    sda_out();
    for (i = 0; i < 8; i++) {
        if (b & 0x80) SDA_H(); else SDA_L();
        i2c_delay();
        SCL_H(); i2c_delay();
        SCL_L(); i2c_delay();
        b <<= 1;
    }

    /* 第 9 个时钟：释放 SDA，读从机的 ACK */
    sda_in();
    SDA_H();                        /* 释放（开漏 + 上拉） */
    i2c_delay();
    SCL_H(); i2c_delay();
    ack = (SDA_READ() == Bit_RESET) ? 0 : 1;    /* 从机拉低 = ACK */
    SCL_L(); i2c_delay();
    sda_out();

    return ack;
}

/* ==================== 第 1 步：引脚能力测试 ==================== */

static void test_pins(void)
{
    uint8_t v;
    GPIO_InitTypeDef g;

    printf("\r\n【第 1 步】引脚能力测试（能不能拉高拉低）\r\n");
    printf("----------------------------------------\r\n");

    /* B6
       ★ 修正（2026-10-05）：保持推挽输出模式直接读 IDR。
       之前改成 GPIO_Mode_IN_FLOATING 再读是错的 ——
       改成输入就等于释放引脚，上拉电阻立刻把它拉高，读到 1 是假象。 */
    g.GPIO_Pin = SCL_PIN; g.GPIO_Mode = GPIO_Mode_Out_PP; g.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(SCL_PORT, &g);
    GPIO_ResetBits(SCL_PORT, SCL_PIN); delay_ms(20);
    v = GPIO_ReadInputDataBit(SCL_PORT, SCL_PIN);
    printf("  B6 拉低后读回: %u  %s\r\n", v, v == 0 ? "√ 能拉低" : "X ★拉不低（被外部强拉高）");

    GPIO_SetBits(SCL_PORT, SCL_PIN); delay_ms(20);
    v = GPIO_ReadInputDataBit(SCL_PORT, SCL_PIN);
    printf("  B6 拉高后读回: %u  %s\r\n", v, v == 1 ? "√ 能拉高" : "X ★拉不高（短路到GND？）");

    /* B7 */
    g.GPIO_Pin = SDA_PIN;
    g.GPIO_Mode = GPIO_Mode_Out_PP; GPIO_Init(SDA_PORT, &g);
    GPIO_ResetBits(SDA_PORT, SDA_PIN); delay_ms(20);
    v = GPIO_ReadInputDataBit(SDA_PORT, SDA_PIN);
    printf("  B7 拉低后读回: %u  %s\r\n", v, v == 0 ? "√ 能拉低" : "X ★拉不低（被外部强拉高）");

    GPIO_SetBits(SDA_PORT, SDA_PIN); delay_ms(20);
    v = GPIO_ReadInputDataBit(SDA_PORT, SDA_PIN);
    printf("  B7 拉高后读回: %u  %s\r\n", v, v == 1 ? "√ 能拉高" : "X ★拉不高（短路到GND？）");
}

/* ==================== 第 2 步：总线空闲电平 ==================== */

static void test_bus_idle(void)
{
    uint8_t scl, sda;

    printf("\r\n【第 2 步】总线空闲电平（两根线都释放）\r\n");
    printf("----------------------------------------\r\n");

    sda_in();
    scl_out(); sda_out();
    SCL_H(); SDA_H();               /* 开漏输出高 = 释放 */
    delay_ms(5);

    /* 读真实电平要配成输入 */
    {
        GPIO_InitTypeDef g;
        g.GPIO_Pin = SCL_PIN; g.GPIO_Mode = GPIO_Mode_IN_FLOATING; g.GPIO_Speed = GPIO_Speed_50MHz;
        GPIO_Init(SCL_PORT, &g);
        g.GPIO_Pin = SDA_PIN; GPIO_Init(SDA_PORT, &g);
    }
    delay_ms(5);
    scl = GPIO_ReadInputDataBit(SCL_PORT, SCL_PIN);
    sda = GPIO_ReadInputDataBit(SDA_PORT, SDA_PIN);

    printf("  SCL 空闲电平: %u  %s\r\n", scl,
           scl == 1 ? "√ 有上拉（正常）" : "★ 读到 0 —— 缺上拉电阻，或被拉到 GND");
    printf("  SDA 空闲电平: %u  %s\r\n", sda,
           sda == 1 ? "√ 有上拉（正常）" : "★ 读到 0 —— 缺上拉电阻，或被拉到 GND");

    sda_out(); scl_out();
    SDA_H(); SCL_H();
}

/* ==================== 第 3 步：START 时序 ==================== */

static void test_start(void)
{
    uint8_t v;

    printf("\r\n【第 3 步】START 时序测试\r\n");
    printf("----------------------------------------\r\n");

    sda_out(); scl_out();
    SDA_H(); SCL_H(); delay_ms(2);

    /* 拉低 SDA */
    SDA_L(); delay_ms(2);
    {
        GPIO_InitTypeDef g;
        g.GPIO_Pin = SDA_PIN; g.GPIO_Mode = GPIO_Mode_IN_FLOATING; g.GPIO_Speed = GPIO_Speed_50MHz;
        GPIO_Init(SDA_PORT, &g);
    }
    delay_ms(2);
    v = GPIO_ReadInputDataBit(SDA_PORT, SDA_PIN);
    printf("  拉低 SDA 后读回: %u  %s\r\n", v, v == 0 ? "√ 能拉低" : "X ★拉不低");

    sda_out(); SDA_H(); SCL_H();
}

/* ==================== 第 4 步：逐地址探测（软件 I2C） ==================== */

static uint8_t soft_probe(uint8_t addr)
{
    uint8_t ack;

    sda_out(); scl_out();
    SDA_H(); SCL_H(); delay_ms(1);

    i2c_start();
    ack = i2c_write_byte((uint8_t)(addr << 1));     /* 写方向 */
    i2c_stop();

    delay_ms(2);
    return (ack == 0) ? 1 : 0;                      /* 1 = 有应答 */
}

static void test_scan(void)
{
    uint8_t addr, n = 0;

    printf("\r\n【第 4 步】逐地址探测（软件 I2C，扫全部地址）\r\n");
    printf("----------------------------------------\r\n");

    /* 先重点测 OLED 的两个常见地址 */
    printf("  ★ 重点测 OLED 的两个常见地址：\r\n");
    printf("     0x3C -> %s\r\n", soft_probe(0x3C) ? "★★★ 有应答！模块是好的！" : "无应答");
    printf("     0x3D -> %s\r\n", soft_probe(0x3D) ? "★★★ 有应答！模块是好的！" : "无应答");

    printf("\r\n  扫描全部地址 0x01~0x7F：\r\n");
    for (addr = 0x01; addr < 0x80; addr++) {
        if (soft_probe(addr)) {
            printf("    发现设备: 0x%02X", addr);
            if (addr == 0x3C || addr == 0x3D) printf("  <- SSD1306 OLED");
            if (addr == 0x57) printf("  <- MAX30102");
            if (addr == 0x68 || addr == 0x69) printf("  <- MPU6050");
            printf("\r\n");
            n++;
        }
    }
    printf("\r\n  共发现 %u 个设备\r\n", n);

    /* ---- 结论 ---- */
    printf("\r\n========================================\r\n");
    printf("              测 试 结 论\r\n");
    printf("========================================\r\n");
    if (n > 0) {
        printf("  ★★★ 总线上有设备应答！I2C 是通的。\r\n");
        printf("      → 如果没有 0x3C/0x3D，说明 OLED 的地址不是常见的 0x3C\r\n");
        printf("      → 如果看到了 0x3C/0x3D，那 OLED 是好的\r\n");
    } else {
        printf("  ✗ 没有任何设备应答。按下面顺序排查：\r\n\r\n");
        printf("  ① 看【第 1 步】→ 有\"拉不高\"吗？\r\n");
        printf("       有 → ★ 线路短路到 GND，检查接线\r\n\r\n");
        printf("  ② 看【第 2 步】→ SCL/SDA 空闲电平是 1 吗？\r\n");
        printf("       是 0 → ★ 缺 4.7kΩ 上拉电阻（SCL、SDA 各一个到 3.3V）\r\n\r\n");
        printf("  ③ 上拉和短路都正常 → ★ 用万用表通断档量：\r\n");
        printf("       STM32 的 B6 针  ←→  OLED 的 SCL 脚   应该响\r\n");
        printf("       STM32 的 B7 针  ←→  OLED 的 SDA 脚   应该响\r\n");
        printf("       OLED 的 GND 脚  ←→  面包板 GND       应该响\r\n");
        printf("       OLED 的 VCC 脚  ←→  3.3V             应该响\r\n");
        printf("       哪项不响 = 那段线断了\r\n\r\n");
        printf("  ④ 都通 → ★ 把 OLED 拔下来，4 根线【直接】接 STM32\r\n");
        printf("       （绕过面包板）再测一次\r\n");
        printf("       还不行 → ★ 模块坏了，换一个\r\n\r\n");
        printf("  ⑤ 检查 OLED 是不是【7 脚 SPI 版】：\r\n");
        printf("       丝印 GND VCC D0 D1 RES DC CS = SPI 接口，用不了 I2C\r\n");
    }
    printf("========================================\r\n");
}

/* ==================== 主程序 ==================== */

int main(void)
{
    uint32_t round = 0;

    bsp_init();
    uart1_init();

    printf("\r\n\r\n");
    printf("========================================\r\n");
    printf("  501 毕设 —— OLED / I2C 硬件测试\r\n");
    printf("  （软件 I2C，逐步诊断）\r\n");
    printf("========================================\r\n");

    while (1) {
        round++;
        printf("\r\n\r\n██████ 第 %u 轮 ██████\r\n", (unsigned)round);

        test_pins();            /* 第 1 步 */
        test_bus_idle();        /* 第 2 步 */
        test_start();           /* 第 3 步 */
        test_scan();            /* 第 4 步 */

        printf("\r\n  10 秒后重新测试...\r\n");

        /* 心跳 */
        {
            uint8_t i;
            for (i = 0; i < 100; i++) {
                led_onboard((i % 20) < 2 ? 1 : 0);
                delay_ms(100);
            }
        }
    }
}

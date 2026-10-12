/**
 ******************************************************************************
 *  15_i2c_hunt —— I2C 总线终极排查（★ 含手动短接测试）
 ******************************************************************************
 *  这个程序做 5 件事：
 *      【1】实时显示 B6/B7 的电平 —— ★ 让你手动短接验证 STM32 的输入功能
 *      【2】硬件 I2C 用 3 种速率扫描
 *      【3】软件 I2C 用 3 种速率扫描
 *      【4】重点报告 0x3C/0x3D/0x57/0x68 这几个关键地址
 *      【5】给出明确结论
 *
 *  ★ 接线：
 *      STM32 的 B6 ←→ 模块的 SCL
 *      STM32 的 B7 ←→ 模块的 SDA
 *      STM32 的 G  ←→ 模块的 GND
 *      STM32 的 3.3 ←→ 模块的 VCC
 *      ★ 两个 4.7kΩ：B6←→3.3、B7←→3.3
 *      USB-TTL：TXD→A10、RXD→A9、GND→G
 ******************************************************************************
 */
#include "bsp.h"
#include "i2c.h"

#define SCL_PORT    GPIOB
#define SCL_PIN     GPIO_Pin_6
#define SDA_PORT    GPIOB
#define SDA_PIN     GPIO_Pin_7

/* ==================== 软件 I2C ==================== */
static uint16_t g_dly = 10;

static void scl_out(void) { GPIO_InitTypeDef g; g.GPIO_Pin=SCL_PIN; g.GPIO_Mode=GPIO_Mode_Out_OD; g.GPIO_Speed=GPIO_Speed_50MHz; GPIO_Init(SCL_PORT,&g); }
static void sda_out(void) { GPIO_InitTypeDef g; g.GPIO_Pin=SDA_PIN; g.GPIO_Mode=GPIO_Mode_Out_OD; g.GPIO_Speed=GPIO_Speed_50MHz; GPIO_Init(SDA_PORT,&g); }
static void sda_in (void) { GPIO_InitTypeDef g; g.GPIO_Pin=SDA_PIN; g.GPIO_Mode=GPIO_Mode_IPU;    g.GPIO_Speed=GPIO_Speed_50MHz; GPIO_Init(SDA_PORT,&g); }
static void scl_in (void) { GPIO_InitTypeDef g; g.GPIO_Pin=SCL_PIN; g.GPIO_Mode=GPIO_Mode_IPU;    g.GPIO_Speed=GPIO_Speed_50MHz; GPIO_Init(SCL_PORT,&g); }

#define SCL_H()     GPIO_SetBits(SCL_PORT, SCL_PIN)
#define SCL_L()     GPIO_ResetBits(SCL_PORT, SCL_PIN)
#define SDA_H()     GPIO_SetBits(SDA_PORT, SDA_PIN)
#define SDA_L()     GPIO_ResetBits(SDA_PORT, SDA_PIN)
#define SCL_READ()  GPIO_ReadInputDataBit(SCL_PORT, SCL_PIN)
#define SDA_READ()  GPIO_ReadInputDataBit(SDA_PORT, SDA_PIN)

static void dly(void) { delay_us(g_dly); }

static void sw_start(void)
{
    sda_out(); scl_out();
    SDA_H(); SCL_H(); dly();
    SDA_L(); dly();
    SCL_L(); dly();
}

static void sw_stop(void)
{
    sda_out(); scl_out();
    SDA_L(); dly();
    SCL_H(); dly();
    SDA_H(); dly();
}

static uint8_t sw_write(uint8_t b)
{
    uint8_t i, ack;
    sda_out();
    for (i = 0; i < 8; i++) {
        if (b & 0x80) SDA_H(); else SDA_L();
        dly(); SCL_H(); dly(); SCL_L(); dly();
        b <<= 1;
    }
    sda_in(); dly();
    SCL_H(); dly();
    ack = (SDA_READ() == Bit_RESET) ? 1 : 0;
    SCL_L(); dly();
    sda_out();
    return ack;
}

static uint8_t sw_probe(uint8_t addr)
{
    uint8_t ack;
    sda_out(); scl_out();
    SDA_H(); SCL_H(); dly();
    sw_start();
    ack = sw_write((uint8_t)(addr << 1));
    sw_stop();
    delay_ms(1);
    return ack;
}

/* ==================== 【1】手动短接测试 ==================== */
static void manual_test(void)
{
    uint32_t i;
    uint8_t s, d;

    printf("\r\n\r\n##############################################\r\n");
    printf("  【第 1 步】★ 手动短接测试（验证 STM32 的输入功能）\r\n");
    printf("##############################################\r\n\r\n");
    printf("  ★ 现在 B6 和 B7 都配成【输入】模式（既不输出高也不输出低）\r\n");
    printf("  ★ 请拿一根杜邦线，依次做这两个动作：\r\n\r\n");
    printf("     动作 A：把【STM32 的 B7 针】用线连到【GND】\r\n");
    printf("             → 下面 SDA 那一列应该从 1 变成 0\r\n\r\n");
    printf("     动作 B：把【STM32 的 B6 针】用线连到【GND】\r\n");
    printf("             → 下面 SCL 那一列应该从 1 变成 0\r\n\r\n");
    printf("  ★★ 如果你把 B7 接到 GND 了，但下面一直显示 SDA=1：\r\n");
    printf("       → STM32 的 B7 引脚【输入功能坏了】！这是根本原因\r\n\r\n");
    printf("  现在开始实时显示（10 秒，每 200ms 一行）：\r\n");
    printf("  ----------------------------------------\r\n");
    printf("   序号 |  SCL |  SDA |  说明\r\n");
    printf("  ------+------+------+------------------\r\n");

    scl_in(); sda_in();

    for (i = 1; i <= 50; i++) {
        s = SCL_READ();
        d = SDA_READ();

        printf("   %4u |   %u  |   %u  |", (unsigned)i, s, d);
        if (s == 1 && d == 1)      printf("  都高（正常空闲）\r\n");
        else if (s == 0 && d == 1) printf("  ★ SCL 被拉到 GND\r\n");
        else if (s == 1 && d == 0) printf("  ★ SDA 被拉到 GND\r\n");
        else                       printf("  ★ 两根都被拉到 GND\r\n");

        delay_ms(200);
    }

    scl_out(); sda_out();
    SCL_H(); SDA_H();
}

/* ==================== 【2】硬件 I2C 多速率扫描 ==================== */
static uint8_t hw_scan(uint32_t speed_khz)
{
    uint8_t addr, n = 0;

    /* 重新初始化 I2C，改速率 */
    {
        GPIO_InitTypeDef gpio;
        I2C_InitTypeDef  i2c;

        /* ★★★ 必须先使能时钟！否则访问 I2C1 寄存器会 HardFault
           （这是本程序之前的 bug —— main 里没调 i2c_init()，
             这里又直接操作 I2C1，导致跑飞） */
        RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);
        RCC_APB1PeriphClockCmd(RCC_APB1Periph_I2C1, ENABLE);

        I2C_Cmd(I2C1, DISABLE);
        I2C_DeInit(I2C1);

        gpio.GPIO_Pin   = SCL_PIN | SDA_PIN;
        gpio.GPIO_Mode  = GPIO_Mode_AF_OD;
        gpio.GPIO_Speed = GPIO_Speed_50MHz;
        GPIO_Init(GPIOB, &gpio);

        i2c.I2C_Mode                = I2C_Mode_I2C;
        i2c.I2C_DutyCycle           = I2C_DutyCycle_2;
        i2c.I2C_OwnAddress1         = 0x00;
        i2c.I2C_Ack                 = I2C_Ack_Enable;
        i2c.I2C_AcknowledgedAddress = I2C_AcknowledgedAddress_7bit;
        i2c.I2C_ClockSpeed          = speed_khz * 1000;
        I2C_Init(I2C1, &i2c);
        I2C_Cmd(I2C1, ENABLE);
    }

    for (addr = 0x01; addr < 0x80; addr++) {
        if (i2c_probe(addr) == I2C_OK) {
            printf("        0x%02X\r\n", addr);
            n++;
        }
    }
    return n;
}

/* ==================== 【3】软件 I2C 多速率扫描 ==================== */
static uint8_t sw_scan_all(uint16_t dly_us)
{
    uint8_t addr, n = 0;

    g_dly = dly_us;

    for (addr = 0x01; addr < 0x80; addr++) {
        if (sw_probe(addr)) {
            printf("        0x%02X\r\n", addr);
            n++;
        }
    }
    return n;
}

/* ==================== 主程序 ==================== */
int main(void)
{
    uint8_t n;
    uint32_t round = 0;
    uint8_t i;
    static const uint8_t KEY_ADDRS[] = {0x3C, 0x3D, 0x57, 0x68, 0x69, 0x76, 0x1E};
    uint8_t k;

    bsp_init();
    uart1_init();
    i2c_init();             /* ★ 必须调！否则后面 hw_scan 里操作 I2C1 会 HardFault */

    printf("\r\n\r\n");
    printf("================================================\r\n");
    printf("  501 毕设 —— I2C 总线终极排查\r\n");
    printf("================================================\r\n");
    printf("  接线：B6->SCL  B7->SDA  G->GND  3.3->VCC\r\n");
    printf("        ★ 两个 4.7kΩ：B6<->3.3、B7<->3.3\r\n");
    printf("================================================\r\n");

    /* ★ 每轮先做手动短接测试，再做扫描 */
    while (1) {
        round++;

        /* ---- 第 1 步：手动短接测试（只在第 1 轮做，后面省略） ---- */
        if (round == 1) {
            manual_test();
            printf("\r\n\r\n  手动测试结束。下面开始自动扫描。\r\n");
            printf("  每 20 秒重复一次扫描（不再做手动测试）\r\n");
        }

        printf("\r\n\r\n██████████ 第 %u 轮扫描 ██████████\r\n", (unsigned)round);

        /* ---- 第 2 步：硬件 I2C，多种速率 ---- */
        printf("\r\n【2】硬件 I2C 扫描\r\n");
        printf("----------------------------------------\r\n");

        printf("    100 kHz：\r\n");
        n = hw_scan(100);
        printf("      发现 %u 个\r\n", n);

        printf("    50 kHz：\r\n");
        n = hw_scan(50);
        printf("      发现 %u 个\r\n", n);

        printf("    ★ 10 kHz（最慢，最可靠）：\r\n");
        n = hw_scan(10);
        printf("      发现 %u 个\r\n", n);

        /* ---- 第 3 步：软件 I2C，多种速率 ---- */
        printf("\r\n【3】软件 I2C 扫描\r\n");
        printf("----------------------------------------\r\n");

        printf("    延时 5us（约 100kHz）：\r\n");
        n = sw_scan_all(5);
        printf("      发现 %u 个\r\n", n);

        printf("    延时 20us（约 25kHz）：\r\n");
        n = sw_scan_all(20);
        printf("      发现 %u 个\r\n", n);

        printf("    ★ 延时 100us（约 5kHz，极慢）：\r\n");
        n = sw_scan_all(100);
        printf("      发现 %u 个\r\n", n);

        /* ---- 第 4 步：重点地址逐个报 ---- */
        printf("\r\n【4】重点地址逐个测（软件 I2C，延时 20us）\r\n");
        printf("----------------------------------------\r\n");
        g_dly = 20;
        for (k = 0; k < sizeof(KEY_ADDRS); k++) {
            uint8_t a = KEY_ADDRS[k];
            const char *name = "";
            if (a == 0x3C || a == 0x3D) name = "SSD1306 OLED";
            if (a == 0x57) name = "MAX30102";
            if (a == 0x68 || a == 0x69) name = "MPU6050";
            if (a == 0x76) name = "BMP280";
            if (a == 0x1E) name = "HMC5883";
            printf("    0x%02X (%s) : %s\r\n", a, name,
                   sw_probe(a) ? "★★★ 有应答！" : "无应答");
        }

        /* ---- 结论 ---- */
        printf("\r\n================================================\r\n");
        printf("                    结 论\r\n");
        printf("================================================\r\n");
        printf("  ★ 如果【第 1 步手动短接】有效（接 GND 后读数变 0）：\r\n");
        printf("      → STM32 的输入功能正常，问题在模块或接线\r\n\r\n");
        printf("  ★ 如果【第 1 步手动短接】无效（接 GND 后读数还是 1）：\r\n");
        printf("      → ★★★ STM32 的引脚【输入功能坏了】\r\n");
        printf("         这是所有问题的根本原因！换一块 STM32 板子\r\n\r\n");
        printf("  ★ 如果所有扫描方式都扫不到，但第 1 步正常：\r\n");
        printf("      → 模块坏了（两个都坏的概率虽然低，但有可能）\r\n");
        printf("      → 建议拿第三个模块（MPU6050）再试\r\n");
        printf("      → 或者换一块 STM32 板子\n");
        printf("================================================\r\n");

        for (i = 0; i < 200; i++) {
            led_onboard((i % 30) < 2 ? 1 : 0);
            delay_ms(100);
        }
    }
}

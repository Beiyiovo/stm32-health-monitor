/**
 ******************************************************************************
 *  13_oled_force —— ★ 强制点亮 OLED（不管 ACK，直接发命令）
 ******************************************************************************
 *  思路（按用户要求）：
 *      ★ 不做任何"探测"，不管有没有 ACK，直接把 SSD1306 的初始化序列
 *        和显示数据【硬发】出去。
 *
 *      · 屏幕亮了      → ★★★ 模块是好的！
 *      · 屏幕不亮      → 模块坏了，或者线真的没接上
 *
 *  特点：
 *      · 软件 I2C（GPIO 手动时序），速度可调
 *      · ★ 依次尝试 0x3C 和 0x3D 两个地址
 *      · ★ 支持多种速率（从很慢到较快），总有一个能成
 *      · 同时往串口打印每一步，方便对照
 *
 *  接线：OLED GND→G  VCC→3.3  SCL→B6  SDA→B7
 *        ★ SCL/SDA 各需 4.7kΩ 上拉到 3.3V
 ******************************************************************************
 */
#include "bsp.h"

#define SCL_PORT    GPIOB
#define SCL_PIN     GPIO_Pin_6      /* B6 */
#define SDA_PORT    GPIOB
#define SDA_PIN     GPIO_Pin_7      /* B7 */

/* 当前使用的 I2C 地址（会在 0x3C / 0x3D 之间切换） */
static uint8_t g_addr = 0x3C;

/* 当前延时（微秒），可调 */
static uint16_t g_delay = 20;

/* ==================== 软件 I2C 底层 ==================== */

static void scl_out(void) { GPIO_InitTypeDef g; g.GPIO_Pin=SCL_PIN; g.GPIO_Mode=GPIO_Mode_Out_OD; g.GPIO_Speed=GPIO_Speed_50MHz; GPIO_Init(SCL_PORT,&g); }
static void sda_out(void) { GPIO_InitTypeDef g; g.GPIO_Pin=SDA_PIN; g.GPIO_Mode=GPIO_Mode_Out_OD; g.GPIO_Speed=GPIO_Speed_50MHz; GPIO_Init(SDA_PORT,&g); }
static void sda_in (void) { GPIO_InitTypeDef g; g.GPIO_Pin=SDA_PIN; g.GPIO_Mode=GPIO_Mode_IPU;    g.GPIO_Speed=GPIO_Speed_50MHz; GPIO_Init(SDA_PORT,&g); }

#define SCL_H()     GPIO_SetBits(SCL_PORT, SCL_PIN)
#define SCL_L()     GPIO_ResetBits(SCL_PORT, SCL_PIN)
#define SDA_H()     GPIO_SetBits(SDA_PORT, SDA_PIN)
#define SDA_L()     GPIO_ResetBits(SDA_PORT, SDA_PIN)
#define SDA_READ()  GPIO_ReadInputDataBit(SDA_PORT, SDA_PIN)

static void dly(void) { delay_us(g_delay); }

static void i2c_start(void)
{
    sda_out(); scl_out();
    SDA_H(); SCL_H(); dly();
    SDA_L(); dly();
    SCL_L(); dly();
}

static void i2c_stop(void)
{
    sda_out(); scl_out();
    SDA_L(); dly();
    SCL_H(); dly();
    SDA_H(); dly();
}

/* 写一个字节；返回 1 = 收到 ACK（但我们【不管】它，照发） */
static uint8_t i2c_write(uint8_t b)
{
    uint8_t i, ack;

    sda_out();
    for (i = 0; i < 8; i++) {
        if (b & 0x80) SDA_H(); else SDA_L();
        dly();
        SCL_H(); dly();
        SCL_L(); dly();
        b <<= 1;
    }

    /* 第 9 个时钟读 ACK */
    sda_in();
    dly();
    SCL_H(); dly();
    ack = (SDA_READ() == Bit_RESET) ? 1 : 0;
    SCL_L(); dly();
    sda_out();

    return ack;
}

/* ==================== SSD1306 命令/数据 ==================== */

/* 发一个命令字节：START + 地址 + 0x00 + cmd + STOP */
static uint8_t oled_cmd(uint8_t c)
{
    uint8_t ack1, ack2;

    i2c_start();
    ack1 = i2c_write((uint8_t)(g_addr << 1));
    ack2 = i2c_write(0x00);         /* 控制字节：后面是命令 */
    i2c_write(c);
    i2c_stop();

    return (uint8_t)(ack1 & ack2);
}

/* 发一批显示数据：START + 地址 + 0x40 + data... + STOP */
static uint8_t oled_data(const uint8_t *d, uint16_t len)
{
    uint16_t i;
    uint8_t ack;

    i2c_start();
    ack = i2c_write((uint8_t)(g_addr << 1));
    i2c_write(0x40);                /* 控制字节：后面是数据 */
    for (i = 0; i < len; i++) i2c_write(d[i]);
    i2c_stop();

    return ack;
}

/* ==================== SSD1306 初始化序列 ==================== */

static void oled_init_seq(void)
{
    oled_cmd(0xAE);     /* 关显示 */
    oled_cmd(0x20);     /* 内存寻址模式 */
    oled_cmd(0x10);     /* 页寻址 */
    oled_cmd(0xB0);     /* 页起始 */
    oled_cmd(0xC8);     /* COM 扫描方向 */
    oled_cmd(0x00);
    oled_cmd(0x10);
    oled_cmd(0x40);
    oled_cmd(0x81);     /* 对比度 */
    oled_cmd(0xFF);
    oled_cmd(0xA1);     /* 段重映射 */
    oled_cmd(0xA6);     /* 正常显示 */
    oled_cmd(0xA8);     /* 多路复用 */
    oled_cmd(0x3F);
    oled_cmd(0xA4);
    oled_cmd(0xD3);
    oled_cmd(0x00);
    oled_cmd(0xD5);
    oled_cmd(0xF0);
    oled_cmd(0xD9);
    oled_cmd(0x22);
    oled_cmd(0xDA);
    oled_cmd(0x12);
    oled_cmd(0xDB);
    oled_cmd(0x20);
    oled_cmd(0x8D);
    oled_cmd(0x14);     /* ★ 电荷泵开 —— 不开屏幕不亮 */
    oled_cmd(0xAF);     /* 开显示 */
}

/* 把整屏填成某个字节图案 */
static void oled_fill(uint8_t pattern)
{
    uint8_t page, col[16];
    uint8_t i;

    for (i = 0; i < 16; i++) col[i] = pattern;

    for (page = 0; page < 8; page++) {
        oled_cmd(0xB0 + page);      /* 页地址 */
        oled_cmd(0x00);             /* 列低 4 位 */
        oled_cmd(0x10);             /* 列高 4 位 */
        for (i = 0; i < 8; i++) {   /* 128 列 = 8 × 16 */
            oled_data(col, 16);
        }
    }
}

/* ==================== 测试一个地址 ==================== */

static void try_address(uint8_t addr)
{
    uint8_t ack;

    g_addr = addr;

    printf("\r\n------------------------------------------\r\n");
    printf("  尝试地址 0x%02X（延时 %u us）\r\n", addr, g_delay);
    printf("------------------------------------------\r\n");

    /* 发一个最短的命令 0xAE（关显示），看有没有 ACK */
    printf("  发 0xAE（关显示）... ");
    ack = oled_cmd(0xAE);
    printf("ACK = %u  %s\r\n", ack, ack ? "★ 有应答！模块活着！" : "无应答");

    /* ★ 不管有没有 ACK，都继续发完整初始化 */
    printf("  → 不管 ACK，继续发完整初始化序列...\r\n");
    oled_init_seq();

    printf("  → 全屏点亮（应该能看到屏幕变白/出现雪花点）\r\n");
    oled_fill(0xFF);
    delay_ms(1500);

    printf("  → 全屏熄灭\r\n");
    oled_fill(0x00);
    delay_ms(500);

    printf("  → 显示棋盘格（一半亮一半暗）\r\n");
    {
        uint8_t page, col[16], i;
        for (page = 0; page < 8; page++) {
            for (i = 0; i < 16; i++) col[i] = (page % 2) ? 0xFF : 0x00;
            oled_cmd(0xB0 + page);
            oled_cmd(0x00);
            oled_cmd(0x10);
            for (i = 0; i < 8; i++) oled_data(col, 16);
        }
    }
    delay_ms(1500);
}

/* ==================== 主程序 ==================== */

int main(void)
{
    uint8_t round = 0;
    uint8_t i;

    bsp_init();
    uart1_init();

    /* 先把引脚配好 */
    scl_out(); sda_out();
    SCL_H(); SDA_H();

    printf("\r\n\r\n");
    printf("============================================\r\n");
    printf("  501 毕设 —— ★ 强制点亮 OLED\r\n");
    printf("  （不管 ACK，直接发命令）\r\n");
    printf("============================================\r\n");
    printf("  接线：OLED GND->G   VCC->3.3   SCL->B6   SDA->B7\r\n");
    printf("        ★ SCL/SDA 各需 4.7kΩ 上拉到 3.3V\r\n");
    printf("============================================\r\n");
    printf("\r\n  ★ 盯着屏幕看！它会依次做：\r\n");
    printf("      1. 全屏点亮（应该整屏变白/出现亮块）\r\n");
    printf("      2. 全屏熄灭\n");
    printf("      3. 棋盘格（一半亮一半暗）\r\n");
    printf("  ★ 只要有任何一次看到屏幕有变化 = 模块是好的！\r\n");

    while (1) {
        round++;
        printf("\r\n\r\n██████ 第 %u 轮（用了 %u 次点亮尝试）██████\r\n",
               (unsigned)round, (unsigned)((round - 1) * 4));

        /* 用不同的延时（速率）多试几遍 —— 总有一个能成 */
        {
            static const uint16_t delays[] = {50, 20, 10, 5};
            uint8_t d;

            for (d = 0; d < 4; d++) {
                g_delay = delays[d];

                printf("\r\n======== 速率档 %u/4（延时 %u us）========\r\n", d + 1, g_delay);

                try_address(0x3C);      /* 最常见的地址 */
                try_address(0x3D);      /* 备用地址 */
            }
        }

        printf("\r\n\r\n  ★★★ 如果 4 档速率 × 2 个地址都试过了，屏幕【完全没反应】：\r\n");
        printf("       → 模块坏了，换一个；或者线真的没接上\r\n");
        printf("  ★★★ 如果屏幕有任何反应（亮/闪/花屏）：\r\n");
        printf("       → 模块是好的！把上面打印的\"ACK = 1\"那行发我，\r\n");
        printf("         我就知道该用哪个地址和哪个速率\r\n");

        /* 心跳 8 秒后重来 */
        for (i = 0; i < 80; i++) {
            led_onboard((i % 20) < 2 ? 1 : 0);
            delay_ms(100);
        }
    }
}

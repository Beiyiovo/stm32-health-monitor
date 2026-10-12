/**
 ******************************************************************************
 *  ds18b20.c —— DS18B20 温度传感器驱动（单总线，PA0）
 ******************************************************************************
 *  时序要点（72MHz 主频）：
 *      复位脉冲：拉低 ≥480us → 释放 → 等 15~60us → 读存在脉冲（低=器件在）
 *      写 1    ：拉低 1~15us → 释放 → 等 60us
 *      写 0    ：拉低 60us   → 释放 → 等 2us
 *      读      ：拉低 1us    → 释放 → 等 10us 采样 → 等 50us
 *
 *  命令：
 *      0xCC = 跳过 ROM（只有一个器件时用，省事）
 *      0x44 = 启动温度转换
 *      0xBE = 读暂存器（前 2 字节是温度）
 *
 *  ★ 硬件：DQ 通过 4.7kΩ 上拉到 3.3V
 ******************************************************************************
 */
#include "ds18b20.h"

/* ==================== 引脚方向切换 ==================== */

static void dq_output(void)
{
    GPIO_InitTypeDef gpio;
    gpio.GPIO_Pin   = DS_PIN;
    gpio.GPIO_Mode  = GPIO_Mode_Out_PP;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(DS_GPIO, &gpio);
}

static void dq_input(void)
{
    GPIO_InitTypeDef gpio;
    gpio.GPIO_Pin  = DS_PIN;
    gpio.GPIO_Mode = GPIO_Mode_IPU;         /* 上拉输入 */
    GPIO_Init(DS_GPIO, &gpio);
}

#define DQ_HIGH()   GPIO_SetBits(DS_GPIO, DS_PIN)
#define DQ_LOW()    GPIO_ResetBits(DS_GPIO, DS_PIN)
#define DQ_READ()   GPIO_ReadInputDataBit(DS_GPIO, DS_PIN)

/* ==================== 初始化 ==================== */

void ds18b20_init(void)
{
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);
    dq_output();
    DQ_HIGH();
    delay_ms(10);
}

/* ==================== 复位 + 存在脉冲检测 ==================== */

static uint8_t ds_reset(void)
{
    uint8_t present;

    dq_output();
    DQ_LOW();
    delay_us(500);              /* 拉低 500us（要求 ≥480us） */

    DQ_HIGH();
    dq_input();
    delay_us(60);               /* 等 60us 后采样 */

    present = (DQ_READ() == Bit_RESET) ? 1 : 0;   /* 低 = 器件存在 */

    delay_us(400);              /* 等时序走完 */
    return present;
}

/* ==================== 写一个字节 ==================== */

static void ds_write_byte(uint8_t b)
{
    uint8_t i;

    dq_output();
    for (i = 0; i < 8; i++) {
        DQ_LOW();
        if (b & 0x01) {
            delay_us(2);        /* 写 1：拉低 2us */
            DQ_HIGH();
            delay_us(65);
        } else {
            delay_us(65);       /* 写 0：拉低 65us */
            DQ_HIGH();
            delay_us(2);
        }
        b >>= 1;
    }
}

/* ==================== 读一个字节 ==================== */

static uint8_t ds_read_byte(void)
{
    uint8_t i, b = 0;

    for (i = 0; i < 8; i++) {
        b >>= 1;
        dq_output();
        DQ_LOW();
        delay_us(2);            /* 拉低 2us 启动时隙 */
        DQ_HIGH();
        dq_input();
        delay_us(10);           /* 10us 后采样 */
        if (DQ_READ() == Bit_SET) b |= 0x80;
        delay_us(55);           /* 等时隙走完 */
    }
    return b;
}

/* ==================== 温度转换 ==================== */

uint8_t ds18b20_start(void)
{
    if (!ds_reset()) return 0;
    ds_write_byte(0xCC);        /* 跳过 ROM */
    ds_write_byte(0x44);        /* 启动转换 */
    return 1;
}

/* ==================== 读温度（×10） ==================== */

int16_t ds18b20_read(void)
{
    uint8_t  buf[2];
    int16_t  raw;
    int32_t  t10;

    if (!ds_reset()) return DS18B20_ERR;

    ds_write_byte(0xCC);        /* 跳过 ROM */
    ds_write_byte(0xBE);        /* 读暂存器 */

    buf[0] = ds_read_byte();    /* 温度低字节 */
    buf[1] = ds_read_byte();    /* 温度高字节 */

    raw = (int16_t)(((uint16_t)buf[1] << 8) | buf[0]);

    /* DS18B20 分辨率 0.0625℃ = 1/16 */
    /* 先转成 ×10：raw * 10 / 16，四舍五入 */
    t10 = ((int32_t)raw * 10 + (raw >= 0 ? 8 : -8)) / 16;

    /* 合理性检查：-55 ~ +125℃ */
    if (t10 < -550 || t10 > 1250) return DS18B20_ERR;

    return (int16_t)t10;
}

/* ==================== 一步到位 ==================== */

int16_t ds18b20_get(void)
{
    if (!ds18b20_start()) return DS18B20_ERR;

    delay_ms(800);              /* 12 位分辨率需要 750ms */

    return ds18b20_read();
}

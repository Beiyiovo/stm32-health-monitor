/**
 ******************************************************************************
 *  i2c.c —— ★ 软件 I2C 驱动（GPIO 手动模拟，慢速）
 ******************************************************************************
 *  ★★ 2026-10-06 重写：
 *      原版用硬件 I2C1 外设，在本套硬件上【扫不到任何设备】。
 *      用 16_i2c_scope（逐位打印的软件 I2C）验证：
 *      ★ 软件 I2C + 慢速 → OLED 的 0x3C 有应答！
 *
 *      所以本驱动改为纯 GPIO 模拟，速率由 I2C_DELAY_US（默认 20us ≈ 25kHz）控制。
 *
 *  ★ 电气要求（和硬件 I2C 一样）：
 *      PB6/PB7 配成【开漏输出】，SCL 和 SDA 各需要一个 4.7kΩ 上拉到 3.3V
 ******************************************************************************
 */
#include "i2c.h"

/* ==================== 引脚宏 ==================== */

#define SCL_PORT    I2C_GPIO
#define SCL_PIN     I2C_SCL_PIN
#define SDA_PORT    I2C_GPIO
#define SDA_PIN     I2C_SDA_PIN

#define SCL_H()     GPIO_SetBits(SCL_PORT, SCL_PIN)
#define SCL_L()     GPIO_ResetBits(SCL_PORT, SCL_PIN)
#define SDA_H()     GPIO_SetBits(SDA_PORT, SDA_PIN)
#define SDA_L()     GPIO_ResetBits(SDA_PORT, SDA_PIN)

/* ★ 开漏输出模式下，IDR 仍反映引脚的真实电平，可以直接读 */
#define SCL_R()     GPIO_ReadInputDataBit(SCL_PORT, SCL_PIN)
#define SDA_R()     GPIO_ReadInputDataBit(SDA_PORT, SDA_PIN)

static void dly(void) { delay_us(I2C_DELAY_US); }

/* ==================== 初始化 ==================== */

void i2c_init(void)
{
    GPIO_InitTypeDef gpio;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);

    /* ★ 开漏输出（OD），高电平靠外部上拉电阻 */
    gpio.GPIO_Pin   = SCL_PIN | SDA_PIN;
    gpio.GPIO_Mode  = GPIO_Mode_Out_OD;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(I2C_GPIO, &gpio);

    /* 两根都释放（输出高 = 高阻，靠上拉拉高） */
    SCL_H();
    SDA_H();
    delay_us(50);
    /* ★★★ 2026-10-15 新增：上电先发 9 个时钟脉冲，解救可能被从机拉死的 SDA
     *
     * 【为什么】I2C 从机（MAX30102 最容易）如果在上电瞬间被复位打断，
     *   可能把 SDA 一直拉低 → 总线死锁 → 软件永远扫不到设备，只能断电重上。
     *   i2c_reset() 本来就有这个功能，但原来【全工程没有任何调用点】
     *   （map 里显示被链接器当死代码删掉了 438 字节）。
     *   ★ 转自制 PCB 后走线电容和上电时序都变了，这个风险比面包板更高。*/
    i2c_reset();
    delay_us(50);
}

/**
 * ★ 总线恢复：从机意外拉死 SDA 时，发 9 个 SCL 脉冲解救
 * （参考 STM32 I2C 调试实践：9 个时钟脉冲能让从机释放 SDA）
 */
void i2c_reset(void)
{
    uint8_t i;

    SDA_H();                    /* 释放 SDA */

    for (i = 0; i < 9; i++) {
        if (SDA_R() == Bit_SET) break;      /* SDA 已经释放，不用再发 */
        SCL_L(); delay_us(50);
        SCL_H(); delay_us(50);
    }

    /* 补一个 STOP：SCL 高时 SDA 由低变高 */
    SCL_L(); SDA_L(); delay_us(50);
    SCL_H(); delay_us(50);
    SDA_H(); delay_us(50);
}

/* ==================== 基本时序 ==================== */

static void i2c_start(void)
{
    SDA_H(); SCL_H(); dly();
    SDA_L(); dly();             /* SCL 高时 SDA 下降 = START */
    SCL_L(); dly();
}

static void i2c_stop(void)
{
    SDA_L(); dly();
    SCL_H(); dly();
    SDA_H(); dly();             /* SCL 高时 SDA 上升 = STOP */
}

/* 写一个字节；返回 1 = 收到 ACK */
static uint8_t i2c_write_byte(uint8_t b)
{
    uint8_t i, ack;

    for (i = 0; i < 8; i++) {
        if (b & 0x80) SDA_H(); else SDA_L();
        dly();
        SCL_H(); dly();
        SCL_L(); dly();
        b <<= 1;
    }

    /* 第 9 个时钟：释放 SDA，读从机的 ACK */
    SDA_H(); dly();
    SCL_H(); dly();
    ack = (SDA_R() == Bit_RESET) ? 1 : 0;
    SCL_L(); dly();

    return ack;
}

/* 读一个字节；ack=1 表示读完发 ACK（继续读），ack=0 发 NACK（最后一个字节） */
static uint8_t i2c_read_byte(uint8_t ack)
{
    uint8_t i, b = 0;

    SDA_H();                    /* 释放 SDA，让从机驱动 */
    for (i = 0; i < 8; i++) {
        b <<= 1;
        SCL_H(); dly();
        if (SDA_R() == Bit_SET) b |= 0x01;
        SCL_L(); dly();
    }

    /* 第 9 个时钟：主机发 ACK 或 NACK */
    if (ack) SDA_L(); else SDA_H();
    dly();
    SCL_H(); dly();
    SCL_L(); dly();
    SDA_H();                    /* 释放 SDA */

    return b;
}

/* ==================== 探测 ==================== */

uint8_t i2c_probe(uint8_t addr)
{
    uint8_t ack;

    i2c_start();
    ack = i2c_write_byte((uint8_t)(addr << 1));     /* 写方向 */
    i2c_stop();

    delay_us(20);
    return ack ? I2C_OK : I2C_ERR;
}

/* ==================== 写寄存器 ==================== */

uint8_t i2c_write_regs(uint8_t addr, uint8_t reg, const uint8_t *buf, uint16_t len)
{
    uint16_t i;
    uint8_t  ok;

    i2c_start();
    ok = i2c_write_byte((uint8_t)(addr << 1));      /* 地址 + 写 */
    if (!ok) { i2c_stop(); return I2C_ERR; }

    ok = i2c_write_byte(reg);                       /* 寄存器地址 */
    if (!ok) { i2c_stop(); return I2C_ERR; }

    for (i = 0; i < len; i++) {
        ok = i2c_write_byte(buf[i]);
        if (!ok) { i2c_stop(); return I2C_ERR; }
    }

    i2c_stop();
    return I2C_OK;
}

uint8_t i2c_write_reg(uint8_t addr, uint8_t reg, uint8_t val)
{
    return i2c_write_regs(addr, reg, &val, 1);
}

/* ==================== 读寄存器 ==================== */

uint8_t i2c_read_regs(uint8_t addr, uint8_t reg, uint8_t *buf, uint16_t len)
{
    uint16_t i;
    uint8_t  ok;

    if (len == 0) return I2C_ERR;

    /* --- 阶段 1：写寄存器地址 --- */
    i2c_start();
    ok = i2c_write_byte((uint8_t)(addr << 1));      /* 地址 + 写 */
    if (!ok) { i2c_stop(); return I2C_ERR; }

    ok = i2c_write_byte(reg);
    if (!ok) { i2c_stop(); return I2C_ERR; }

    /* --- 阶段 2：重复 START，切读方向 --- */
    i2c_start();
    ok = i2c_write_byte((uint8_t)((addr << 1) | 0x01));     /* 地址 + 读 */
    if (!ok) { i2c_stop(); return I2C_ERR; }

    /* --- 阶段 3：读数据；最后一个字节发 NACK --- */
    for (i = 0; i < len; i++) {
        buf[i] = i2c_read_byte((i == len - 1) ? 0 : 1);
    }

    i2c_stop();
    return I2C_OK;
}

uint8_t i2c_read_reg(uint8_t addr, uint8_t reg, uint8_t *val)
{
    return i2c_read_regs(addr, reg, val, 1);
}

/**
 * 直接连读（不先写寄存器地址）
 * ★ MAX30102 读 FIFO 时用
 */
uint8_t i2c_read_raw(uint8_t addr, uint8_t *buf, uint16_t len)
{
    uint16_t i;
    uint8_t  ok;

    if (len == 0) return I2C_ERR;

    i2c_start();
    ok = i2c_write_byte((uint8_t)((addr << 1) | 0x01));     /* 地址 + 读 */
    if (!ok) { i2c_stop(); return I2C_ERR; }

    for (i = 0; i < len; i++) {
        buf[i] = i2c_read_byte((i == len - 1) ? 0 : 1);
    }

    i2c_stop();
    return I2C_OK;
}

/**
 ******************************************************************************
 *  i2c.h —— I2C 驱动（★ 软件 I2C，GPIO 模拟）
 ******************************************************************************
 *  ★★ 2026-10-06 重大更改：
 *      原来用【硬件 I2C】（STM32F103 的 I2C1 外设），扫不到任何设备。
 *      改用【软件 I2C】（GPIO 手动时序）并【放慢速率】后，
 *      实测能扫到 OLED 的 0x3C！（用 16_i2c_scope 验证的）
 *
 *      原因推断：本套硬件（面包板 + 杜邦线）分布电容较大，
 *                I2C 速率快时上升沿来不及建立，从机采样错误。
 *                慢速（约 25kHz）时完全正常。
 *
 *  ★ 引脚：PB6 = SCL，PB7 = SDA（都在 _common/bsp.h 里定义）
 *  ★ 接口和原来【完全一样】，所有工程不用改代码
 ******************************************************************************
 */
#ifndef __I2C_H
#define __I2C_H

#include "bsp.h"

/* ★ 速率：每半周期延时多少微秒
     20 = 约 25kHz（★ 实测可用，推荐）
     10 = 约 50kHz
      5 = 约 100kHz（★ 本套硬件上扫不到，别用） */
#define I2C_DELAY_US    20

void    i2c_init(void);
void    i2c_reset(void);

uint8_t i2c_probe(uint8_t addr);

uint8_t i2c_write_reg(uint8_t addr, uint8_t reg, uint8_t val);
uint8_t i2c_write_regs(uint8_t addr, uint8_t reg, const uint8_t *buf, uint16_t len);
uint8_t i2c_read_reg(uint8_t addr, uint8_t reg, uint8_t *val);
uint8_t i2c_read_regs(uint8_t addr, uint8_t reg, uint8_t *buf, uint16_t len);
uint8_t i2c_read_raw(uint8_t addr, uint8_t *buf, uint16_t len);

#define I2C_OK      0
#define I2C_ERR     1

#endif /* __I2C_H */

/**
 ******************************************************************************
 *  mpu6050.h —— MPU6050 六轴传感器驱动 + 计步算法
 ******************************************************************************
 */
#ifndef __MPU6050_H
#define __MPU6050_H

#include "bsp.h"

#define MPU6050_ADDR    0x68        /* ★ AD0 接 GND 时是 0x68 */

/* ---- 基础读写 ---- */
uint8_t mpu6050_init(void);
uint8_t mpu6050_read_raw(int16_t *ax, int16_t *ay, int16_t *az,
                         int16_t *gx, int16_t *gy, int16_t *gz);

/* 加速度转成 g（×1000 的整数，如 1000 = 1.000g） */
void    mpu6050_accel_g(int16_t ax, int16_t ay, int16_t az,
                        int32_t *gx, int32_t *gy, int32_t *gz);

/* ---- 姿态角（简化版，只用加速度算倾角，单位 0.1°） ---- */
void    mpu6050_angle(int16_t ax, int16_t ay, int16_t az,
                      int16_t *pitch10, int16_t *roll10);

/* ---- 计步 ---- */
void     pedometer_init(void);
uint32_t pedometer_update(void);        /* ★ 周期调用（建议 20~50ms），返回累计步数 */
uint32_t pedometer_get(void);
void     pedometer_reset(void);

#endif /* __MPU6050_H */

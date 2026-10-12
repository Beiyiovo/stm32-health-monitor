/**
 ******************************************************************************
 *  mpu6050.c —— MPU6050 六轴传感器驱动 + 计步算法
 ******************************************************************************
 *  接线：SCL → PB6，SDA → PB7，AD0 → GND（地址 0x68）
 *
 *  计步原理（简化但实用）：
 *      1. 读三轴加速度，算合加速度 magnitude = √(ax²+ay²+az²)
 *      2. 减去重力分量 → 得到"动态加速度"
 *      3. 用一个滑动平均跟踪动态加速度的幅度
 *      4. 幅度上穿【动态阈值】时算一步
 *      5. 加"不应期"（280ms），防止一步被数成多步
 ******************************************************************************
 */
#include "mpu6050.h"
#include "i2c.h"
#include <math.h>

/* ==================== 初始化 ==================== */

uint8_t mpu6050_init(void)
{
    uint8_t who = 0;

    /* 探测器件 */
    if (i2c_probe(MPU6050_ADDR) != I2C_OK) return 0;

    /* 读 WHO_AM_I（0x75），正常应返回 0x68 */
    i2c_read_reg(MPU6050_ADDR, 0x75, &who);

    /* 复位 */
    i2c_write_reg(MPU6050_ADDR, 0x6B, 0x80);
    delay_ms(100);

    /* 唤醒 + 选择 PLL（用陀螺仪 X 轴做时钟源，更稳） */
    i2c_write_reg(MPU6050_ADDR, 0x6B, 0x01);
    delay_ms(50);

    /* 采样率分频：1kHz / (1+4) = 200Hz */
    i2c_write_reg(MPU6050_ADDR, 0x19, 0x04);

    /* 数字低通滤波：带宽 20Hz（★ 计步用，滤掉高频抖动） */
    i2c_write_reg(MPU6050_ADDR, 0x1A, 0x04);

    /* 陀螺仪量程 ±2000°/s */
    i2c_write_reg(MPU6050_ADDR, 0x1B, 0x18);

    /* ★ 加速度量程 ±2g（计步用，分辨率最高：16384 LSB/g） */
    i2c_write_reg(MPU6050_ADDR, 0x1C, 0x00);

    /* 关中断 */
    i2c_write_reg(MPU6050_ADDR, 0x38, 0x00);

    /* 用户控制：不用 FIFO，不用 I2C 主模式 */
    i2c_write_reg(MPU6050_ADDR, 0x6A, 0x00);

    delay_ms(50);
    return 1;
}

/* ==================== 读原始数据 ==================== */

uint8_t mpu6050_read_raw(int16_t *ax, int16_t *ay, int16_t *az,
                         int16_t *gx, int16_t *gy, int16_t *gz)
{
    uint8_t buf[14];

    /* 从 0x3B 开始连读 14 字节：加速度(6) + 温度(2) + 陀螺仪(6) */
    if (i2c_read_regs(MPU6050_ADDR, 0x3B, buf, 14) != I2C_OK) return 0;

    if (ax) *ax = (int16_t)((buf[0]  << 8) | buf[1]);
    if (ay) *ay = (int16_t)((buf[2]  << 8) | buf[3]);
    if (az) *az = (int16_t)((buf[4]  << 8) | buf[5]);
    /* buf[6],buf[7] 是温度，本驱动不用 */
    if (gx) *gx = (int16_t)((buf[8]  << 8) | buf[9]);
    if (gy) *gy = (int16_t)((buf[10] << 8) | buf[11]);
    if (gz) *gz = (int16_t)((buf[12] << 8) | buf[13]);

    return 1;
}

/* ==================== 单位换算 ==================== */

void mpu6050_accel_g(int16_t ax, int16_t ay, int16_t az,
                     int32_t *gx, int32_t *gy, int32_t *gz)
{
    /* ±2g 量程：16384 LSB = 1g → 结果 ×1000 */
    if (gx) *gx = ((int32_t)ax * 1000) / 16384;
    if (gy) *gy = ((int32_t)ay * 1000) / 16384;
    if (gz) *gz = ((int32_t)az * 1000) / 16384;
}

void mpu6050_angle(int16_t ax, int16_t ay, int16_t az,
                   int16_t *pitch10, int16_t *roll10)
{
    /* ★ 用浮点算角（只在显示时调用，不影响计步性能） */
    float fx = (float)ax, fy = (float)ay, fz = (float)az;
    float p, r;

    p = atan2f(-fx, sqrtf(fy * fy + fz * fz)) * 57.29578f;   /* pitch */
    r = atan2f(fy, fz) * 57.29578f;                            /* roll  */

    if (pitch10) *pitch10 = (int16_t)(p * 10.0f);
    if (roll10)  *roll10  = (int16_t)(r * 10.0f);
}

/* ==================== 计步算法 ==================== */

#define PED_SAMPLE_MS       25          /* 调用周期，要和主程序一致 */
#define PED_REFRACTORY      280         /* 不应期 ms（一步最短间隔） */
#define PED_THRESHOLD       180         /* ★ 动态加速度阈值（mg），越大越难触发 */
#define PED_AVG_SHIFT       4           /* 滑动平均系数：1/16 */

static uint32_t s_steps = 0;            /* 累计步数 */
static int32_t  s_avg   = 0;            /* 动态加速度的滑动平均（mg） */
static int32_t  s_prev  = 0;            /* 上一次的动态加速度 */
static uint32_t s_last_ms = 0;          /* 上次计步的时刻 */
static uint32_t s_ms    = 0;            /* 内部毫秒计数 */
static uint8_t  s_armed = 1;            /* 峰值门控：已越过谷底，可计下一步 */

void pedometer_init(void)
{
    s_steps    = 0;
    s_avg      = 0;
    s_prev     = 0;
    s_last_ms  = 0;
    s_ms       = 0;
    s_armed    = 1;
}

uint32_t pedometer_update(void)
{
    int16_t ax, ay, az;
    int32_t mag, dyn, delta;

    /* 1. 读加速度 */
    if (!mpu6050_read_raw(&ax, &ay, &az, NULL, NULL, NULL)) return s_steps;

    /* 2. 合加速度（mg） */
    mag = ((int32_t)ax * 1000) / 16384;
    mag = mag * mag;
    {
        int32_t t = ((int32_t)ay * 1000) / 16384;
        mag += t * t;
        t = ((int32_t)az * 1000) / 16384;
        mag += t * t;
    }
    /* 整数开方（牛顿迭代，够快够用） */
    {
        int32_t x = mag, y = 1, k = 0;
        if (x <= 0) { mag = 0; }
        else {
            while (x > 4096) { x >>= 2; k++; }          /* 缩小范围 */
            y = x;
            y = (y + x / y) / 2;
            y = (y + x / y) / 2;
            y = (y + x / y) / 2;
            while (k--) y <<= 1;                        /* 放大回去 */
            mag = y;
        }
    }

    /* 3. 减去重力（1g = 1000mg）→ 动态加速度 */
    dyn = mag - 1000;
    if (dyn < 0) dyn = -dyn;

    /* 4. 滑动平均跟踪动态幅度 */
    s_avg += (dyn - s_avg) >> PED_AVG_SHIFT;

    /* 5. 时间推进 */
    s_ms += PED_SAMPLE_MS;

    /* 6. 峰值检测：上穿阈值 + 过了不应期 */
    delta = dyn - s_avg;

    if (delta > PED_THRESHOLD && s_prev <= PED_THRESHOLD && s_armed) {
        if ((s_ms - s_last_ms) >= PED_REFRACTORY) {
            s_steps++;
            s_last_ms = s_ms;
            s_armed = 0;                    /* 等回落到谷底再允许下一步 */
        }
    }

    /* 回落到负阈值以下 → 重新武装 */
    if (delta < -(PED_THRESHOLD / 2)) {
        s_armed = 1;
    }

    s_prev = delta;
    return s_steps;
}

uint32_t pedometer_get(void)   { return s_steps; }
void     pedometer_reset(void) { pedometer_init(); }

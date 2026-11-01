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

/* ==================== 32 位整数开方（逐位逼近法）====================
 *
 * ★★★ 2026-10-15 新增：替换掉原来错误的牛顿迭代实现
 *
 * 【为什么必须要换】
 *   原来的实现只迭代 3 次、初值 y=x 又选得太小，远未收敛：
 *     输入 1000000（对应静止 1g）→ 正确值 1000，算出来 7840（★ 错 7.84 倍）
 *     输入 10000                 → 正确值 100，  算出来 628 （错 6.28 倍）
 *     输入 1000                  → 正确值 31，   算出来 127 （错 4.10 倍）
 *
 *   后果（实测现象：板子放桌上不动，步数每秒 +1）：
 *     · 静止时 mag 算成 ≈7840 而不是 1000
 *     · dyn = |mag − 1000| ≈ 6840（本应 ≈ 0）
 *     · ★ 加速度 ±0.5% 的噪声被放大成 dyn 波动 ≈160mg
 *     · 计步阈值 PED_THRESHOLD = 180mg → 噪声几乎顶到阈值
 *     · 于是静止时 delta 反复穿越 ±阈值 → 一直误计步
 *
 * 【本算法的特点】
 *   · 纯整数运算（无浮点、无除法）→ 在 M3 上跑很快
 *   · 结果精确（Python 20 万次随机测试：零误差）
 *   · 静止时 dyn ≈ 0~6mg，比原来低 26 倍 → 噪声远离阈值
 *
 * 【原理】逐位确定结果的每一位：从最高位（2^15）开始试，
 *         能减就减、该位记 1，减不动就把该位清 0，然后降到下一位。
 * ==================================================================== */
static uint32_t isqrt_u32(uint32_t n)
{
    uint32_t res = 0;
    uint32_t bit = 1u << 30;        /* 不大于 n 的最大的 4 的幂 */

    while (bit > n) bit >>= 2;

    while (bit != 0) {
        if (n >= res + bit) {
            n   -= res + bit;
            res  = (res >> 1) + bit;
        } else {
            res >>= 1;
        }
        bit >>= 2;
    }
    return res;
}

/* ==================== 计步算法 ==================== */

#define PED_SAMPLE_MS       25          /* 调用周期，要和主程序一致 */
#define PED_REFRACTORY      280         /* 不应期 ms（一步最短间隔） */
#define PED_THRESHOLD       180         /* ★ 动态加速度阈值（mg），越大越难触发 */
#define PED_AVG_SHIFT       4           /* 滑动平均系数：1/16 */

static uint32_t s_steps = 0;            /* 累计步数 */
static int32_t  s_avg   = 0;            /* 动态加速度的滑动平均（mg） */
static int32_t  s_prev  = 0;            /* 上一次的动态加速度 */
static int32_t  s_last_activity = 0;    /* ★ 2026-10-23：最近一次动态加速度（mg），供睡眠检测 */

static uint32_t s_last_step_ms = 0;     /* ★ 上次计步的墙钟时刻（millis）*/
static uint32_t s_ms    = 0;            /* 内部毫秒计数 */
static uint8_t  s_armed = 1;            /* 峰值门控：已越过谷底，可计下一步 */

void pedometer_init(void)
{
    s_steps    = 0;
    s_avg      = 0;
    s_prev     = 0;
    s_last_step_ms = 0;
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
    /* ★★★ 整数开方（逐位逼近法，精确）
     *   ★ 2026-10-15 修复：原来用的牛顿迭代只做 3 次、初值太小，
     *     结果错 4~8 倍，导致静止时 dyn≈6840（应为 0）、
     *     噪声被放大 8 倍逼近阈值 → 步数一直乱加。
     *     详见 isqrt_u32() 上方的说明。*/
    mag = (int32_t)isqrt_u32((uint32_t)((mag > 0) ? mag : 0));

    /* 3. 减去重力（1g = 1000mg）→ 动态加速度 */
    dyn = mag - 1000;
    if (dyn < 0) dyn = -dyn;

    s_last_activity = dyn;              /* ★ 2026-10-23 保存给睡眠检测用 */

    /* 4. 滑动平均跟踪动态幅度 */
    s_avg += (dyn - s_avg) >> PED_AVG_SHIFT;

    /* 5. 峰值检测：上穿阈值 + 过了不应期
     *
     * ★★★ 2026-10-15 修复：不应期改用【真实墙钟时间 millis()】
     *
     * 【原 bug】原来用 s_ms += PED_SAMPLE_MS(25) 自累加，假设函数每 25ms 被调一次。
     *   但实际调用周期 = 主循环周期 ≈ 45ms（本项目自己的注释 bsp.c/main.c 都写 45ms），
     *   → 内部时间比真实时间【慢约 1.8 倍】
     *   → 不应期 280"内部毫秒"实际是约 500ms
     *   → ★ 快走（100~120 步/分）时直接漏计步
     *
     * 【修法】直接用 millis() 差值，与实际调用周期完全解耦。*/
    delta = dyn - s_avg;

    if (delta > PED_THRESHOLD && s_prev <= PED_THRESHOLD && s_armed) {
        if ((millis() - s_last_step_ms) >= PED_REFRACTORY) {
            s_steps++;
            s_last_step_ms = millis();
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

/* ★ 2026-10-23 新增：给睡眠检测用 */
int32_t mpu6050_get_activity(void) { return s_last_activity; }
void     pedometer_reset(void) { pedometer_init(); }

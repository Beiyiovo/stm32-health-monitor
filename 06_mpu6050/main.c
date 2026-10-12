/**
 ******************************************************************************
 *  06_mpu6050 —— 步骤 6：MPU6050 加速度 + 计步验证
 ******************************************************************************
 *  目的：
 *      1. 确认能读到三轴加速度（静止时 Z 轴约 1g）
 *      2. 验证计步算法（拿起模块晃动，看步数增加）
 *
 *  接线：MPU6050 VCC→3.3  GND→G  SCL→B6  SDA→B7  AD0→G（地址 0x68）
 ******************************************************************************
 */
#include "bsp.h"
#include "i2c.h"
#include "mpu6050.h"

int main(void)
{
    int16_t  ax, ay, az, gx, gy, gz;
    int32_t  fax, fay, faz;
    int16_t  pitch10, roll10;
    uint32_t steps = 0;
    uint32_t n = 0, i;
    uint32_t last_print = 0;

    bsp_init();
    uart1_init();
    i2c_init();

    printf("\r\n\r\n========================================\r\n");
    printf("  501 个人健康助手 —— 步骤 6：MPU6050 + 计步\r\n");
    printf("========================================\r\n\r\n");
    printf("接线：VCC→3.3  GND→G  SCL→B6  SDA→B7  AD0→G\r\n\r\n");

    if (!mpu6050_init()) {
        printf("X 找不到 MPU6050（0x68）！\r\n");
        printf("  检查：\r\n");
        printf("    1. VCC→3.3  GND→G  SCL→B6  SDA→B7\r\n");
        printf("    2. ★ AD0 必须接 GND（不接的话地址是 0x69）\r\n");
        printf("    3. 用 03_i2c_scan 扫一下，看 0x68 在不在\r\n");
        printf("\r\n  程序停在这，接好线按复位重启。\r\n");
        while (1) {
            led_onboard(1); delay_ms(150);
            led_onboard(0); delay_ms(150);
        }
    }
    printf("√ MPU6050 初始化成功\r\n\r\n");

    pedometer_init();

    printf("开始采集（每 25ms 一次，每 500ms 打印一行）\r\n");
    printf("★ 静止时 Z 轴应约 1000mg；拿起模块晃动，看步数增加\r\n\r\n");
    printf("  序号 |   ax    ay    az  (mg) |  pitch  roll |  步数\r\n");
    printf("  -----+----------------------+--------------+-------\r\n");

    while (1) {
        /* 读原始数据 */
        if (!mpu6050_read_raw(&ax, &ay, &az, &gx, &gy, &gz)) {
            printf("  读取失败\r\n");
            delay_ms(25);
            continue;
        }

        /* 更新计步 */
        steps = pedometer_update();
        n++;

        /* 每 500ms（20 次）打印一行 */
        if ((n - last_print) >= 20) {
            last_print = n;

            mpu6050_accel_g(ax, ay, az, &fax, &fay, &faz);
            mpu6050_angle(ax, ay, az, &pitch10, &roll10);

            printf("  %4u | %5d %5d %5d | %5d %5d | %5u\r\n",
                   (unsigned)n, (int)fax, (int)fay, (int)faz,
                   (int)pitch10, (int)roll10, (unsigned)steps);
        }

        /* ★ 固定 25ms 周期 —— 计步算法依赖这个周期 */
        for (i = 0; i < 25; i++) delay_ms(1);

        led_onboard((n % 40) < 2 ? 1 : 0);
    }
}

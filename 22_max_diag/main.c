/**
 ******************************************************************************
 *  22_max_diag —— ★★★ MAX30102 专项诊断（全部结果打印到串口）
 ******************************************************************************
 *  目的：一次定位 MAX30102 到底是"没采样"还是"采样了但读取有问题"
 *
 *  它会做 6 步检查：
 *      ① I2C 探测 0x57
 *      ② 读 PART_ID / REV_ID（判断芯片真假）
 *      ③ ★ 写配置后【读回验证】—— 这一步能发现 I2C 写入是否真的生效
 *      ④ 读 FIFO 指针，看写指针有没有在动（有没有在采样）
 *      ⑤ 连续 10 秒读 FIFO 原始值（IR/RED）
 *      ⑥ 给出结论
 *
 *  接线：
 *      MAX30102 下排 4 脚：VIN→3.3V、SDA→B7、SCL→B6、GND→G
 *      ★ 上排 4 脚全部不接
 *      ★ 两个 4.7kΩ 上拉：B6↔3.3、B7↔3.3
 *      USB-TTL：TXD→A10、RXD→A9、GND→G
 ******************************************************************************
 */
#include "bsp.h"
#include "i2c.h"

#define ADDR        0x57

/* 寄存器 */
#define R_INT_ST1   0x00
#define R_INT_ST2   0x01
#define R_INT_EN1   0x02
#define R_INT_EN2   0x03
#define R_FIFO_WR   0x04
#define R_OVF_CNT   0x05
#define R_FIFO_RD   0x06
#define R_FIFO_DATA 0x07
#define R_FIFO_CFG  0x08
#define R_MODE_CFG  0x09
#define R_SPO2_CFG  0x0A
#define R_LED1_PA   0x0C
#define R_LED2_PA   0x0D
#define R_TEMP_INT  0x1F
#define R_TEMP_FRAC 0x20
#define R_REV_ID    0xFE
#define R_PART_ID   0xFF

static uint8_t rd(uint8_t reg)
{
    uint8_t v = 0xFF;
    if (i2c_read_reg(ADDR, reg, &v) != I2C_OK) return 0xFF;
    return v;
}

static void wr(uint8_t reg, uint8_t val)
{
    (void)i2c_write_reg(ADDR, reg, val);
}

/* ★ 写一个寄存器，然后读回来验证 */
static void wr_verify(const char *name, uint8_t reg, uint8_t val)
{
    uint8_t back;

    wr(reg, val);
    delay_ms(5);
    back = rd(reg);

    printf("    %-10s 写 0x%02X -> 读回 0x%02X   %s\r\n",
           name, val, back,
           (back == val) ? "√ 一致" : "★ 不一致！I2C 写入没生效");
}

int main(void)
{
    uint8_t  pid, rev;
    uint8_t  wr_p, rd_p, mode, spo2, led1, led2, fifocfg;
    uint32_t n = 0;
    uint8_t  i;
    uint32_t last_wr = 0xFFFFFFFF;
    uint32_t samples_seen = 0;
    uint32_t last_ir = 0, last_red = 0;
    uint8_t  first_wr = 0;

    bsp_init();
    uart1_init();
    i2c_init();

    printf("\r\n\r\n");
    printf("================================================\r\n");
    printf("  501 毕设 —— ★ MAX30102 专项诊断\r\n");
    printf("================================================\r\n");
    printf("  接线：VIN->3.3  SDA->B7  SCL->B6  GND->G\r\n");
    printf("        ★ 上排 4 脚不接\r\n");
    printf("        ★ 4.7kohm x2: B6<->3.3, B7<->3.3\r\n");
    printf("================================================\r\n\r\n");

    delay_ms(500);

    /* ==================== ① I2C 探测 ==================== */
    printf("【1】I2C 探测 0x57 ... ");
    if (i2c_probe(ADDR) != I2C_OK) {
        printf("★ 无应答！\r\n");
        printf("     检查：VIN=3.3V？SDA->B7？SCL->B6？GND->G？\r\n");
        printf("     ★ 注意脚序是 VIN-SDA-SCL-GND（SDA 在 SCL 前面）\r\n");
        goto loop_forever;
    }
    printf("√ 有应答\r\n\r\n");

    /* ==================== ② 芯片 ID ==================== */
    pid = rd(R_PART_ID);
    rev = rd(R_REV_ID);
    printf("【2】芯片识别\r\n");
    printf("    PART_ID (0xFF) = 0x%02X   %s\r\n", pid,
           (pid == 0x15) ? "√ 正品 MAX30102" : "★ 不是 0x15（兼容片或假货）");
    printf("    REV_ID  (0xFE) = 0x%02X\r\n\r\n", rev);

    /* ==================== ③ 写配置 + 读回验证 ==================== */
    printf("【3】写配置并读回验证（★ 关键步骤）\r\n");

    /* 软件复位 */
    wr(R_MODE_CFG, 0x40);
    delay_ms(100);

    wr_verify("INT_EN1",  R_INT_EN1,  0x00);
    wr_verify("INT_EN2",  R_INT_EN2,  0x00);
    wr_verify("FIFO_WR",  R_FIFO_WR,  0x00);
    wr_verify("OVF_CNT",  R_OVF_CNT,  0x00);
    wr_verify("FIFO_RD",  R_FIFO_RD,  0x00);
    wr_verify("FIFO_CFG", R_FIFO_CFG, 0x4F);
    wr_verify("SPO2_CFG", R_SPO2_CFG, 0x27);
    wr_verify("LED1_PA",  R_LED1_PA,  0x24);
    wr_verify("LED2_PA",  R_LED2_PA,  0x24);
    /* ★ MODE_CFG 放最后写（它一写就开始采样）*/
    wr_verify("MODE_CFG", R_MODE_CFG, 0x03);

    delay_ms(200);
    printf("\r\n");

    mode    = rd(R_MODE_CFG);
    spo2    = rd(R_SPO2_CFG);
    led1    = rd(R_LED1_PA);
    led2    = rd(R_LED2_PA);
    fifocfg = rd(R_FIFO_CFG);

    printf("    读回汇总：MODE=0x%02X SPO2=0x%02X LED1=0x%02X LED2=0x%02X FIFO=0x%02X\r\n\r\n",
           mode, spo2, led1, led2, fifocfg);

    /* ==================== ④ 看 FIFO 写指针有没有动 ==================== */
    printf("【4】等 2 秒，看 FIFO 写指针动不动（动 = 传感器在采样）\r\n");
    first_wr = rd(R_FIFO_WR);
    printf("    第 1 次读 FIFO_WR = 0x%02X (%u)\r\n", first_wr, first_wr);

    for (i = 0; i < 20; i++) {
        delay_ms(100);
        wr_p = rd(R_FIFO_WR);
        if (i % 5 == 0) printf("    %u.%us  FIFO_WR = 0x%02X (%u)\r\n",
                               (i + 1) / 10, (i + 1) % 10, wr_p, wr_p);
    }

    wr_p = rd(R_FIFO_WR);
    printf("    第 2 次读 FIFO_WR = 0x%02X (%u)\r\n", wr_p, wr_p);

    if (wr_p == first_wr) {
        printf("    ★★ FIFO_WR 没变！传感器【没有在采样】\r\n");
        printf("       可能原因：\r\n");
        printf("         · MODE_CFG 写入没生效（看上面第 3 步的读回值）\r\n");
        printf("         · 芯片是兼容片，寄存器定义不同\r\n");
        printf("         · 传感器损坏\r\n");
        printf("         · ★ VIN 电压不足（要用 3.3V，且电流要够）\r\n");
    } else {
        printf("    √ FIFO_WR 在变！传感器【正在采样】\r\n");
    }
    printf("\r\n");

    /* ==================== ⑤ 连续读原始值 ==================== */
    printf("【5】连续 10 秒读 FIFO 原始值（★ 手指轻贴在玻璃窗上）\r\n");
    printf("    时间   写指针  样本数   IR 原始值    RED 原始值\r\n");
    printf("    -------------------------------------------------\r\n");

    for (n = 1; n <= 20; n++) {
        uint8_t b[6];
        uint32_t red, ir;

        delay_ms(500);

        wr_p = rd(R_FIFO_WR);
        rd_p = rd(R_FIFO_RD);

        /* 读一组样本（RED[3] + IR[3]，各 18 位）*/
        if (i2c_read_regs(ADDR, R_FIFO_DATA, b, 6) == I2C_OK) {
            red = (((uint32_t)b[0] << 16) | ((uint32_t)b[1] << 8) | b[2]) & 0x3FFFF;
            ir  = (((uint32_t)b[3] << 16) | ((uint32_t)b[4] << 8) | b[5]) & 0x3FFFF;
            last_ir = ir; last_red = red;
            if (ir > 1000) samples_seen++;
        } else {
            ir = 0; red = 0;
        }

        printf("    %2u.%us   0x%02X    %2u     %8lu    %8lu\r\n",
               (unsigned)(n / 2), (unsigned)((n % 2) ? 5 : 0),
               wr_p, (unsigned)((wr_p - rd_p) & 0x1F),
               (unsigned long)ir, (unsigned long)red);

        if (wr_p != last_wr) last_wr = wr_p;
    }

    printf("\r\n");

    /* ==================== ⑥ 结论 ==================== */
    printf("================================================\r\n");
    printf("  诊 断 结 论\r\n");
    printf("================================================\r\n");

    if (pid != 0x15) {
        printf("  ★ PART_ID = 0x%02X（不是 0x15）\r\n", pid);
        printf("     -> 芯片可能不是原厂 MAX30102，寄存器定义可能不同\r\n");
    }

    if (mode != 0x03) {
        printf("  ★ MODE_CFG 读回 0x%02X（期望 0x03）\r\n", mode);
        printf("     -> 配置没写进去，传感器不会采样\r\n");
    } else {
        printf("  √ MODE_CFG = 0x03，配置正确\r\n");
    }

    if (last_wr == first_wr && samples_seen == 0) {
        printf("  ★★ 传感器完全没有输出数据\r\n");
        printf("     最可能：\r\n");
        printf("       1. 模块 VIN 供电不足（量一下 VIN 对 GND 是不是 3.3V）\r\n");
        printf("       2. 芯片是兼容片/假货\r\n");
        printf("       3. 模块损坏 -> 换一个模块试\r\n");
    } else if (last_ir < 1000) {
        printf("  ★ 有采样但 IR 值很低（%lu）\r\n", (unsigned long)last_ir);
        printf("     -> 传感器在工作，但没接到反射光\r\n");
        printf("     -> ★ 手指要完全盖住玻璃窗，轻轻贴住别用力压\r\n");
    } else {
        printf("  √√√ IR=%lu RED=%lu，传感器工作正常！\r\n",
               (unsigned long)last_ir, (unsigned long)last_red);
        printf("     -> 可以烧 09_main 测心率和血氧了\r\n");
    }

    printf("\r\n  10 秒后重来一轮...\r\n\r\n");

loop_forever:
    while (1) {
        led_onboard(1); delay_ms(50);
        led_onboard(0); delay_ms(950);
    }
}

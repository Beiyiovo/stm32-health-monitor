/**
 ******************************************************************************
 *  16_i2c_scope —— ★ 软件示波器式 I2C 诊断
 ******************************************************************************
 *  这个程序把 I2C 通信的【每一位电平】都打印出来，
 *  像逻辑分析仪一样，能看出到底卡在哪一步。
 *
 *  它会：
 *      ① 打印空闲电平
 *      ② 产生 START，打印 SDA/SCL 的实时电平
 *      ③ 逐位发地址 0x3C<<1，每一位都打印
 *      ④ 读第 9 位（ACK），打印从机有没有拉低 SDA
 *      ⑤ 产生 STOP
 *
 *  ★ 关键看第 ④ 步：
 *       如果 ACK 位读到 SDA=0  → ★★★ 从机应答了！模块是好的！
 *       如果 ACK 位读到 SDA=1  → 从机没应答（模块没工作 / 线没通）
 *
 *  接线：模块 SCL→STM32 的 B6，SDA→B7，GND→G，VCC→3.3
 *        ★ 两个 4.7kΩ：B6↔3.3、B7↔3.3
 *        USB-TTL：TXD→A10、RXD→A9、GND→G
 ******************************************************************************
 */
#include "bsp.h"

#define SCL_PORT    GPIOB
#define SCL_PIN     GPIO_Pin_6
#define SDA_PORT    GPIOB
#define SDA_PIN     GPIO_Pin_7

#define DLY_US      20      /* 每一步的延时，约 25kHz，很慢很稳 */

/* ==================== 引脚操作 ==================== */

static void pins_od(void)
{
    GPIO_InitTypeDef g;
    g.GPIO_Pin   = SCL_PIN | SDA_PIN;
    g.GPIO_Mode  = GPIO_Mode_Out_OD;    /* ★ 开漏输出（I2C 标准） */
    g.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOB, &g);
}

#define SCL_H()     GPIO_SetBits(SCL_PORT, SCL_PIN)
#define SCL_L()     GPIO_ResetBits(SCL_PORT, SCL_PIN)
#define SDA_H()     GPIO_SetBits(SDA_PORT, SDA_PIN)
#define SDA_L()     GPIO_ResetBits(SDA_PORT, SDA_PIN)

/* ★ 读电平：开漏输出模式下 IDR 仍反映引脚真实电平，直接读 */
#define SCL_R()     GPIO_ReadInputDataBit(SCL_PORT, SCL_PIN)
#define SDA_R()     GPIO_ReadInputDataBit(SDA_PORT, SDA_PIN)

static void dly(void) { delay_us(DLY_US); }

/* ==================== 通信测试 ==================== */

static void show_state(const char *tag)
{
    printf("      %-28s SCL=%u  SDA=%u\r\n", tag, SCL_R(), SDA_R());
}

/* 产生 START，逐步打印 */
static void do_start(void)
{
    printf("\r\n    --- START 时序 ---\r\n");

    SDA_H(); SCL_H(); dly();
    show_state("1. 两根都释放（空闲）");

    SDA_L(); dly();
    show_state("2. SDA 拉低（SCL 还高）");

    SCL_L(); dly();
    show_state("3. SCL 拉低（START 完成）");
}

/* 发一个字节，逐位打印 */
static uint8_t send_byte(uint8_t b, const char *what)
{
    uint8_t i, ack;

    printf("\r\n    --- 发送 %s: 0x%02X ---\r\n", what, b);

    for (i = 0; i < 8; i++) {
        uint8_t bit = (b & 0x80) ? 1 : 0;

        if (bit) SDA_H(); else SDA_L();
        dly();
        printf("      第 %d 位 = %d  ->  SDA=%u\r\n", i + 1, bit, SDA_R());

        SCL_H(); dly();
        SCL_L(); dly();

        b <<= 1;
    }

    /* ★ 第 9 位：读 ACK */
    printf("\r\n    --- 读第 9 位（ACK）---\r\n");

    SDA_H(); dly();                 /* 释放 SDA，让从机驱动 */
    printf("      释放 SDA 后:        SDA=%u  %s\r\n", SDA_R(),
           SDA_R() ? "（正常的，上拉拉高）" : "★ 被拉住了");

    SCL_H(); dly();
    ack = (SDA_R() == Bit_RESET) ? 1 : 0;
    printf("      SCL 拉高后:         SDA=%u  <<<< ★★★ 这一位是 ACK\r\n", SDA_R());

    SCL_L(); dly();

    printf("      => ACK = %d  %s\r\n", ack,
           ack ? "★★★ 从机应答了！模块是好的！" : "从机没应答");

    return ack;
}

static void do_stop(void)
{
    printf("\r\n    --- STOP 时序 ---\r\n");

    SDA_L(); dly();
    SCL_H(); dly();
    show_state("1. SCL 拉高，SDA 还低");

    SDA_H(); dly();
    show_state("2. SDA 释放（STOP 完成）");
}

/* ==================== 主程序 ==================== */

int main(void)
{
    uint32_t round = 0;
    uint8_t  i;
    uint8_t  addrs[] = {0x3C, 0x3D, 0x57, 0x68};
    const char *names[] = {"OLED(主)", "OLED(备)", "MAX30102", "MPU6050"};
    uint8_t  k;

    bsp_init();
    uart1_init();

    printf("\r\n\r\n");
    printf("================================================\r\n");
    printf("  501 毕设 —— ★ 软件示波器式 I2C 诊断\r\n");
    printf("================================================\r\n");
    printf("  接线：模块 SCL->B6  SDA->B7  GND->G  VCC->3.3\r\n");
    printf("        ★ 两个 4.7kΩ：B6<->3.3、B7<->3.3\r\n");
    printf("        USB-TTL：TXD->A10、RXD->A9、GND->G\r\n");
    printf("================================================\r\n");

    pins_od();
    SCL_H(); SDA_H();

    while (1) {
        round++;
        printf("\r\n\r\n██████████ 第 %u 轮 ██████████\r\n", (unsigned)round);

        /* ---------- 先把引脚配成推挽，验证排针 ---------- */
        printf("\r\n【A】排针通断自检（推挽输出）\r\n");
        printf("----------------------------------------\r\n");
        {
            GPIO_InitTypeDef g;
            uint8_t v;

            g.GPIO_Pin = SCL_PIN; g.GPIO_Mode = GPIO_Mode_Out_PP;
            g.GPIO_Speed = GPIO_Speed_50MHz; GPIO_Init(GPIOB, &g);
            GPIO_ResetBits(SCL_PORT, SCL_PIN); delay_ms(50);
            v = GPIO_ReadInputDataBit(SCL_PORT, SCL_PIN);
            printf("    B6 推挽拉低 -> 读回 %u  %s\r\n", v, v == 0 ? "√" : "X ★异常");

            g.GPIO_Pin = SDA_PIN; GPIO_Init(GPIOB, &g);
            GPIO_ResetBits(SDA_PORT, SDA_PIN); delay_ms(50);
            v = GPIO_ReadInputDataBit(SDA_PORT, SDA_PIN);
            printf("    B7 推挽拉低 -> 读回 %u  %s\r\n", v, v == 0 ? "√" : "X ★异常");

            printf("\r\n    ★★★ 请现在用万用表量【STM32 排针】上的电压：\r\n");
            printf("         B6 排针 应该是 0V（因为程序把它拉低了）\r\n");
            printf("         B7 排针 应该是 0V\r\n");
            printf("         ★ 如果排针不是 0V -> 排针虚焊！换一块 STM32 板子\r\n");
            delay_ms(3000);
        }

        pins_od();
        SCL_H(); SDA_H();
        delay_ms(100);

        /* ---------- 空闲电平 ---------- */
        printf("\r\n【B】空闲电平（开漏模式，两根都释放）\r\n");
        printf("----------------------------------------\r\n");
        show_state("空闲时");

        if (SCL_R() == 0 || SDA_R() == 0) {
            printf("\r\n    ★★ 有引脚被拉低！上拉电阻没接好，或者线路短路\r\n");
        } else {
            printf("\r\n    √ 两根都是高电平，上拉正常\r\n");
        }

        /* ---------- 逐个地址做完整时序 ---------- */
        for (k = 0; k < 4; k++) {
            uint8_t ack1, ack2;

            printf("\r\n\r\n================================================\r\n");
            printf("【C】测试地址 0x%02X (%s)\r\n", addrs[k], names[k]);
            printf("================================================\r\n");

            do_start();
            ack1 = send_byte((uint8_t)(addrs[k] << 1), "地址+写位");
            ack2 = send_byte(0x00, "控制字节");
            do_stop();

            printf("\r\n    ★★ 结论：地址 0x%02X ", addrs[k]);
            if (ack1) printf("有应答！★★★ 模块活着！\r\n");
            else      printf("无应答。\r\n");

            delay_ms(500);
        }

        /* ---------- 总结 ---------- */
        printf("\r\n\r\n================================================\r\n");
        printf("                    总 结\r\n");
        printf("================================================\r\n");
        printf("  ★ 请对照上面每个地址的\"第 9 位（ACK）\"那一行：\r\n\r\n");
        printf("  【情况 1】某个地址 ACK = 1（SDA 被从机拉低到 0）\r\n");
        printf("      → ★★★ 那个模块是好的！总线也是好的！\r\n");
        printf("      → 把是哪个地址告诉我，我改驱动\r\n\r\n");
        printf("  【情况 2】所有地址 ACK 都是 0，且 SDA 一直是 1\r\n");
        printf("      → 从机完全没响应。可能：\r\n");
        printf("        · 模块的 SCL/SDA/GND/VCC 有某根线没通\r\n");
        printf("        · ★ 用通断档量：STM32 的 B6 针 <-> 模块的 SCL 脚\r\n");
        printf("                         STM32 的 B7 针 <-> 模块的 SDA 脚\r\n");
        printf("                         STM32 的 G  针 <-> 模块的 GND 脚\r\n");
        printf("        · 模块坏了（虽然两个都坏概率低）\r\n\r\n");
        printf("  【情况 3】SDA 一直是 0（释放后也拉不高）\r\n");
        printf("      → ★ 有东西把 SDA 拉到地了。检查接线有没有短路\r\n\r\n");
        printf("  【情况 4】【A】那步排针量不到 0V\r\n");
        printf("      → ★★★ STM32 排针虚焊，换一块板子\r\n");
        printf("================================================\r\n");

        printf("\r\n  20 秒后重来...\r\n");
        for (i = 0; i < 200; i++) {
            led_onboard((i % 30) < 2 ? 1 : 0);
            delay_ms(100);
        }
    }
}

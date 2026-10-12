/**
 ******************************************************************************
 *  12_wire_test —— 接线通断测试（★ 配合万用表使用）
 ******************************************************************************
 *  原理：
 *      让 B6 和 B7 输出【慢速方波】（2 秒一个周期，慢到万用表能看清）
 *      你用万用表分别量【STM32 侧】和【OLED 侧】，看两边是否同步变化
 *
 *      ★ 两边一起变  = 这根线通了
 *      ★ OLED 侧不动 = 这根线断了（插错列 / 没插紧 / 面包板内部断）
 *
 *  测试流程（每 4 秒切换一个阶段，串口会打印当前该量哪里）：
 *      阶段 1：B6 拉高、B7 拉低   →  量 STM32 的 B6/B7，和 OLED 的 SCL/SDA
 *      阶段 2：B6 拉低、B7 拉高   →  再量一遍
 *      阶段 3：都拉低
 *      阶段 4：都释放（高）
 *
 *  ★ 接线：OLED GND→G  VCC→3.3  SCL→B6  SDA→B7
 ******************************************************************************
 */
#include "bsp.h"

#define SCL_PORT    GPIOB
#define SCL_PIN     GPIO_Pin_6          /* B6 */
#define SDA_PORT    GPIOB
#define SDA_PIN     GPIO_Pin_7          /* B7 */

/* 输出模式（推挽）—— 这样能真的拉低拉高 */
static void pins_output(void)
{
    GPIO_InitTypeDef g;
    g.GPIO_Pin   = SCL_PIN | SDA_PIN;
    g.GPIO_Mode  = GPIO_Mode_Out_PP;
    g.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOB, &g);
}

/* ★ 正确读电平的方法：保持输出模式，直接读 IDR
   （推挽输出时 IDR 仍反映引脚的真实电平） */
static uint8_t read_scl(void) { return GPIO_ReadInputDataBit(SCL_PORT, SCL_PIN); }
static uint8_t read_sda(void) { return GPIO_ReadInputDataBit(SDA_PORT, SDA_PIN); }

static void set_pins(uint8_t scl, uint8_t sda)
{
    if (scl) GPIO_SetBits(SCL_PORT, SCL_PIN);   else GPIO_ResetBits(SCL_PORT, SCL_PIN);
    if (sda) GPIO_SetBits(SDA_PORT, SDA_PIN);   else GPIO_ResetBits(SDA_PORT, SDA_PIN);
}

/* ==================== 测一次引脚能力（★ 修正版） ==================== */
static void test_pins_fixed(void)
{
    uint8_t v;

    printf("\r\n【引脚能力测试（修正版）】\r\n");
    printf("----------------------------------------\r\n");

    pins_output();

    /* B6 拉低 —— ★ 保持输出模式直接读，不改成输入 */
    GPIO_ResetBits(SCL_PORT, SCL_PIN);
    delay_ms(20);
    v = read_scl();
    printf("  B6 拉低后读回: %u  %s\r\n", v,
           v == 0 ? "√ 能拉低" : "X ★拉不低 —— 被外部强拉到 3.3V（短路？）");

    GPIO_SetBits(SCL_PORT, SCL_PIN);
    delay_ms(20);
    v = read_scl();
    printf("  B6 拉高后读回: %u  %s\r\n", v,
           v == 1 ? "√ 能拉高" : "X ★拉不高 —— 被外部强拉到 GND（短路？）");

    /* B7 */
    GPIO_ResetBits(SDA_PORT, SDA_PIN);
    delay_ms(20);
    v = read_sda();
    printf("  B7 拉低后读回: %u  %s\r\n", v,
           v == 0 ? "√ 能拉低" : "X ★拉不低 —— 被外部强拉到 3.3V（短路？）");

    GPIO_SetBits(SDA_PORT, SDA_PIN);
    delay_ms(20);
    v = read_sda();
    printf("  B7 拉高后读回: %u  %s\r\n", v,
           v == 1 ? "√ 能拉高" : "X ★拉不高 —— 被外部强拉到 GND（短路？）");
}

/* ==================== 主程序 ==================== */
int main(void)
{
    uint32_t phase = 0;
    uint8_t  i;

    bsp_init();
    uart1_init();

    printf("\r\n\r\n");
    printf("============================================\r\n");
    printf("  501 毕设 —— 接线通断测试（配合万用表）\r\n");
    printf("============================================\r\n");
    printf("  接线：OLED GND->G   VCC->3.3   SCL->B6   SDA->B7\r\n");
    printf("============================================\r\n");

    /* 先做修正版的引脚测试 */
    test_pins_fixed();

    printf("\r\n\r\n  ★★★ 接下来是重点：配合万用表 ★★★\r\n");
    printf("  程序每 4 秒切换一次引脚电平，你按提示量电压\r\n");
    printf("  万用表 -> 直流电压 20V 档，黑笔接 GND\r\n\r\n");

    while (1) {
        phase++;

        printf("\r\n██████ 阶段 %u ██████\r\n", (unsigned)phase);

        switch ((phase - 1) % 4) {

            case 0:
                printf("  B6 (SCL) = 高  3.3V      B7 (SDA) = 低  0V\r\n");
                printf("  ----\r\n");
                printf("  请量【STM32 侧】和【OLED 侧】，两边应该一样：\r\n");
                printf("     STM32 的 B6 针        -> 应读 3.3V\r\n");
                printf("     OLED  的 SCL 脚       -> ★ 也应读 3.3V（不一样=线断了）\r\n");
                printf("     STM32 的 B7 针        -> 应读 0V\r\n");
                printf("     OLED  的 SDA 脚       -> ★ 也应读 0V（不一样=线断了）\r\n");
                set_pins(1, 0);
                break;

            case 1:
                printf("  B6 (SCL) = 低  0V        B7 (SDA) = 高  3.3V\r\n");
                printf("  ----\r\n");
                printf("  请量（注意和上一阶段【反过来了】）：\r\n");
                printf("     STM32 的 B6 针        -> 应读 0V\r\n");
                printf("     OLED  的 SCL 脚       -> ★ 也应读 0V（不一样=线断了）\r\n");
                printf("     STM32 的 B7 针        -> 应读 3.3V\r\n");
                printf("     OLED  的 SDA 脚       -> ★ 也应读 3.3V（不一样=线断了）\r\n");
                printf("  ★★ 这一阶段最关键：如果 OLED 侧不跟着变，那根线就是断的\r\n");
                set_pins(0, 1);
                break;

            case 2:
                printf("  B6 = 低 0V   B7 = 低 0V\r\n");
                printf("  ----\r\n");
                printf("  两根都拉低。量【OLED 侧】：\r\n");
                printf("     OLED 的 SCL 脚  -> 应读 0V\r\n");
                printf("     OLED 的 SDA 脚  -> 应读 0V\r\n");
                printf("     ★ 如果读到 3.3V -> 那根线【没接到 STM32】\r\n");
                set_pins(0, 0);
                break;

            default:
                printf("  B6 = 高 3.3V   B7 = 高 3.3V（都释放）\r\n");
                printf("  ----\r\n");
                printf("  两根都拉高，此时量【OLED 侧的 GND 和 VCC】：\r\n");
                printf("     OLED 的 VCC 脚  -> 应读 3.3V\r\n");
                printf("     OLED 的 GND 脚  -> 应读 0V\r\n");
                printf("     ★ 如果 VCC 读不到 3.3V -> VCC 线断了\r\n");
                printf("     ★★ 如果 GND 读不到 0V  -> GND 线断了（模块不会工作！）\r\n");
                set_pins(1, 1);
                break;
        }

        /* 4 秒后切换（期间打印倒计时） */
        for (i = 0; i < 40; i++) {
            led_onboard((i % 10) < 1 ? 1 : 0);
            delay_ms(100);
        }
    }
}

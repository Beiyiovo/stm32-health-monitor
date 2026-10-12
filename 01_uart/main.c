/**
 ******************************************************************************
 *  501 毕业设计 —— 步骤 1：串口打印 + LED 心跳
 ******************************************************************************
 *  目标（阶段①验收标准）：
 *      烧进去后，电脑串口助手能看到打印 = STM32 开发环境完全打通
 *
 *  硬件：
 *      STM32F103C8T6 开发板
 *      USB-TTL（CH340）接 PA9 / PA10 / GND   ← 看下面的接线
 *
 *  接线：
 *      USB-TTL TXD  ->  STM32 PA10  (USART1_RX)
 *      USB-TTL RXD  ->  STM32 PA9   (USART1_TX)
 *      USB-TTL GND  ->  STM32 GND
 *      USB-TTL VCC  ->  不接
 *
 *      ※ 如果你的开发板自带 CH340（就是采购清单里要求的那个版本），
 *        直接用板载 USB 口连电脑即可，不用外接 USB-TTL。
 *
 *  串口设置：115200  8  1  None  无流控
 *
 *  ⚠️ Keil 里必须做的两个设置（不做会出问题）：
 *      1. Options for Target -> Target 标签 -> 勾选【Use MicroLIB】
 *         （不勾的话 printf 会卡在 semihosting，程序跑不起来）
 *      2. Options for Target -> Debug 标签 -> 选对你的下载器
 *         没有 ST-Link 就用串口下载：先把 BOOT0 跳线帽拨到 1，下载完拨回 0
 ******************************************************************************
 */

#include "stm32f10x.h"
#include <stdio.h>

/* ==================== 延时（粗略，够用） ==================== */
static void delay_ms(uint32_t ms)
{
    volatile uint32_t i;
    while (ms--) {
        for (i = 0; i < 7200; i++) { __NOP(); }
    }
}

/* ==================== LED（PC13，板载，低电平点亮） ==================== */
static void LED_Init(void)
{
    GPIO_InitTypeDef gpio;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOC, ENABLE);

    gpio.GPIO_Pin   = GPIO_Pin_13;
    gpio.GPIO_Mode  = GPIO_Mode_Out_PP;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOC, &gpio);

    GPIO_SetBits(GPIOC, GPIO_Pin_13);   /* 先灭掉 */
}

/* ==================== USART1（PA9=TX, PA10=RX, 115200） ==================== */
static void USART1_Init(void)
{
    GPIO_InitTypeDef  gpio;
    USART_InitTypeDef usart;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_USART1, ENABLE);

    /* PA9 = USART1_TX，复用推挽输出 */
    gpio.GPIO_Pin   = GPIO_Pin_9;
    gpio.GPIO_Mode  = GPIO_Mode_AF_PP;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &gpio);

    /* PA10 = USART1_RX，浮空输入 */
    gpio.GPIO_Pin  = GPIO_Pin_10;
    gpio.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_Init(GPIOA, &gpio);

    usart.USART_BaudRate            = 115200;
    usart.USART_WordLength          = USART_WordLength_8b;
    usart.USART_StopBits            = USART_StopBits_1;
    usart.USART_Parity              = USART_Parity_No;
    usart.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    usart.USART_Mode                = USART_Mode_Rx | USART_Mode_Tx;
    USART_Init(USART1, &usart);

    USART_Cmd(USART1, ENABLE);
}

/* ==================== printf 重定向到 USART1 ==================== */
int fputc(int ch, FILE *f)
{
    (void)f;
    USART_SendData(USART1, (uint8_t)ch);
    while (USART_GetFlagStatus(USART1, USART_FLAG_TC) == RESET) { }
    return ch;
}

/* ==================== 主程序 ==================== */
int main(void)
{
    uint32_t n = 0;

    LED_Init();
    USART1_Init();

    /* ---- 开机自检信息 ---- */
    printf("\r\n");
    printf("==========================================\r\n");
    printf("  501 个人健康助手 —— 步骤 1 环境验证\r\n");
    printf("==========================================\r\n");
    printf("  芯片型号   : STM32F103C8T6\r\n");
    printf("  系统主频   : %u Hz\r\n", (unsigned)SystemCoreClock);
    printf("  串口       : USART1 (PA9/PA10)\r\n");
    printf("  串口参数   : 115200 8 N 1\r\n");
    printf("  编译时间   : %s %s\r\n", __DATE__, __TIME__);
    printf("==========================================\r\n");
    printf("  >>> 看到这段文字 = 开发环境完全打通 <<<\r\n");
    printf("==========================================\r\n\r\n");

    /* ---- 心跳循环：每秒一次，LED 同步闪烁 ---- */
    while (1) {
        n++;
        GPIO_ResetBits(GPIOC, GPIO_Pin_13);        /* LED 亮（低电平点亮） */
        printf("[RUN] 第 %u 次心跳   LED=亮   系统运行中\r\n", (unsigned)n);
        delay_ms(500);

        GPIO_SetBits(GPIOC, GPIO_Pin_13);          /* LED 灭 */
        printf("[RUN] 第 %u 次心跳   LED=灭\r\n", (unsigned)n);
        delay_ms(500);
    }
}

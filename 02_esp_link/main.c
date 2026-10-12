/**
 ******************************************************************************
 *  501 毕业设计 —— 步骤 2：STM32 ↔ ESP32-C3 无线链路验证
 ******************************************************************************
 *  目标：
 *      STM32 自动把 ESP32-C3 配成 AP + TCP Server
 *      → 手机连 ESP32 的热点
 *      → STM32 每 2 秒发一条模拟健康数据
 *      → 手机 App 收到数据 = 无线链路打通
 *
 *  接线（★ 4 根杜邦线 —— ★ 看板上的丝印，不要数第几个孔）：
 *      STM32 上排 印着【A2】的孔  →  ESP32-C3 右排第 22 孔（GPIO06）
 *      STM32 上排 印着【A3】的孔  →  ESP32-C3 右排第 23 孔（GPIO07）
 *      STM32 下排 印着【G】 的孔  →  ESP32-C3 右排第 17 孔（GND）
 *      ESP32-C3 用自己 Type-C 供电；STM32 用 Micro USB 供电
 *
 *  调试：USB-TTL 接 STM32 印着【A10】【A9】【G】的孔，串口助手 115200 看打印
 ******************************************************************************
 */

#include "stm32f10x.h"
#include <stdio.h>
#include <string.h>

/* ==================== USART2 接收环形缓冲 ==================== */
#define RXBUF_SIZE  1024
static volatile uint8_t  rxbuf[RXBUF_SIZE];
static volatile uint16_t rx_head = 0;
static volatile uint16_t rx_tail = 0;

static void rx_push(uint8_t b)
{
    uint16_t nxt = (uint16_t)((rx_head + 1) % RXBUF_SIZE);
    if (nxt != rx_tail) {
        rxbuf[rx_head] = b;
        rx_head = nxt;
    }
}

static int rx_pop(void)
{
    if (rx_head == rx_tail) return -1;
    {
        uint8_t b = rxbuf[rx_tail];
        rx_tail = (uint16_t)((rx_tail + 1) % RXBUF_SIZE);
        return (int)b;
    }
}

static void rx_clear(void) { rx_tail = rx_head; }

void USART2_IRQHandler(void)
{
    if (USART_GetITStatus(USART2, USART_IT_RXNE) != RESET) {
        rx_push((uint8_t)(USART_ReceiveData(USART2) & 0xFF));
        USART_ClearITPendingBit(USART2, USART_IT_RXNE);
    }
}

/* ==================== 延时 ==================== */
static void delay_ms(uint32_t ms)
{
    volatile uint32_t i;
    while (ms--) {
        for (i = 0; i < 7200; i++) { __NOP(); }
    }
}

/* ==================== USART1：给电脑看打印 ==================== */
static void USART1_Init(void)
{
    GPIO_InitTypeDef  gpio;
    USART_InitTypeDef usart;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_USART1, ENABLE);

    gpio.GPIO_Pin   = GPIO_Pin_9;
    gpio.GPIO_Mode  = GPIO_Mode_AF_PP;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &gpio);

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

int fputc(int ch, FILE *f)
{
    (void)f;
    USART_SendData(USART1, (uint8_t)ch);
    while (USART_GetFlagStatus(USART1, USART_FLAG_TC) == RESET) { }
    return ch;
}

/* ==================== USART2：和 ESP32-C3 通信 ==================== */
static void USART2_Init(void)
{
    GPIO_InitTypeDef  gpio;
    USART_InitTypeDef usart;
    NVIC_InitTypeDef  nvic;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_USART2, ENABLE);

    gpio.GPIO_Pin   = GPIO_Pin_2;            /* PA2 = USART2_TX */
    gpio.GPIO_Mode  = GPIO_Mode_AF_PP;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &gpio);

    gpio.GPIO_Pin  = GPIO_Pin_3;             /* PA3 = USART2_RX */
    gpio.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_Init(GPIOA, &gpio);

    usart.USART_BaudRate            = 115200;   /* ★ ESP-AT 默认 115200 */
    usart.USART_WordLength          = USART_WordLength_8b;
    usart.USART_StopBits            = USART_StopBits_1;
    usart.USART_Parity              = USART_Parity_No;
    usart.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    usart.USART_Mode                = USART_Mode_Rx | USART_Mode_Tx;
    USART_Init(USART2, &usart);

    USART_ITConfig(USART2, USART_IT_RXNE, ENABLE);
    nvic.NVIC_IRQChannel                   = USART2_IRQn;
    nvic.NVIC_IRQChannelPreemptionPriority = 1;
    nvic.NVIC_IRQChannelSubPriority        = 1;
    nvic.NVIC_IRQChannelCmd                = ENABLE;
    NVIC_Init(&nvic);

    USART_Cmd(USART2, ENABLE);
}

/* ==================== LED（PC13，板载，低电平亮） ==================== */
static void LED_Init(void)
{
    GPIO_InitTypeDef gpio;
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOC, ENABLE);
    gpio.GPIO_Pin   = GPIO_Pin_13;
    gpio.GPIO_Mode  = GPIO_Mode_Out_PP;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOC, &gpio);
    GPIO_SetBits(GPIOC, GPIO_Pin_13);
}

/* ==================== ESP32 通信工具 ==================== */

/* 往 USART2 发一个字节 */
static void u2_putc(char c)
{
    USART_SendData(USART2, (uint8_t)c);
    while (USART_GetFlagStatus(USART2, USART_FLAG_TXE) == RESET) { }
}

/* 发一条 AT 指令（自动补 \r\n） */
static void esp_cmd(const char *cmd)
{
    printf("  >> %s\r\n", cmd);
    while (*cmd) u2_putc(*cmd++);
    u2_putc('\r');
    u2_putc('\n');
}

/* 清空接收缓冲 */
static void esp_flush(void)
{
    rx_clear();
    delay_ms(30);
    rx_clear();
}

/**
 * ★ 等 ESP32 回应某个关键字（逐字符匹配，能处理 '>' 这种不带换行的提示符）
 * 返回 1 = 匹配到，0 = 超时
 */
static int esp_wait(const char *expect, uint32_t timeout_ms)
{
    uint32_t t = 0;
    uint16_t match = 0;
    uint16_t elen  = (uint16_t)strlen(expect);

    if (elen == 0) return 1;

    while (t < timeout_ms) {
        int c;
        while ((c = rx_pop()) >= 0) {
            /* --- 同步打印到电脑，方便调试 --- */
            if (c == '\n') {
                /* 换行不重复打，靠 \r 处理 */
            } else if (c == '\r') {
                putchar('\r');
                putchar('\n');
            } else if (c >= 32 && c < 127) {
                putchar(c);
            }
            fflush(stdout);

            /* --- 逐字符匹配 --- */
            if ((char)c == expect[match]) {
                match++;
                if (match == elen) {
                    putchar('\r');
                    putchar('\n');
                    return 1;
                }
            } else {
                match = ((char)c == expect[0]) ? 1 : 0;
            }
        }
        delay_ms(5);
        t += 5;
    }
    return 0;
}

/* 把缓冲区里剩的东西打出来（清尾巴） */
static void esp_drain(uint32_t ms)
{
    uint32_t t = 0;
    while (t < ms) {
        int c;
        while ((c = rx_pop()) >= 0) {
            if (c == '\r') { putchar('\r'); putchar('\n'); }
            else if (c >= 32 && c < 127) putchar(c);
        }
        fflush(stdout);
        delay_ms(10);
        t += 10;
    }
    putchar('\r'); putchar('\n');
}

/* ==================== 主程序 ==================== */
int main(void)
{
    uint32_t n = 0;
    char     line[128];
    char     cmd[48];

    LED_Init();
    USART1_Init();
    USART2_Init();

    printf("\r\n\r\n");
    printf("========================================\r\n");
    printf("  501 个人健康助手 —— 步骤 2\r\n");
    printf("  STM32 <-> ESP32-C3  无线链路验证\r\n");
    printf("========================================\r\n");
    printf("  串口1 A9/A10   : 115200  给电脑看\r\n");
    printf("  串口2 A2/A3    : 115200  连 ESP32\r\n");
    printf("========================================\r\n\r\n");

    /* ---------- 【1】测试 ESP32 在不在 ---------- */
    printf("【1】测试 ESP32-C3 是否在线...\r\n");
    esp_flush();
    esp_cmd("AT");
    if (!esp_wait("OK", 3000)) {
        printf("\r\n  X ESP32 没响应！检查这 4 点：\r\n");
        printf("     1. STM32 上排印着【A2】的孔 -> ESP32 右排第22孔\r\n");
        printf("     2. STM32 上排印着【A3】的孔 -> ESP32 右排第23孔\r\n");
        printf("     3. STM32 下排印着【G】 的孔 -> ESP32 右排第17孔\r\n");
        printf("     4. ESP32 有用 Type-C 供电吗\r\n");
        printf("\r\n  程序停在这里，重新接好线后按【复位键】重启。\r\n");
        while (1) {
            GPIO_ResetBits(GPIOC, GPIO_Pin_13);
            delay_ms(200);
            GPIO_SetBits(GPIOC, GPIO_Pin_13);
            delay_ms(200);
        }
    }
    printf("  √ ESP32 在线\r\n\r\n");
    esp_drain(200);

    /* ---------- 【2】配置成 AP + TCP Server ---------- */
    printf("【2】配置 ESP32-C3 为 AP 模式 + TCP 服务器...\r\n\r\n");

    esp_cmd("AT+CWMODE=2");
    esp_wait("OK", 3000);  esp_drain(200);

    esp_cmd("AT+CWSAP=\"HealthHelper\",\"12345678\",5,3");
    esp_wait("OK", 4000);  esp_drain(300);

    esp_cmd("AT+CIPMUX=1");
    esp_wait("OK", 3000);  esp_drain(200);

    esp_cmd("AT+CIPSERVER=1,8080");
    esp_wait("OK", 3000);  esp_drain(200);

    printf("\r\n  √ 配置完成\r\n");
    printf("  ★ 热点: HealthHelper    密码: 12345678\r\n");
    printf("  ★ 手机 App 连: 192.168.4.1 : 8080\r\n\r\n");

    /* ---------- 【3】等手机连上 ---------- */
    printf("【3】等手机连上来...\r\n");
    printf("     手机上：连热点 HealthHelper，然后 TCP Client 连 192.168.4.1:8080\r\n\r\n");
    {
        int connected = 0;
        uint32_t waited = 0;
        while (!connected && waited < 120000) {
            int c;
            while ((c = rx_pop()) >= 0) {
                static char b[128];
                static uint16_t bn = 0;
                if (c == '\r' || c == '\n') {
                    b[bn] = '\0';
                    if (bn > 0) {
                        if (strstr(b, "CONNECT")) {
                            printf("  ★★★ 手机连上了！(%s)\r\n\r\n", b);
                            connected = 1;
                        }
                    }
                    bn = 0;
                } else if (bn < sizeof(b) - 1) {
                    b[bn++] = (char)c;
                }
            }
            delay_ms(50);
            waited += 50;
            if ((waited % 10000) == 0) {
                printf("  ...还在等（%u 秒）\r\n", (unsigned)(waited / 1000));
            }
        }
        if (!connected) {
            printf("  ! 等了 2 分钟没等到手机。继续往下走 —— 手机连上后下面就能发出去。\r\n\r\n");
        }
    }

    /* ---------- 【4】循环发数据 ---------- */
    printf("【4】开始每 2 秒发一条模拟健康数据\r\n");
    printf("════════════════════════════════════════\r\n\r\n");

    while (1) {
        int len;

        n++;
        sprintf(line, "$HR=%d,SPO2=%d,TEMP=%d.%d,STEP=%d\r\n",
                72 + (int)(n % 8),
                97 - (int)(n % 3),
                36, (int)(n % 10),
                (int)(n * 13));
        len = (int)strlen(line);

        printf("[%u] %s", (unsigned)n, line);

        sprintf(cmd, "AT+CIPSEND=0,%d", len);
        esp_flush();
        esp_cmd(cmd);

        if (esp_wait(">", 2000)) {
            const char *p = line;
            while (*p) u2_putc(*p++);

            if (esp_wait("SEND OK", 3000)) {
                printf("      √ 已发给手机\r\n");
            } else {
                printf("      ! 没收到 SEND OK（手机连上了吗）\r\n");
            }
        } else {
            printf("      ! ESP32 没回 '>'（手机连上了吗）\r\n");
        }
        esp_drain(100);

        GPIO_ResetBits(GPIOC, GPIO_Pin_13);
        delay_ms(1000);
        GPIO_SetBits(GPIOC, GPIO_Pin_13);
        delay_ms(1000);
    }
}

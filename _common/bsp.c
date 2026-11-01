/**
 ******************************************************************************
 *  bsp.c —— 501 个人健康助手 · 板级支持包
 ******************************************************************************
 */
#include "bsp.h"

/* ==================== 延时 ==================== */

/* ============================================================================
 * ★★★ 2026-10-06 重写延时函数：改用 DWT 周期计数器
 *
 * 【为什么必须改】
 *   原来用 NOP 空循环估算：
 *       delay_us(): 8 个 NOP ≈ 0.18us（标称 1us）→ ★ 快 5 倍！
 *       delay_ms(): 7200 次循环 ≈ 0.6ms（标称 1ms）→ ★ 快 1.7 倍
 *
 *   后果（都已实测到）：
 *     · ★★ DS18B20 读不出温度（返回 -1000）
 *       它用单总线，时序要求 1us 级（复位 480us、写时隙 60us、采样点 15us），
 *       延时快 5 倍 → 时序全乱 → 从机不应答。
 *     · 3 秒开机自检实际只有约 1.8 秒
 *     · 软件 I2C 名义 25kHz 实际约 100kHz（碰巧能用，但不稳）
 *
 * 【新方案】
 *   Cortex-M3 内核自带 DWT（Data Watchpoint and Trace）周期计数器，
 *   直接数 CPU 周期，精度 = 1 个时钟周期，且不受编译器优化影响。
 *   72MHz 下 1us = 72 个周期。
 * ============================================================================ */

static uint8_t s_dwt_ok = 0;        /* DWT 是否可用 */

static void dwt_init(void)
{
    /* 使能跟踪与调试模块 */
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    /* 清零周期计数器并使能 */
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

    /* 验证：跑一小段，看计数器有没有在动 */
    {
        uint32_t a = DWT->CYCCNT;
        volatile uint32_t k;
        for (k = 0; k < 100; k++) { __NOP(); }
        if (DWT->CYCCNT != a) s_dwt_ok = 1;
    }
}

/**
 * 精确毫秒延时（基于 DWT 周期计数器）
 */
void delay_ms(uint32_t ms)
{
    while (ms--) {
        delay_us(1000);
    }
}

/**
 * ★ 精确定时微秒延时（基于 DWT 周期计数器）
 * 72MHz 下：1us = SystemCoreClock / 1000000 = 72 个周期
 */
void delay_us(uint32_t us)
{
    if (!s_dwt_ok) {
        /* DWT 不可用时的退化方案（极少发生）：按 72MHz 粗算，尽量接近 */
        volatile uint32_t i;
        while (us--) {
            for (i = 0; i < 40; i++) { __NOP(); }       /* 约 1us */
        }
        return;
    }

    {
        uint32_t start  = DWT->CYCCNT;
        uint32_t cycles = us * (SystemCoreClock / 1000000UL);

        /* ★ 用无符号减法，天然处理计数器回绕（32 位 @72MHz 约 59 秒绕一次） */
        while ((DWT->CYCCNT - start) < cycles) { }
    }
}

/**
 * ★★★ 2026-10-06 新增：毫秒级系统时间戳（基于 DWT 周期计数器）
 *
 * 【为什么需要】
 *   主循环周期不是固定的（10ms 延时 + I2C 操作 + OLED 分页刷新 ≈ 45ms），
 *   所以原来用"tick 计数"计时全部偏慢：
 *     · 温度：代码算 750ms，实际 3.4 秒才读一次 → 用户反馈"温度不实时更新"
 *     · "每 2 秒上传"：实际 9 秒
 *
 *   用 DWT 周期计数器做成真正的毫秒时间戳后，所有定时都准确了。
 *
 * 【回绕处理】
 *   CYCCNT 是 32 位，72MHz 下约 59.6 秒回绕一次。
 *   这里做高位补偿，做成单调递增的毫秒计数（可用几十天不溢出）。
 */
uint32_t millis(void)
{
    static uint32_t high = 0;           /* 回绕补偿的高位 */
    static uint32_t last = 0;
    uint32_t now;

    if (!s_dwt_ok) return 0;

    now = DWT->CYCCNT / (SystemCoreClock / 1000UL);     /* 0 ~ 59652 */

    if (now < last) {
        high += 59652UL;                /* 检测到回绕，补一个周期 */
    }
    last = now;

    return high + now;
}

/* ==================== 板载外设初始化 ==================== */

void bsp_init(void)
{
    GPIO_InitTypeDef gpio;

    /* ★★★ 第一件事：初始化 DWT 周期计数器（后面所有延时都靠它） */
    dwt_init();

    /* --- 时钟 --- */
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA |
                           RCC_APB2Periph_GPIOB |
                           RCC_APB2Periph_GPIOC, ENABLE);

    /* --- 板载 LED（PC13，低电平点亮） --- */
    gpio.GPIO_Pin   = LED_ONBOARD_PIN;
    gpio.GPIO_Mode  = GPIO_Mode_Out_PP;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(LED_ONBOARD_GPIO, &gpio);
    GPIO_SetBits(LED_ONBOARD_GPIO, LED_ONBOARD_PIN);        /* 先灭 */

    /* --- 红色 LED（PB1，高电平点亮） --- */
    gpio.GPIO_Pin = LED_R_PIN;
    GPIO_Init(LED_R_GPIO, &gpio);
    GPIO_ResetBits(LED_R_GPIO, LED_R_PIN);                  /* 先灭 */

    /* --- 蜂鸣器（PB0，★ 低电平触发 → 先拉高关掉） --- */
    gpio.GPIO_Pin = BUZZ_PIN;
    GPIO_Init(BUZZ_GPIO, &gpio);
    GPIO_SetBits(BUZZ_GPIO, BUZZ_PIN);                      /* ★ 先关 */

    /* --- 三个按键（上拉输入，按下 = 低） --- */
    gpio.GPIO_Pin  = KEY1_PIN | KEY2_PIN | KEY3_PIN;
    gpio.GPIO_Mode = GPIO_Mode_IPU;
    GPIO_Init(GPIOA, &gpio);
}

/* ==================== 输出控制 ==================== */

void led_onboard(int on)
{
    if (on) GPIO_ResetBits(LED_ONBOARD_GPIO, LED_ONBOARD_PIN);   /* 低电平亮 */
    else    GPIO_SetBits(LED_ONBOARD_GPIO, LED_ONBOARD_PIN);
}

void led_red(int on)
{
    if (on) GPIO_SetBits(LED_R_GPIO, LED_R_PIN);                 /* 高电平亮 */
    else    GPIO_ResetBits(LED_R_GPIO, LED_R_PIN);
}

void buzzer(int on)
{
    /* ★ 实测：低电平触发（碰 GND 会响） */
    if (on) GPIO_ResetBits(BUZZ_GPIO, BUZZ_PIN);                 /* 拉低 → 响 */
    else    GPIO_SetBits(BUZZ_GPIO, BUZZ_PIN);                   /* 拉高 → 停 */
}

/* ==================== 按键 ==================== */

/* ==================== ★★★ 按键（带软件消抖 + 长按支持）====================
 *
 * 【为什么要消抖】
 *   机械按键在按下/松开的瞬间，触点会弹跳 5~20ms，期间电平反复跳变。
 *   如果不消抖，一次按下可能被识别成好几次。
 *   现在主循环周期约 45ms（比抖动长）"碰巧"没出问题，但不可靠 ——
 *   以后改了主循环的节奏就会暴露。
 *
 * 【消抖算法】
 *   连续读到 3 次相同电平才认为状态真的变了。
 *   3 次 × 45ms ≈ 135ms，足够滤掉抖动，人手按一下（>100ms）也能可靠识别。
 *
 * 【长按支持】
 *   记录按下的持续时间，供上层判断"短按"还是"长按"。
 * ==================================================================== */

/* ★★★ 2026-10-15 修改：3 → 2
 *   这个常量现在只用于【松开】的消抖（按下改成立即响应了）。
 *   2 拍 × 45ms = 90ms，足够滤掉松手瞬间的触点抖动，
 *   又不会让"快速按键"被误判。
 *   ★ 背景：用户反馈"按太快没反应，要按一秒左右"——
 *     原来按下要 3 拍（135ms）才认，快速按键还没被认就松开了，事件完全丢失。*/
#define KEY_DEBOUNCE_CNT    2       /* 松开时连续几次一致才算数 */

/* ★★★ 2026-10-07：长按判定时间（毫秒）
 *   原来 3000ms，改成 2000ms —— 按着 3 秒有点久，2 秒既不会误触短按，操作也不累。
 *   ★ 且判定改在【松手时】做（见 key_get_event），按住期间不触发任何事件。*/
#define KEY_LONG_PRESS_MS   2000

typedef struct {
    uint8_t  gpio_port_id;          /* 0=GPIOA */
    uint16_t pin;
    uint8_t  stable;                /* 消抖后的稳定状态（1=按下）*/
    uint8_t  last_raw;              /* 上次原始读数 */
    uint8_t  same_cnt;              /* 连续相同次数 */
    uint32_t press_ms;              /* 按下起始时间（millis）*/
} key_state_t;

static key_state_t s_keys[3] = {
    { 0, KEY1_PIN, 0, 0, 0, 0 },
    { 0, KEY2_PIN, 0, 0, 0, 0 },
    { 0, KEY3_PIN, 0, 0, 0, 0 },
};

/* 读一个按键的原始电平（1 = 按下）*/
static uint8_t key_raw(uint8_t idx)
{
    uint16_t pin = s_keys[idx].pin;
    return (GPIO_ReadInputDataBit(GPIOA, pin) == Bit_RESET) ? 1 : 0;
}

/* ★ 消抖更新 —— 只更新【指定的一个键】
 *
 * 【★★★ 2026-10-15 关键设计说明（改过两次，这里说清楚为什么是这样）】
 *
 * 问题背景：
 *   main.c 每轮主循环连续调用 key_get_event(1) / (2) / (3)。
 *   如果每个调用都去更新【全部 3 个键】，那同一轮循环内每个键的
 *   same_cnt 会被 +3 → 一拍就凑满 KEY_DEBOUNCE_CNT(3)
 *   → ★ 消抖窗口从设计值 135ms 被压缩到 1 轮主循环（约 45ms）
 *   → 机械按键 5~20ms 的触点弹跳没被滤掉。
 *
 * ✅ 本实现的做法（最终版）：
 *   key_get_event(i) 只调用 key_poll_one(i) —— 只更新自己那一个键。
 *   于是每个键每轮主循环【正好被更新 1 次】：
 *        1 拍 = 45ms，3 拍 = 135ms  ★ 正好等于设计值
 *   · 不需要任何"时间闸门"，没有隐式状态
 *   · 采样节奏与调用顺序、调用次数完全无关
 *
 * ⚠️ 曾经用过的写法（已废弃，记录备查）：
 *   在函数开头加 `if (millis() - s_poll_ms < 10) return;`
 *   —— 在当前 45ms 主循环下能工作，但如果哪天主循环周期缩到 ≤10ms，
 *      闸门会【每拍都挡住】→ 按键全部失效。所以换成了现在的单键更新。
 */
static void key_poll_one(uint8_t i)
{
    uint8_t raw;

    if (i > 2) return;

    raw = key_raw(i);

    if (raw) {
        /* ================= 按下：★ 立即响应（不等消抖）=================
         *
         * ★★★ 2026-10-15 关键改进（用户实测反馈驱动）
         *
         * 【为什么改成立即响应】
         *   原来要求"连续 KEY_DEBOUNCE_CNT 次采样一致"才认状态变化，
         *   主循环约 45ms/拍 → 按下要 3 拍 = 135ms 才被认。
         *   ★ 用户快速按一下（<135ms）时，还没被认成"按下"就已经松开，
         *     于是 press_ms 从来没被设置过 → ★★ 这次按键【完全丢失】。
         *   ★ 实测现象：用户说"按太快没反应，得按一秒左右才生效"。
         *
         * 【为什么按下可以不等消抖】
         *   按下是【主动动作】—— 手指持续施加压力，触点一旦闭合就会被压住，
         *   抖动主要发生在闭合的几毫秒内；而我们的采样间隔 45ms 远大于抖动时长，
         *   所以单次采到低电平基本就是真的按下了。
         *   ★ 松开相反：松手瞬间触点会弹跳，必须消抖（见下面的 else 分支）。
         *
         * 【代价】如果按下瞬间恰好采到抖动中的高电平，press_ms 会晚一拍（45ms）。
         *   对"短按/长按"的判定无影响（长按判定是 2000ms 量级）。*/
        if (!s_keys[i].last_raw) {
            s_keys[i].press_ms = millis();      /* ★ 记下按下时刻（同一拍就记）*/
        }
        s_keys[i].last_raw = 1;
        s_keys[i].stable   = 1;                 /* ★ 立即置为"已按下" */
        s_keys[i].same_cnt = 0;                 /* 重置松开计时 */

    } else {
        /* ================= 松开：消抖确认（滤掉触点弹跳）================= */
        if (s_keys[i].last_raw) {               /* 从"按下"变"松开"的第一拍 */
            s_keys[i].last_raw = 0;
            s_keys[i].same_cnt = 1;
        } else if (s_keys[i].same_cnt < KEY_DEBOUNCE_CNT) {
            s_keys[i].same_cnt++;
        }

        /* 连续 KEY_DEBOUNCE_CNT 拍都是松开 → 才认"真的松开了" */
        if (s_keys[i].same_cnt >= KEY_DEBOUNCE_CNT) {
            s_keys[i].stable = 0;
        }
    }
}

/* ★ 更新全部 3 个键（只有 key_any_just_pressed 需要）*/
static void key_poll_all(void)
{
    uint8_t i;
    for (i = 0; i < 3; i++) key_poll_one(i);
}

/**
 * ★ 读取按键事件
 *   idx: 1=KEY1(PA1)  2=KEY2(PA4)  3=KEY3(PA5)
 *
 * 返回：0 = 无事件
 *       1 = 短按（按下后松开，且按住时间 < KEY_LONG_PRESS_MS）
 *       2 = 长按（按下后松开，且按住时间 ≥ KEY_LONG_PRESS_MS）
 *
 * ★★★ 2026-10-07 修改：改成【松手时才判定】
 *
 * 【为什么】用户反馈："长按的过程中灯就已经亮了，我希望是按完之后马上亮。"
 *   原来按住 3 秒就立刻报长按事件（那时手还按着），
 *   导致 LED 在按住期间就亮了。
 *
 *   现在改成：按住期间【不报任何事件】，等【松手】时才根据时长判定：
 *       按住 <  2 秒  → 短按 (1)
 *       按住 ≥  2 秒  → 长按 (2)
 *   这样上层"按完之后亮灯"的逻辑就自然成立了。
 *
 *   ★ 副作用（好的）：长按也不会中途触发功能，避免误操作。
 */
uint8_t key_get_event(uint8_t idx)
{
    uint8_t i = (uint8_t)(idx - 1);
    uint8_t ev = 0;

    if (idx < 1 || idx > 3) return 0;

    key_poll_one(i);                /* ★ 只更新自己这个键（每拍 1 次 → 135ms 消抖）*/

    if (!s_keys[i].stable) {
        /* --- ★ 已松开：根据按住时长判定 --- */
        if (s_keys[i].press_ms != 0) {
            uint32_t held = millis() - s_keys[i].press_ms;
            ev = (held >= KEY_LONG_PRESS_MS) ? 2 : 1;
            s_keys[i].press_ms = 0;         /* 清零，避免重复上报 */
        }
    }
    /* ★ 按住期间不返回任何事件 —— 等松手 */

    return ev;
}

/**
 * ★★★ 2026-10-07 新增：检测"任意键刚按下"的瞬间（用于按键点亮反馈）
 *
 * 【为什么单独做这个】
 *   key_get_event() 只在【短按松开】或【长按 3 秒】时才返回事件，
 *   但用户要的是"一按下去就有反馈" —— 必须检测按下瞬间的电平跳变。
 *
 * 返回：刚刚按下的键号（1/2/3），0 = 没有新按下
 *
 * ★ 内部已经走消抖，不会因为触点弹跳重复触发。
 */
uint8_t key_any_just_pressed(void)
{
    static uint8_t prev_stable[3] = { 0, 0, 0 };
    uint8_t i, ret = 0;

    key_poll_all();                 /* ★ 这个接口要判断"任意键"，所以全部更新 */

    for (i = 0; i < 3; i++) {
        /* ★ 状态从"松开"变成"按下"的那一次 */
        if (s_keys[i].stable && !prev_stable[i]) {
            if (ret == 0) ret = (uint8_t)(i + 1);       /* 记第一个按下的键 */
        }
        prev_stable[i] = s_keys[i].stable;
    }

    return ret;
}

/* 兼容旧接口（原来只有边沿检测，现在内部走消抖）*/
uint8_t key1_pressed(void) { return (key_get_event(1) != 0) ? 1 : 0; }
uint8_t key2_pressed(void) { return (key_get_event(2) != 0) ? 1 : 0; }
uint8_t key3_pressed(void) { return (key_get_event(3) != 0) ? 1 : 0; }

/* ==================== USART1：调试打印 ==================== */

void uart1_init(void)
{
    GPIO_InitTypeDef  gpio;
    USART_InitTypeDef usart;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA | RCC_APB2Periph_USART1, ENABLE);

    gpio.GPIO_Pin   = GPIO_Pin_9;           /* PA9 = TX */
    gpio.GPIO_Mode  = GPIO_Mode_AF_PP;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &gpio);

    gpio.GPIO_Pin  = GPIO_Pin_10;           /* PA10 = RX */
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

/* printf 重定向到 USART1 —— ★ Keil 里必须勾 Use MicroLIB */
int fputc(int ch, FILE *f)
{
    uint32_t guard = 0;
    (void)f;
    USART_SendData(USART1, (uint8_t)ch);
    /* ★ 2026-10-06：加超时保护，避免 USART1 异常时 printf 永久卡死 */
    while (USART_GetFlagStatus(USART1, USART_FLAG_TC) == RESET) {
        if (++guard > 200000UL) break;
    }
    return ch;
}

/* ==================== USART2：和 ESP32-C3 通信 ==================== */

#define U2_RXBUF_SIZE   1024
static volatile uint8_t  u2_rxbuf[U2_RXBUF_SIZE];
static volatile uint16_t u2_head = 0;
static volatile uint16_t u2_tail = 0;
static volatile uint32_t u2_overflow = 0;       /* ★ 溢出计数（丢了几个字节） */

void USART2_IRQHandler(void)
{
    if (USART_GetITStatus(USART2, USART_IT_RXNE) != RESET) {

        /* ★★★ 2026-10-06 关键修复：
           STM32 的 USART 有个硬规则 —— RXNE 置位后【必须读 DR 寄存器】才能清掉它。
           原来的写法是"缓冲区满了就不读 DR"，
           结果 ESP32 上电喷大量启动日志把 1024 字节缓冲区灌满后：
             nxt == u2_tail → 不读 DR → RXNE 一直为 1
             → 中断【持续触发】→ CPU 100% 卡在中断里
             → 主循环完全饿死 → 串口打印停止 → 表现为"卡死"
           ★ 修法：先无条件读 DR（清 RXNE），再看缓冲区有没有空间存。 */
        uint8_t data = (uint8_t)(USART_ReceiveData(USART2) & 0xFF);

        {
            uint16_t nxt = (uint16_t)((u2_head + 1) % U2_RXBUF_SIZE);
            if (nxt != u2_tail) {
                u2_rxbuf[u2_head] = data;
                u2_head = nxt;
            } else {
                u2_overflow++;          /* 缓冲区满，这个字节丢弃（但不影响中断清除） */
            }
        }

        /* 读 DR 已自动清 RXNE；这里再清一次防止标志残留 */
        USART_ClearITPendingBit(USART2, USART_IT_RXNE);
    }
}

void uart2_init(void)
{
    GPIO_InitTypeDef  gpio;
    USART_InitTypeDef usart;
    NVIC_InitTypeDef  nvic;

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_USART2, ENABLE);

    gpio.GPIO_Pin   = GPIO_Pin_2;           /* PA2 = TX → ESP32 GPIO06 */
    gpio.GPIO_Mode  = GPIO_Mode_AF_PP;
    gpio.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &gpio);

    gpio.GPIO_Pin  = GPIO_Pin_3;            /* PA3 = RX ← ESP32 GPIO07 */
    gpio.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_Init(GPIOA, &gpio);

    usart.USART_BaudRate            = 115200;
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

/* ★★★ 2026-10-06 加超时保护：
   原来 while (TXE == RESET) { } 是【死等】。
   如果 USART2 没初始化（时钟没开/外设没使能），TXE 永远不置位 → 主循环永久卡死。
   现象：串口打印到 ">> AT" 就不动了，同时蜂鸣器保持"响"的状态（因为 alert_update 不再被调用）。
   ★ 现在最多等 200000 次循环就放弃，绝不卡死。 */
void uart2_send(const char *s)
{
    while (*s) {
        uint32_t guard = 0;
        USART_SendData(USART2, (uint8_t)(*s++));
        while (USART_GetFlagStatus(USART2, USART_FLAG_TXE) == RESET) {
            if (++guard > 200000UL) return;         /* ★ 超时保护，直接返回 */
        }
    }
}

/**
 * 从 ESP32 收一行（以 \n 结尾）
 * 返回 1 = 收到一行，0 = 超时
 */
int uart2_recv_line(char *buf, int maxlen, uint32_t timeout_ms)
{
    int n = 0;
    uint32_t t = 0;

    while (t < timeout_ms) {
        while (u2_head != u2_tail) {
            char c = (char)u2_rxbuf[u2_tail];
            u2_tail = (uint16_t)((u2_tail + 1) % U2_RXBUF_SIZE);

            if (c == '\n') {
                buf[n] = '\0';
                if (n > 0 && buf[n - 1] == '\r') buf[n - 1] = '\0';
                return 1;
            }
            if (n < maxlen - 1) buf[n++] = c;
        }
        delay_ms(5);
        t += 5;
    }
    buf[n] = '\0';
    return 0;
}

/**
 * 非阻塞取一个字节
 * 返回 1 = 取到了（放在 *c），0 = 缓冲区空
 */
int uart2_getc_nonblock(char *c)
{
    if (u2_head == u2_tail) return 0;
    *c = (char)u2_rxbuf[u2_tail];
    u2_tail = (uint16_t)((u2_tail + 1) % U2_RXBUF_SIZE);
    return 1;
}

void uart2_clear(void)
{
    u2_tail = u2_head;
}

/* ★ 返回接收溢出次数（丢了多少字节），用来诊断 ESP32 是不是发太多太快 */
uint32_t uart2_get_overflow(void)
{
    return u2_overflow;
}

/* ★ 返回缓冲区里还有多少字节没读 */
uint16_t uart2_available(void)
{
    return (uint16_t)((u2_head - u2_tail) & (U2_RXBUF_SIZE - 1));
}

/**
 ******************************************************************************
 *  09_main —— 步骤 9：★ 完整主程序（最终版）
 ******************************************************************************
 *  501 毕业设计《基于 STM32 的个人健康助手设计与实现》
 *
 *  功能（对照任务书 7 条硬件要求）：
 *      ① 心率 30~200 bpm      —— MAX30102
 *      ② 血氧 70~100%          —— MAX30102
 *      ③ 体温 32~42℃           —— DS18B20
 *      ④ 计步                  —— MPU6050
 *      ⑤ 阈值提醒（心率 60~100、体温 36.0~37.5）—— 蜂鸣器 + 红色 LED
 *      ⑥ OLED 显示四项数据
 *      ⑦ WiFi 上传手机 —— ESP32-C3（AP 模式 + TCP Server）
 *
 *  ★ 任务调度（全部非阻塞）：
 *       每  10ms : max30102_update()   读心率血氧
 *       每  25ms : pedometer_update()  计步
 *       每 100ms : alert_update()      告警判断（含蜂鸣器/LED）
 *       每 100ms : ds_task()           温度状态机（不阻塞）
 *       每 500ms : oled_update()       刷屏
 *       每  2 s  : esp_send_data()     上传到手机
 *
 *  接线：
 *      I2C（B6=SCL，B7=SDA）→ OLED + MPU6050 + MAX30102
 *      A0  → DS18B20 DQ（4.7kΩ 上拉到 3.3V）
 *      B0  → 蜂鸣器 I/O（VCC 接 5V）
 *      B1  → 220Ω → 红色 LED → GND
 *      A1/A4/A5 → 三个按键（另一端接 GND）
 *      A2/A3 → ESP32-C3 的 GPIO06/GPIO07
 *      A9/A10 → USB-TTL（看调试打印）
 ******************************************************************************
 */
#include "bsp.h"
#include "i2c.h"
#include "oled.h"
#include "ds18b20.h"
#include "mpu6050.h"
#include "max30102.h"
#include "alert.h"

/* ==================== 全局数据 ==================== */
static max30102_data_t g_max;           /* 心率血氧 */
static int16_t  g_temp10   = DS18B20_ERR;   /* 体温 ×10 */
static uint8_t  g_temp_valid = 0;
static uint32_t g_steps    = 0;         /* 步数 */
static uint8_t  g_alert    = ALERT_NONE;
static uint8_t  g_esp_ok   = 0;         /* ESP32 是否就绪 */

/* ★★★ 2026-10-06 新增：按键相关状态（文件级，oled_build 也要用）
 *   KEY1 → 消音（走 alert_mute，不需要额外变量）
 *   KEY2 → 立即上传（把 s_last_send 清零即可）
 *   KEY3 → 显示系统信息页 3 秒 */
/* ★★★ 2026-10-07 最终版：诊断输出开关
 *
 *   0 = 正式版（默认）：只输出数据行 + 每秒一行状态，干净专业
 *   1 = 诊断版：输出五行详细诊断（[MAX]/[I2C]/[FIFO]/[对照]/[采样率]）
 *              排查问题时改成 1 重新编译即可
 */
#define DETAILED_DEBUG  0

static uint32_t s_info_until = 0;       /* 信息页显示截止的 millis() 时间戳 */
static uint8_t  s_key_msg    = 0;       /* 最近按下的键（1/2/3），0 = 无 */
static uint32_t s_key_led_until = 0;     /* ★ 按键点亮反馈截止时间（按下后亮 1 秒）*/


/* ==================== DS18B20 非阻塞状态机 ==================== */
#define DS_IDLE     0
#define DS_CONVERT  1
#define DS_READY    2

static uint8_t  s_ds_state = DS_IDLE;
static uint32_t s_ds_ms    = 0;      /* ★ 转换开始时的 millis() 时间戳 */
/* static uint16_t s_ds_timer = 0;  ★ 已废弃：改用 s_ds_tick 基于 tick 计时 */

/**
 * ★★★ 2026-10-06 重写：温度状态机改成【基于 tick 计时】
 *
 * 【原来出的问题】
 *   原版假设"每 100ms 调用一次"，用 s_ds_timer += 100 累加。
 *   但主循环实际周期是 500~600ms（OLED 分页刷新占了大部分时间），
 *   于是 800ms 的转换等待被拉长到 4~5 秒 → 温度"不实时更新"。
 *
 * 【新做法】
 *   每次主循环都调用本函数，用 tick 差值计时。
 *   DS18B20 12 位转换需要 750ms，主循环约 10~50ms 一拍，
 *   取 75 拍作阈值（实际取决于循环周期，最长也就 3 秒，比原来快很多）。
 *   ★ 转换没完成时读出来是 85℃（DS18B20 的上电默认值），所以宁可多等。
 */
/**
 * ★★★ 2026-10-06 重写：温度状态机改成【基于 millis() 真实时间】
 *
 * 【原来出的问题】
 *   主循环周期实际约 45ms（10ms 延时 + I2C 操作 + OLED 分页刷新），
 *   而代码按"tick = 10ms"估算 → DS18B20 的 750ms 转换被算成 3.4 秒，
 *   用户反馈"温度不实时更新"。
 *
 * 【新做法】用 DWT 周期计数器做的 millis() 时间戳，计时精确。
 *   DS18B20 12 位转换需要 750ms，这里等 800ms 留余量。
 *   转换没完成时读出来是 85℃（上电默认值），代码里会丢弃并重试。
 */
static void ds_task(void)
{
    switch (s_ds_state) {
        case DS_IDLE:
            if (ds18b20_start()) {
                s_ds_state = DS_CONVERT;
                s_ds_ms    = millis();
            }
            break;

        case DS_CONVERT:
            if ((millis() - s_ds_ms) >= 800) {      /* ★ 精确 800ms */
                s_ds_state = DS_READY;
            }
            break;

        case DS_READY:
            {
                int16_t t = ds18b20_read();
                if (t == DS18B20_ERR) {
                    /* ★ 2026-10-06 修复：读失败时【保留上次的有效值】，
                       不要清成 -1000（否则 OLED 显示"待测量"、串口打印 -100.0，
                       看起来像温度坏了，其实只是这一拍没读到）。*/
                    if (g_temp10 == DS18B20_ERR) g_temp_valid = 0;
                } else if (t == 850) {
                    /* ★ 85.0℃ 是 DS18B20 上电默认值，说明转换没完成，这次丢弃重试 */
                } else {
                    g_temp10     = t;
                    g_temp_valid = 1;
                }
            }
            s_ds_state = DS_IDLE;
            break;
    }
}

/* ==================== OLED 显示 ==================== */
/* ==================== ★ 中文数据界面 ====================
 *
 *      心率   75 bpm
 *      血氧   98 %
 *      温度   36.5 C
 *      步数   1234
 *
 * ★ 汉字 16×16 占 2 页 → 8 页屏正好 4 行
 * ★ 标签固定在列 0~31（2 个汉字），数值从列 48 开始
 * ★ 状态提示放在右下角小字（1 页高，不占行）
 * ================================================================= */

static void oled_build(void)   /* ★ 只操作显存，发送靠主循环分页刷新 */
{
    char buf[16];

    oled_clear();

    /* ================= 心率 =================
     * ★ 措辞改进（2026-10-06 用户建议）：
     *   "无数据" 让人以为没做好 → 改成状态描述
     *     没手指      → 待测量
     *     有手指在测  → 测量中
     *     算出来了    → 数字 + bpm
     *     传感器挂了  → 失败
     */
    oled_draw_cn(0, 0, "心率");
    if (g_max.hr_valid) {
        sprintf(buf, "%u", g_max.hr);
        oled_draw_str(48, 1, buf);
        oled_draw_str(80, 1, "bpm");
    } else if (!g_max.finger) {
        oled_draw_cn(48, 0, "待测量");
    } else {
        oled_draw_cn(48, 0, "测量中");
    }

    /* ================= 血氧 ================= */
    oled_draw_cn(0, 2, "血氧");
    if (g_max.spo2_valid) {
        sprintf(buf, "%u", g_max.spo2);
        oled_draw_str(48, 3, buf);
        oled_draw_str(74, 3, "%");
    } else if (!g_max.finger) {
        oled_draw_cn(48, 2, "待测量");
    } else {
        oled_draw_cn(48, 2, "测量中");
    }

    /* ================= 温度 =================
     * ★ 2026-10-06 修：原来低于 33℃ 显示"未测"，用户反映"温度一直显示未测"。
     *   现在改成：只要读到有效值（≠ -1000）就【显示数字】。
     *   TEMP_VALID_MIN(33℃) 这个门槛【只用于告警判断】，不用于显示。
     */
    oled_draw_cn(0, 4, "温度");
    if (g_temp10 == -1000) {
        oled_draw_cn(48, 4, "待测量");
    } else {
        int t  = (int)g_temp10;
        int ti = t / 10;
        int td = t % 10;
        if (td < 0) td = -td;
        sprintf(buf, "%d.%d", ti, td);
        oled_draw_str(48, 5, buf);
        oled_draw_str(84, 5, "C");
    }

    /* ================= 步数 ================= */
    oled_draw_cn(0, 6, "步数");
    sprintf(buf, "%lu", (unsigned long)g_steps);
    oled_draw_str(48, 7, buf);

    /* ================= ★ 按键提示 / 系统信息页 ================= */
    if (millis() < s_info_until) {
        /* KEY3 按下后 3 秒内：整屏显示系统信息 */
        max30102_dbg_t dg;
        max30102_get_debug(&dg);
        oled_clear();
        oled_draw_cn(32, 0, "系统信息");
        oled_draw_str(0, 2, "Sample:");
        sprintf(buf, "%lu", (unsigned long)dg.sample_cnt);
        oled_draw_str(54, 2, buf);
        oled_draw_str(0, 3, "Overflow:");
        sprintf(buf, "%u", dg.fifo_ovf);
        oled_draw_str(66, 3, buf);
        oled_draw_str(0, 4, "Peaks:");
        sprintf(buf, "%u", dg.peak_cnt);
        oled_draw_str(54, 4, buf);
        oled_draw_str(0, 5, "WiFi:");
        oled_draw_str(42, 5, g_esp_ok ? "OK" : "--");
        oled_draw_str(0, 6, "Alert:");
        oled_draw_str(48, 6, g_alert == ALERT_NONE ? "None" : "YES");
        oled_draw_str(0, 7, alert_is_muted() ? "MUTE ON" : "MUTE OFF");
        return;                     /* 信息页占满整屏，直接返回 */
    }

    /* ================= 右下角小状态 ================= */
    if (g_alert != ALERT_NONE) {
        oled_draw_str(100, 7, "ALERT");
    } else if (g_esp_ok) {
        oled_draw_str(112, 7, "WIFI");
    }

    /* ★ 这里【不】调 oled_refresh()，由主循环分页发送 */
}

/* ==================== ESP32 通信 ==================== */

/**
 * ★★★ 2026-10-06 重写：全程透明版
 *
 * 改进：
 *   ① 发指令前先【清空缓冲 + 等 150ms】，让 ESP32 有时间准备
 *   ② ★ 收到的每一个字节都打印出来（能看到 ESP32 到底回了什么）
 *   ③ ★ 打印溢出计数（如果 ESP32 发太猛，会显示丢了多少字节）
 *   ④ 超时时间加长（ESP32 处理 AT+CWSAP 要 1~2 秒）
 *   ⑤ 全程有板载 LED 心跳（证明程序还在跑，不是真死机）
 */
static uint8_t esp_cmd_wait(const char *cmd, const char *expect, uint32_t timeout_ms)
{
    uint32_t t = 0;
    uint16_t match = 0, elen = (uint16_t)strlen(expect);
    uint16_t got = 0;                   /* 收到多少字节 */
    uint16_t shown = 0;                 /* 打印了多少个（防止刷屏） */

    /* ★ 清空缓冲，并给 ESP32 一点准备时间 */
    uart2_clear();
    delay_ms(150);

    printf("     >> %s\r\n", cmd);
    uart2_send(cmd);
    uart2_send("\r\n");

    if (elen == 0) { delay_ms(timeout_ms > 100 ? 100 : timeout_ms); return 1; }

    printf("     << ");

    while (t < timeout_ms) {
        char c;

        /* 把缓冲区里的全部读完 */
        while (uart2_getc_nonblock(&c)) {
            got++;

            /* ★ 打印收到的字符（最多 120 个，防止刷屏） */
            if (shown < 120) {
                if (c == '\r' || c == '\n') {
                    /* 换行统一显示成 | 便于一行看完 */
                    if (shown > 0 && shown < 120) { putchar('|'); shown++; }
                } else if (c >= 32 && c < 127) {
                    putchar(c); shown++;
                } else {
                    putchar('.'); shown++;
                }
                fflush(stdout);
            } else if (shown == 120) {
                printf("...(更多略)");
                shown = 121;
            }

            /* 逐字符匹配 */
            if (c == expect[match]) {
                match++;
                if (match == elen) {
                    printf("\r\n     √ 匹配到 \"%s\"（收到 %u 字节）\r\n", expect, got);
                    return 1;
                }
            } else {
                match = (c == expect[0]) ? 1 : 0;
            }
        }

        delay_ms(5);
        t += 5;                         /* ★ 无条件累加 */
    }

    printf("\r\n     X 超时（%u ms）—— 共收到 %u 字节，没匹配到 \"%s\"\r\n",
           (unsigned)timeout_ms, got, expect);

    if (got == 0) {
        printf("       ★ 一个字节都没收到 → STM32 到 ESP32 的线不通（A2 / GND）\r\n");
    } else {
        printf("       ★ 收到数据但内容不对 → 可能 ESP32 不在 AT 模式，或者波特率不对\r\n");
    }

    return 0;
}

static uint8_t esp_init(void)
{
    uint32_t ovf;

    printf("【ESP32】初始化\r\n");
    printf("  接线应为：A2→ESP32右排22孔  A3→右排23孔  G→右排17孔\r\n");
    printf("  ESP32 要插自己的 Type-C 供电\r\n\r\n");

    /* ---- 步骤 1：先看看 ESP32 上电时喷了什么 ---- */
    printf("  [步骤 1/6] 先听 1.5 秒，看 ESP32 有没有主动说话...\r\n");
    uart2_clear();
    {
        uint8_t i;
        uint16_t n = 0;
        char c;
        for (i = 0; i < 15; i++) {
            led_onboard(1);
            delay_ms(50);
            led_onboard(0);
            delay_ms(50);
            while (uart2_getc_nonblock(&c)) n++;
        }
        printf("     收到 %u 字节\r\n", n);
        if (n == 0) {
            printf("     ★ 一个字节都没有 —— ESP32 没在说话（可能没供电，或线不通）\r\n");
        } else {
            printf("     ★ ESP32 有在发数据（上电日志），说明 A3/GND 这根线是通的\r\n");
        }
    }

    /* ---- 步骤 2：发 AT ---- */
    printf("\r\n  [步骤 2/6] 发 AT 测试\r\n");
    if (!esp_cmd_wait("AT", "OK", 3000)) {
        printf("\r\n  X ESP32 无响应！检查：\r\n");
        printf("     1. A2 → ESP32 右排第 22 孔（GPIO06）\r\n");
        printf("     2. A3 → ESP32 右排第 23 孔（GPIO07）\r\n");
        printf("     3. G  → ESP32 右排第 17 孔（GND）\r\n");
        printf("     4. ESP32 插了自己的 Type-C 供电吗（板上电源灯亮吗）\r\n");
        printf("     5. ★ 孔号从【天线端】数起\r\n");
        printf("     6. ★ 试试把 A2 / A3 两根线对调\r\n");
        printf("\r\n  ★ 跳过 ESP32，继续跑其他功能（数据不上传，但传感器照常工作）\r\n\r\n");
        return 0;
    }

    /* ---- 步骤 3~6 ---- */
    printf("\r\n  [步骤 3/6] 设 AP 模式\r\n");
    esp_cmd_wait("AT+CWMODE=2", "OK", 3000);
    delay_ms(200);

    printf("\r\n  [步骤 4/6] 建热点（这一步 ESP32 要 1~2 秒）\r\n");
    esp_cmd_wait("AT+CWSAP=\"HealthHelper\",\"12345678\",5,3", "OK", 6000);
    delay_ms(300);

    printf("\r\n  [步骤 5/6] 开多连接\r\n");
    esp_cmd_wait("AT+CIPMUX=1", "OK", 3000);
    delay_ms(200);

    printf("\r\n  [步骤 6/6] 开 TCP 服务器（端口 8080）\r\n");
    esp_cmd_wait("AT+CIPSERVER=1,8080", "OK", 4000);

    ovf = uart2_get_overflow();
    printf("\r\n  √ ESP32 初始化流程走完（接收溢出 %u 次）\r\n", (unsigned)ovf);
    printf("     热点: HealthHelper / 12345678\r\n");
    printf("     手机连: 192.168.4.1:8080\r\n\r\n");
    return 1;
}

/**
 * 把一行数据通过 ESP32 发给手机
 * 流程：AT+CIPSEND=0,<len> → 等 '>' → 发数据 → 等 SEND OK
 */
static void esp_send_data(const char *line)
{
    char cmd[32];
    int  len = (int)strlen(line);
    int  i;

    if (!g_esp_ok) return;

    sprintf(cmd, "AT+CIPSEND=0,%d", len);

    if (!esp_cmd_wait(cmd, ">", 400)) {    /* ★ 1500->400ms，避免长时间阻塞导致 FIFO 溢出 */
        /* 没回 '>' —— 多半是手机还没连 */
        return;
    }

    uart2_send(line);
    (void)i;

    esp_cmd_wait("", "SEND OK", 2000);
}

/* ==================== 主程序 ==================== */
int main(void)
{
    /* uint32_t tick = 0;  ★ 已废弃：全部定时改用 millis() 真实时间 */              /* 主循环计数，每次 10ms */
    uint32_t n_send = 0;
    char     line[64];
    uint8_t  i;
    uint8_t  alert_printed = 0;
    uint8_t  s_oled_page = 0;       /* ★ 分页刷新用的当前页号 */
    /* ★★★ 2026-10-06 全部定时改成基于 millis() 的真实时间
       （主循环周期约 45ms 不固定，用 tick 计数计时会偏慢好几倍）*/
    uint32_t s_last_send  = 0;      /* 上次上传手机的时间 */
    /* s_last_build 已废弃：改成刷完一轮立刻重建 */
    uint32_t s_last_page  = 0;      /* 上次刷 OLED 一页 */
    uint32_t s_last_wd    = 0;      /* 上次看门狗检查 */
    uint32_t s_last_diag  = 0;      /* 上次打印诊断 */
    uint32_t s_last_ped   = 0;      /* 上次计步采样（25ms）*/
    uint32_t s_last_snap  = 0;      /* 上次数据快照（100ms）*/
    uint32_t s_last_led   = 0;      /* 上次 LED 心跳翻转 */

    /* ★★★ 2026-10-06 新增：按键状态
     *   KEY1 (PA1) → 消音 10 秒（告警仍在，只是蜂鸣器不响）
     *   KEY2 (PA4) → 立即上传数据到手机
     *   KEY3 (PA5) → 显示系统信息 3 秒 */
    /* k1_last/k2_last/k3_last 已废弃：消抖改在 bsp.c 的 key_get_event() 里做 */
    /* ★ s_info_until / s_key_msg 改成文件级静态变量（oled_build 里要用）*/

    /* ---------- 初始化 ---------- */
    bsp_init();
    uart1_init();
    i2c_init();

    /* ★★★ 2026-10-06 关键修复：漏了这一行！
       uart2_init() 必须显式调用（bsp_init() 内部【没有】调用它）。
       漏掉的后果：
         · USART2 时钟没开、GPIO 没配、外设没使能
         · 接收中断没开 → 一个字节都收不到（"步骤 1 收到 0 字节"）
         · uart2_send() 里 while(TXE==RESET) 永远等不到 → ★ 主循环卡死在这里
         · 主循环卡死 → alert_update() 不再被调用 → 蜂鸣器保持"响"的状态
       → 一个 bug 同时造成"收不到数据 + 卡死 + 蜂鸣器一直响"三个现象。 */
    uart2_init();

    printf("\r\n\r\n");
    printf("========================================\r\n");
    printf("  501 个人健康助手 —— 完整版\r\n");
    printf("  基于 STM32F103C8T6\r\n");
    printf("========================================\r\n\r\n");

    /* ---------- ★★★ 第 1 步：开机自检（蜂鸣器 + 红 LED 一起动作 3 秒）---------- */
    printf("【开机自检】蜂鸣器响 + 红色 LED 亮，持续 3 秒...\r\n");

    alert_init();
    alert_test();       /* ★ alert_test() 内部会：响+亮 3 秒 → 停 → 短响 2 声 */

    printf("  自检结束（听到 3 秒长响 + 2 声短响 = 蜂鸣器和 LED 都正常）\r\n\r\n");

    /* ---------- ★ 第 2 步：初始化 OLED（先显示"自检中"）---------- */
    if (i2c_probe(OLED_ADDR) == I2C_OK) {
        oled_init();
        oled_clear();
        oled_draw_str(20, 1, "501 Health Helper");
        oled_draw_str(28, 3, "Self Test OK");
        oled_draw_str(24, 5, "Booting...");
        oled_refresh();
        printf("√ OLED   OK\r\n");
    } else {
        printf("X OLED   找不到（0x3C）\r\n");
    }
    delay_ms(800);

    /* MPU6050 */
    if (mpu6050_init()) {
        pedometer_init();
        printf("√ MPU6050 OK\r\n");
    } else {
        printf("X MPU6050 找不到（0x68）\r\n");
    }

    /* MAX30102 */
    if (max30102_init()) {
        printf("√ MAX30102 OK\r\n");
    } else {
        printf("X MAX30102 找不到（0x57）\r\n");
    }
    /* ★★★ 检查点 A：MAX30102 刚初始化完，FIFO_WR 应该在变 */
    {
        uint8_t w1 = 0xEE, w2 = 0xEE;
        (void)i2c_read_reg(0x57, 0x04, &w1);   /* FIFO_WR */
        delay_ms(200);
        (void)i2c_read_reg(0x57, 0x04, &w2);
        printf("   [检查点A 刚初始化完] FIFO_WR: %u -> %u  %s\r\n",
               w1, w2, (w1 != w2) ? "√在采样" : "★没采样");
    }

    /* DS18B20 */
    ds18b20_init();
    printf("√ DS18B20 已初始化\r\n");

    /* ★★★ 检查点 B：DS18B20 初始化之后 */
    {
        uint8_t w1 = 0xEE, w2 = 0xEE;
        (void)i2c_read_reg(0x57, 0x04, &w1);
        delay_ms(200);
        (void)i2c_read_reg(0x57, 0x04, &w2);
        printf("   [检查点B DS18B20之后] FIFO_WR: %u -> %u  %s\r\n",
               w1, w2, (w1 != w2) ? "√在采样" : "★没采样");
    }

    /* ESP32 */
    g_esp_ok = esp_init();

    /* ★★★ 检查点 C：ESP32 初始化之后（这一步耗时约 12 秒）*/
    {
        uint8_t w1 = 0xEE, w2 = 0xEE;
        (void)i2c_read_reg(0x57, 0x04, &w1);
        delay_ms(200);
        (void)i2c_read_reg(0x57, 0x04, &w2);
        printf("   [检查点C ESP32之后] FIFO_WR: %u -> %u  %s\r\n",
               w1, w2, (w1 != w2) ? "√在采样" : "★没采样（预期，稍后会重新初始化）");
    }

    /* ========================================================================
     * ★★★ 2026-10-06 关键修复：MAX30102 在 ESP32 之后【重新初始化】
     *
     * 【实测证据】
     *   [检查点A 刚初始化完]     FIFO_WR: 3 -> 8   √在采样
     *   [检查点B DS18B20之后]    FIFO_WR: 正常
     *   [检查点C ESP32之后]      FIFO_WR: 0 -> 0   ★★ 停了！
     *
     * 【原因】
     *   esp_init() 耗时约 12 秒（6 条 AT 指令 + 等待回显）。
     *   这期间传感器以 25Hz 持续采样 = 300 个样本，
     *   而 FIFO 只有 32 深 → 反复溢出，OVF_COUNTER 饱和到 31，
     *   写指针停在 0，传感器不再往 FIFO 写数据。
     *   → 主循环读到的永远是 wr=0、n=0、样本数 0。
     *
     * 【修法】
     *   把 MAX30102 的初始化【挪到所有慢速初始化之后、进主循环之前】，
     *   让它初始化完立刻开始被读，不给它闲置溢出的机会。
     *   （原来的位置在 DS18B20 之前、ESP32 之前，正是问题所在。）
     * ======================================================================== */
    printf("\r\n★ 重新初始化 MAX30102（放在 ESP32 之后，避免 FIFO 闲置溢出）\r\n");
    if (max30102_init()) {
        printf("√ MAX30102 重新初始化成功\r\n");
    } else {
        printf("X MAX30102 重新初始化失败\r\n");
    }
    {
        uint8_t w1 = 0xEE, w2 = 0xEE;
        (void)i2c_read_reg(0x57, 0x04, &w1);
        delay_ms(200);
        (void)i2c_read_reg(0x57, 0x04, &w2);
        printf("   [验证] FIFO_WR: %u -> %u  %s\r\n\r\n",
               w1, w2, (w1 != w2) ? "√√√ 在采样了！" : "★★ 还是没采样");
    }

    /* 准备就绪 */
    if (i2c_probe(OLED_ADDR) == I2C_OK) {
        oled_clear();
        oled_draw_str(0, 2, "Ready!");
        oled_draw_str(0, 4, "Put finger on");
        oled_draw_str(0, 5, "the sensor");
        oled_refresh();
    }

    printf("\r\n===== 开始运行 =====\r\n");
    printf("每 2 秒上传一次数据到手机\r\n\r\n");

    /* ---------- 主循环（基准 10ms 一拍） ---------- */
    while (1) {
        /* tick++;  ★ 已废弃 */

        /* --- 每 10ms：读心率血氧（内部按 FIFO 处理） --- */
        max30102_update();

        /* --- ★ 每 25ms：计步（用 millis() 精确计时）---
         *   计步算法按 25ms 采样周期设计（40Hz）。
         *   原来用 tick%5 计时，主循环 45ms 周期下实际是 225ms 才采一次（4.4Hz），
         *   远低于算法要求，会导致漏计步。 */
        if ((millis() - s_last_ped) >= 25) {
            s_last_ped = millis();
            g_steps = pedometer_update();
        }

        /* --- 每 100ms（10 拍） --- */
        /* ★ 温度状态机：每拍都调（内部按 tick 计时）*/
        ds_task();

        /* --- ★ 每 100ms：取一次传感器数据快照 + 告警判断 --- */
        if ((millis() - s_last_snap) >= 100) {
            s_last_snap = millis();
            g_max = max30102_get();

            /* ★★★ 告警判断
             *
             * 2026-10-06 修复：原来直接传 g_temp_valid，
             * 结果探头放桌上（测室温 25℃）时被判成"体温过低"，
             * 蜂鸣器 150ms 响/150ms 停无限循环 → 用户反馈"蜂鸣器一直响"。
             *
             * 现在加一层判断：体温低于 TEMP_VALID_MIN(33.0℃) 时，
             * 认为探头没接触人体，这次读数【不参与报警】，也不显示为有效值。 */
            {
                uint8_t tv = g_temp_valid;
                if (tv && (g_temp10 < TEMP_VALID_MIN)) tv = 0;      /* ★ 探头未接触 */
                g_alert = alert_update(g_max.hr, g_max.hr_valid, g_temp10, tv);
            }

            /* 告警状态变化时打印一次（避免刷屏） */
            if (g_alert != ALERT_NONE && !alert_printed) {
                printf("★ 告警: %s  (心率 %u, 体温 %d.%d)\r\n",
                       alert_text(g_alert), g_max.hr,
                       (int)(g_temp10 / 10), (int)(g_temp10 % 10));
                alert_printed = 1;
            } else if (g_alert == ALERT_NONE) {
                if (alert_printed) printf("  告警解除\r\n");
                alert_printed = 0;
            }

            /* ★★★ 2026-10-06 新增：每 5 秒打印一次内部状态，方便诊断蜂鸣器
               如果告警状态是"正常"但蜂鸣器还响，那就是硬件/接线问题。 */
#if DETAILED_DEBUG
            if ((millis() - s_last_diag) >= 5000) {
                s_last_diag = millis();
                uint8_t tv_dbg = g_temp_valid;
                if (tv_dbg && (g_temp10 < TEMP_VALID_MIN)) tv_dbg = 0;

                printf("    [状态] 告警=%s | 心率有效=%u(%u) | 体温有效=%u(%d.%d) | 血氧有效=%u(%u)\r\n",
                       alert_text(g_alert),
                       g_max.hr_valid, g_max.hr,
                       tv_dbg,
                       (g_temp10 == DS18B20_ERR) ? 0 : (int)(g_temp10 / 10),
                       (g_temp10 == DS18B20_ERR) ? 0
                           : (int)((g_temp10 % 10 < 0) ? -(g_temp10 % 10) : (g_temp10 % 10)),
                       g_max.spo2_valid, g_max.spo2);

                /* ★★★ 2026-10-06 新增：MAX30102 算法内部状态
                   用来诊断"为什么 09_main 出不来心率而 23_hr_debug 可以" */
                {
                    max30102_dbg_t dg;
                    max30102_get_debug(&dg);
                    printf("    [MAX] 样本=%lu | IRdc=%ld | ACmax=%ld | 峰数=%u\r\n",
                           (unsigned long)dg.sample_cnt,
                           (long)dg.ir_dc,
                           (long)dg.ac_max_ever,
                           dg.peak_cnt);
                    /* ★★★ 2026-10-06 细化：定位"样本数一直是 0"卡在哪一步 */
                    printf("    [I2C] update调用=%lu | WR失败=%lu RD失败=%lu DATA失败=%lu | 成功样本=%lu\r\n",
                           (unsigned long)dg.upd_calls,
                           (unsigned long)dg.upd_fail_wr,
                           (unsigned long)dg.upd_fail_rd,
                           (unsigned long)dg.upd_fail_data,
                           (unsigned long)dg.upd_ok);
                    printf("    [FIFO] wr=%u rd=%u n=%u | 溢出=%u | 手指=%u\r\n",
                           dg.upd_last_wr, dg.upd_last_rd, dg.upd_last_n,
                           dg.fifo_ovf, g_max.finger);
                    /* ★★ 初始化读回验证：配置有没有真正写进芯片 */
                    printf("    [初始化读回] ID=%02X MODE=%02X SPO2=%02X LED=%02X FIFO=%02X | WR1=%u WR2=%u %s\r\n",
                           dg.init_partid, dg.init_mode, dg.init_spo2,
                           dg.init_led, dg.init_fcfg,
                           dg.init_wr1, dg.init_wr2,
                           (dg.init_wr1 != dg.init_wr2) ? "★在采样" : "★没采样");
                    /* ★★★ 现场对照读：诊断时刻临时读一次，和 update() 里存的对比 */
                    printf("    [对照] 现场读 wr=%u rd=%u  |  update里存 wr=%u rd=%u   %s\r\n",
                           dg.live_wr, dg.live_rd,
                           dg.upd_last_wr, dg.upd_last_rd,
                           (dg.live_wr != dg.upd_last_wr) ? "两者不一致（正常，读数在变）"
                                                          : "两者一致");

                    /* ★★★ 2026-10-06 新增：实测采样率
                     * 算法里的 SAMPLE_RATE 常量必须和实际一致，否则心率会按比例缩放。
                     * 这里用"两次诊断之间的样本增量 ÷ 时间"算出来。 */
                    {
                        static uint32_t last_cnt = 0;
                        static uint32_t last_ms  = 0;
                        static uint8_t  first = 1;

                        if (first) {
                            first = 0;
                            last_ms = millis();
                            printf("    [采样率] 第一次测量，建立基准（样本=%lu）\r\n",
                                   (unsigned long)dg.sample_cnt);
                        } else {
                            uint32_t dcnt = dg.sample_cnt - last_cnt;
                            uint32_t dms  = millis() - last_ms;

                            /* ★ 2026-10-06 改进：不再假设 tick 周期。
                               直接报告"多少拍增加了多少样本"，并反推主循环周期。
                               真实采样率由芯片配置决定（SPO2_SR=100Hz ÷ SMP_AVE=4 = 25Hz），
                               所以只要算出的周期合理，就说明读数正常。 */
                            double rate = (dms > 0) ? ((double)dcnt * 1000.0 / (double)dms) : 0.0;
                            printf("    [采样率] %lums 内样本 +%lu  →  ★ %.1f 样本/秒\r\n",
                                   (unsigned long)dms, (unsigned long)dcnt, rate);
                            printf("             期望 25.0 样本/秒  %s\r\n",
                                   (rate >= 22.0 && rate <= 28.0) ? "√ 一致"
                                   : "★★ 不一致，检查读 FIFO 是否及时");
                        }
                        last_cnt = dg.sample_cnt;
                        last_ms  = millis();
                    }
                }
            }

#else  /* !DETAILED_DEBUG */
        /* ★ 正式版：每 2 秒输出一行精简状态（干净、不刷屏）*/
        if ((millis() - s_last_diag) >= 2000) {
            s_last_diag = millis();
            printf("[状态] HR=%u%s SPO2=%u%s TEMP=%d.%d%s STEP=%lu  样本=%lu  手指=%u  网络=%s\r\n",
                   g_max.hr,   g_max.hr_valid   ? "" : "(无效)",
                   g_max.spo2, g_max.spo2_valid ? "" : "(无效)",
                   (g_temp10 == DS18B20_ERR) ? 0 : (int)(g_temp10 / 10),
                   (g_temp10 == DS18B20_ERR) ? 0
                       : (int)((g_temp10 % 10 < 0) ? -(g_temp10 % 10) : (g_temp10 % 10)),
                   (g_temp10 == DS18B20_ERR) ? "(无效)" : "",
                   (unsigned long)g_steps,
                   (unsigned long)max30102_get_debug_cnt(),
                   g_max.finger,
                   g_esp_ok ? "OK" : "--");
        }
#endif  /* DETAILED_DEBUG */
        }

        /* ====================================================================
         * ★★★ 2026-10-06 修复：OLED 改成【分页刷新】
         *
         * 【原来出的问题】
         *   整屏刷 8 页 = 1024 字节，软件 I2C 25kHz 下要 374 毫秒。
         *   这 374ms 里主循环完全卡住，MAX30102 的 FIFO 积压样本，
         *   再叠加 ESP 上传的阻塞，FIFO（深度 32）会溢出 → 丢数据
         *   → 心率算法时间基准错乱 → hr_valid 永远是 0。
         *   （23_hr_debug 主循环紧凑，所以能出数；09_main 出不来就是这个原因）
         *
         * 【修法】拆成两步：
         *   · 每 500ms 重建一次显存内容（oled_build，纯内存操作，几乎不耗时）
         *   · 每 60ms 只发送 1 页（约 46ms），8 次刷完一轮
         * ==================================================================== */
        /* ====================================================================
         * ★★★ 2026-10-06 修复：OLED 刷新与显存重建【同步】
         *
         * 【原来出的问题】用户反馈"温度显示上下一半，下面显示了个 thesor"。
         *   原来是两个独立计时器：
         *     · 每 500ms 重建显存并把页号复位为 0
         *     · 每 40ms 刷一页（8 页要 320ms）
         *   重建可能发生在刷到第 5 页时 → 页号被强行复位
         *   → 第 5、6、7 页还是【上一代的显存内容】
         *   → 屏幕上出现"两代画面混合"：汉字只显示一半、字符错乱看着像乱码。
         *
         * 【修法】改成【刷完一整轮（8 页）才重建显存】：
         *   状态机：刷 page0 → page1 → ... → page7 → 重建 → 再从头刷
         *   这样屏幕上任何时刻都是同一代内容，不会再混合。
         *   一轮 = 8 × 40ms = 320ms，数据刷新率也是 320ms，足够实时。
         * ==================================================================== */
        if ((millis() - s_last_page) >= 40) {
            s_last_page = millis();

            oled_refresh_page(s_oled_page);

            s_oled_page++;
            if (s_oled_page >= 8) {
                s_oled_page = 0;
                oled_build();               /* ★ 一轮刷完，立刻重建显存 */
            }

            max30102_update();              /* ★ 刷完一页补喂 FIFO，避免积压 */
        }

        /* --- ★ 每 2 秒：上传到手机（用 millis() 精确计时）--- */
        if ((millis() - s_last_send) >= 2000) {
            s_last_send = millis();
            n_send++;

            /* 拼数据行 */
            {
                uint8_t hr   = g_max.hr_valid   ? g_max.hr   : 0;
                uint8_t spo2 = g_max.spo2_valid ? g_max.spo2 : 0;
                int     t10  = g_temp_valid ? (int)g_temp10 : 0;

                sprintf(line, "$HR=%u,SPO2=%u,TEMP=%d.%d,STEP=%u\r\n",
                        hr, spo2,
                        t10 / 10,
                        (t10 % 10 < 0) ? -(t10 % 10) : (t10 % 10),
                        (unsigned)g_steps);
            }

            printf("[%u] %s", (unsigned)n_send, line);

            /* ★ 上传前后各补喂一次 FIFO，减少积压 */
            max30102_update();
            esp_send_data(line);
            max30102_update();
        }

    /* ========================================================================
     * ★★★ 2026-10-06 新增：MAX30102 看门狗
     *
     * 万一传感器又停了（比如长时间没读导致 FIFO 溢出），自动重新初始化，
     * 不用手动重启。每 10 秒检查一次"成功样本数有没有增长"。
     * ======================================================================== */
        if ((millis() - s_last_wd) >= 10000) {
            s_last_wd = millis();
            max30102_dbg_t dg;
            static uint32_t last_ok = 0;
            max30102_get_debug(&dg);

            if (dg.upd_ok == last_ok && dg.upd_calls > 100) {
                /* ★ 10 秒里一个样本都没读到 → 传感器停了，重新初始化 */
                printf("★★ MAX30102 好像停了（成功样本数没涨），重新初始化...\r\n");
                max30102_init();
                delay_ms(200);
                max30102_get_debug(&dg);
                printf("   重新初始化后：");
                {
                    uint8_t w1 = 0xEE, w2 = 0xEE;
                    (void)i2c_read_reg(0x57, 0x04, &w1);
                    delay_ms(200);
                    (void)i2c_read_reg(0x57, 0x04, &w2);
                    printf("FIFO_WR %u -> %u  %s\r\n",
                           w1, w2, (w1 != w2) ? "√恢复了" : "★还是不行");
                }
            }
            last_ok = dg.upd_ok;
        }

        /* --- 板载 LED 心跳（每 1 秒闪一下） --- */
        /* ====================================================================
         * ★★★ 2026-10-07 按键处理（带软件消抖 + 短按/长按区分）
         *
         * 接线：按键一端 → 对应引脚，另一端 → GND（按下 = 低电平）
         *
         *   KEY1 (PA1)  短按 → 消音 10 秒
         *               长按 → 重新初始化 ESP32 网络
         *   KEY2 (PA4)  短按 → 立即上传数据到手机
         *               长按 → 重新开始测量（重置 PPG 算法）
         *   KEY3 (PA5)  短按 → 显示系统信息 3 秒
         *               长按 → 取消静音
         *
         * ★ 消抖在 bsp.c 的 key_get_event() 里做（连续 3 次采样一致才算）
         * ★ 长按判定 3 秒，只报一次事件
         * ==================================================================== */
        /* ====================================================================
         * ★★★ 2026-10-07 按键处理（松手后判定 + 亮灯反馈）
         *
         * 【亮灯时机】用户要求"按完之后马上亮"：
         *   按住期间【不亮】，松手后执行功能 + ★ 板载 LED 亮 1 秒。
         *
         * 【用哪个灯】板载 LED（PC13），不用红色 LED ——
         *   红灯是告警专用（任务书要求"蜂鸣器 + 红灯"），
         *   混用会分不清"我按到了"还是"设备报警"。
         * ==================================================================== */
        {
            uint8_t e1 = key_get_event(1);
            uint8_t e2 = key_get_event(2);
            uint8_t e3 = key_get_event(3);

            /* ★ 任何按键松手并触发事件（短按或长按）→ 亮灯 1 秒 */
            if (e1 != 0 || e2 != 0 || e3 != 0) {
                s_key_led_until = millis() + 1000;
            }

            /* ---------------- KEY1 ---------------- */
            if (e1 == 1) {                          /* 短按：消音 */
                alert_mute(10000);
                s_key_msg    = 1;
                printf("\r\n  [KEY1 短按] ★ 消音 10 秒（红灯照闪）\r\n");
            } else if (e1 == 2) {                   /* 长按：重启网络 */
                printf("\r\n  [KEY1 长按] ★ 重新初始化 ESP32 网络...\r\n");
                s_key_msg = 4;
                g_esp_ok = esp_init();
                printf("  [KEY1 长按] 网络%s\r\n", g_esp_ok ? "已恢复" : "仍未连接");
            }

            /* ---------------- KEY2 ---------------- */
            if (e2 == 1) {                          /* 短按：立即上传 */
                s_last_send  = 0;
                s_key_msg    = 2;
                printf("\r\n  [KEY2 短按] ★ 立即上传数据到手机\r\n");
            } else if (e2 == 2) {                   /* 长按：重新测量 */
                max30102_reset_algo();
                printf("\r\n  [KEY2 长按] ★ 重新开始测量（PPG 算法已重置）\r\n");
                s_key_msg = 5;
            }

            /* ---------------- KEY3 ---------------- */
            if (e3 == 1) {                          /* 短按：系统信息 */
                max30102_dbg_t dg;
                max30102_get_debug(&dg);
                s_info_until = millis() + 3000;
                s_key_msg    = 3;
                printf("\r\n  [KEY3 短按] ★ 系统信息\r\n");
                printf("         样本数=%lu  溢出=%u  峰数=%u\r\n",
                       (unsigned long)dg.sample_cnt, dg.fifo_ovf, dg.peak_cnt);
                printf("         WiFi=%s  告警=%s  静音=%s\r\n",
                       g_esp_ok ? "已连接" : "未连接",
                       alert_text(g_alert),
                       alert_is_muted() ? "是" : "否");
            } else if (e3 == 2) {                   /* 长按：取消静音 */
                alert_mute(0);
                s_key_msg = 6;
                printf("\r\n  [KEY3 长按] ★ 取消静音\r\n");
            }
        }

        /* --- 板载 LED：★ 按键反馈优先，否则走心跳 --- */
        if (millis() < s_key_led_until) {
            led_onboard(1);                     /* ★ 按键反馈：常亮 1 秒 */
        } else {
            led_onboard(((millis() - s_last_led) < 60) ? 1 : 0);   /* 心跳：每 1 秒闪 60ms */
            if ((millis() - s_last_led) >= 1000) s_last_led = millis();
        }

        /* --- 10ms 基准延时 --- */
        for (i = 0; i < 10; i++) delay_ms(1);
    }
}

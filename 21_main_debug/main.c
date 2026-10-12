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
/* ★ 调试版新增：记录各模块初始化结果 + 运行计数 */
static uint8_t  g_mpu_ok   = 0;
static uint8_t  g_oled_ok  = 0;
static uint8_t  g_ds_ok    = 0;
static uint32_t g_tick_dbg = 0;

/* ==================== DS18B20 非阻塞状态机 ==================== */
#define DS_IDLE     0
#define DS_CONVERT  1
#define DS_READY    2

static uint8_t  s_ds_state = DS_IDLE;
static uint16_t s_ds_timer = 0;

static void ds_task(void)
{
    switch (s_ds_state) {
        case DS_IDLE:
            if (ds18b20_start()) {
                s_ds_state = DS_CONVERT;
                s_ds_timer = 0;
            }
            break;

        case DS_CONVERT:
            s_ds_timer += 100;                  /* 调用周期 100ms */
            if (s_ds_timer >= 800) s_ds_state = DS_READY;
            break;

        case DS_READY:
            g_temp10 = ds18b20_read();
            g_temp_valid = (g_temp10 != DS18B20_ERR) ? 1 : 0;
            s_ds_state = DS_IDLE;
            s_ds_timer = 0;
            break;
    }
}

/* ==================== OLED 显示 ==================== */
/* ==================== ★ 中文数据界面 ====================
 *
 * 布局（4 行，每行：中文标签 + 数值）：
 *      心率   75 bpm
 *      血氧   98 %
 *      温度   36.5 C
 *      步数   1234
 *
 * ★ 汉字 16×16 占 2 页，所以 8 页屏正好放 4 行。
 * ★ 标签固定在列 0~31（2 个汉字），数值从列 40 开始，右边留白对齐。
 * ================================================================= */

static void oled_build(void)
{
    char buf[16];

    oled_clear();

    /* ---------------- 第 1 行：心率 ---------------- */
    oled_draw_cn(0, 0, "心率");
    if (g_max.hr_valid) {
        sprintf(buf, "%u", g_max.hr);
        oled_draw_str(48, 1, buf);
        oled_draw_str(80, 1, "bpm");
    } else {
        oled_draw_cn(48, 0, "无数据");
    }

    /* ---------------- 第 2 行：血氧 ---------------- */
    oled_draw_cn(0, 2, "血氧");
    if (g_max.spo2_valid) {
        sprintf(buf, "%u", g_max.spo2);
        oled_draw_str(48, 3, buf);
        oled_draw_str(74, 3, "%");
    } else {
        oled_draw_cn(48, 2, "无数据");
    }

    /* ---------------- 第 3 行：温度 ---------------- */
    oled_draw_cn(0, 4, "温度");
    if (g_temp10 == -1000) {
        oled_draw_cn(48, 4, "待测量");
    } else {
        /* ★ 整数部分和小数部分分开算，负数也正确 */
        int t  = (int)g_temp10;
        int ti = t / 10;
        int td = t % 10;
        if (td < 0) td = -td;
        sprintf(buf, "%d.%d", ti, td);
        oled_draw_str(48, 5, buf);
        oled_draw_str(84, 5, "C");
    }

    /* ---------------- 第 4 行：步数 ---------------- */
    oled_draw_cn(0, 6, "步数");
    sprintf(buf, "%lu", (unsigned long)g_steps);
    oled_draw_str(48, 7, buf);

    /* ★ 右下角小状态：网络是否连上（不影响四项数据显示）*/
    if (g_esp_ok) {
        oled_draw_str(112, 7, "W");
    }

    /* 不在这里发送 */
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

    if (!esp_cmd_wait(cmd, ">", 400)) {
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
    uint32_t tick = 0;              /* 主循环计数，每次 10ms */
    uint32_t n_send = 0;
    char     line[64];
    uint8_t  i;
    uint8_t  alert_printed = 0;
    uint8_t  s_oled_page = 0;
    uint32_t s_last_page = 0;      /* ★ 上次刷 OLED 一页的时间 */

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
        g_oled_ok = 1; printf("√ OLED   OK\r\n");
    } else {
        printf("X OLED   找不到（0x3C）\r\n");
    }
    delay_ms(800);

    /* MPU6050 */
    if (mpu6050_init()) {
        pedometer_init();
        g_mpu_ok = 1; printf("√ MPU6050 OK\r\n");
    } else {
        printf("X MPU6050 找不到（0x68）\r\n");
    }

    /* MAX30102 */
    if (max30102_init()) {
        printf("√ MAX30102 OK\r\n");
    } else {
        printf("X MAX30102 找不到（0x57）\r\n");
    }

    /* DS18B20 */
    ds18b20_init();
    g_ds_ok = 1; printf("√ DS18B20 已初始化\r\n");

    /* ESP32 */
    g_esp_ok = esp_init();

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
        tick++;
        g_tick_dbg = tick;

        /* --- 每 10ms：读心率血氧（内部按 FIFO 处理） --- */
        max30102_update();

        /* --- 每 50ms（5 拍）：计步 --- */
        if ((tick % 5) == 0) {
            /* ★ 计步算法按 25ms 周期设计，5 拍 = 50ms，
               这里改成每 2.5 拍不好实现，所以用 5 拍 + 算法内部按 25ms 基准 */
            g_steps = pedometer_update();
        }

        /* --- 每 100ms（10 拍） --- */
        if ((tick % 10) == 0) {
            g_max = max30102_get();

            /* 温度状态机（非阻塞） */
            ds_task();

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
            if ((tick % 500) == 0) {
                uint8_t tv_dbg = g_temp_valid;
                if (tv_dbg && (g_temp10 < TEMP_VALID_MIN)) tv_dbg = 0;

                printf("    [状态] 告警=%s | 蜂鸣器=%s | 心率有效=%u(%u) | 体温有效=%u(%d.%d) | 血氧有效=%u(%u)\r\n",
                       alert_text(g_alert),
                       alert_is_active() ? "★ 响" : "静音",
                       g_max.hr_valid, g_max.hr,
                       tv_dbg, (int)(g_temp10 / 10),
                       (int)((g_temp10 % 10 < 0) ? -(g_temp10 % 10) : (g_temp10 % 10)),
                       g_max.spo2_valid, g_max.spo2);
            }
        }

        /* --- ★ OLED：刷完一轮（8 页）才重建显存，保证画面不混合 ---
         *   原来"每 500ms 重建 + 每 60ms 刷一页"是两个独立计时器，
         *   重建会打断刷新 → 屏幕上出现两代内容混合（汉字只显示一半、看着像乱码）。*/
        if ((millis() - s_last_page) >= 40) {
            s_last_page = millis();
            oled_refresh_page(s_oled_page);
            s_oled_page++;
            if (s_oled_page >= 8) {
                s_oled_page = 0;
                oled_build();
            }
            max30102_update();
        }

        /* --- 每 2 秒（200 拍）：上传到手机 --- */
        if ((tick % 200) == 0) {
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

            esp_send_data(line);
        }

        /* --- 板载 LED 心跳（每 1 秒闪一下） --- */
        led_onboard(((tick % 100) < 5) ? 1 : 0);

        /* --- 10ms 基准延时 --- */
        for (i = 0; i < 10; i++) delay_ms(1);
    }
}

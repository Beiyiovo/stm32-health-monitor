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
#include "sleep.h"          /* ★ 2026-10-23 睡眠检测 */
#include "alert.h"
#include <string.h>          /* strstr（巴法云私钥判断） */

/* ==================== 巴法云远程模式配置（2026-11-01 新增） ====================
 * 【为什么需要】局域网直连要求手机和设备在同一热点内（人要站在设备旁边）。
 *   远程查看需要云端中转：设备把数据发布到巴法云主题，
 *   小程序随时随地通过 HTTPS 轮询读取（apis.bemfa.com）。
 *
 * 【使用前只需改这 4 行】
 *   ① console.bemfa.com 免费注册 → 控制台复制「私钥」替换 BEMFA_UID
 *   ② 控制台「主题管理」新建主题 health004（协议类型选 TCP）
 *   ③ CLOUD_SSID / CLOUD_PASS 填设备要连的 WiFi（手机热点或路由器，2.4GHz）
 *   不配置或填错都【不影响】本地直连功能，只是远程模式不可用。
 *
 *   小程序端 utils/config.js 的 BEMFA_UID / BEMFA_TOPIC 必须与此处一致。 */

/* ★★★ 2026-11-01 调试开关：0 = 本次编译整体禁用巴法云（不连热点、不发布）
 *   【为什么需要】cloud_reconnect() 是同步阻塞调用，手机热点不在范围时会卡满
 *   AT 超时（实测 26~27 秒），期间主循环停摆、MAX30102 的 FIFO 溢出，会把心率
 *   搞成 0 且长时间不恢复 —— 这个副作用会污染算法本身的验证。
 *   【怎么用】测算法 / 答辩演示本地直连时置 0；需要远程查看时改回 1 重新编译。
 *   置 0 后 cloud_configured() 直接返回 0，所有云端函数在第一行就返回，
 *   既不阻塞主循环，也不影响本地 TCP 与 OLED 显示。 */
#define CLOUD_ENABLE 0

#define BEMFA_UID    "cb8d43645910466bbbcbcefdc48eea70"   /* ←←← 替换成你的巴法云私钥 */
#define BEMFA_TOPIC  "health004"              /* 主题名：仅字母数字，末三位 004=传感器类设备 */
#define CLOUD_SSID   "Ophone X8"  /* ←←← 设备要连接的 WiFi 名称（手机热点/路由器） */
#define CLOUD_PASS   "Echolove"  /* ←←← 对应的 WiFi 密码 */

static uint8_t cloud_configured(void);            /* 用户是否已填写私钥 */
static uint8_t cloud_reconnect(void);             /* 建立云端通道：WiFi→TCP→订阅 */
static void    cloud_publish(const char *line);   /* 把一条协议帧发布到云端 */
static void    cloud_task(void);                  /* 保活 + 断线重连（主循环调用） */

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

/* ★★★ 2026-10-23 新增：睡眠标定打印开关
 *
 * 打开后会【每分钟】往串口打印一行：
 *   [SLEEP] act=42mg now=15mg hr=68 base=72 quiet=3 active=0 st=0
 *
 * 字段含义：
 *   act    = 这一分钟的平均活动量（mg）—— ★ 判"安静"靠它
 *   now    = 最近一次瞬时活动量（mg）
 *   hr     = 当前心率（0 = 无效/手指没放）
 *   base   = 心率基线（清醒时缓慢更新，睡眠中冻结）
 *   quiet  = 已连续多少分钟"安静 + 心率低"
 *   active = 已连续多少分钟"明显活动"
 *   st     = 0 清醒 / 1 睡眠中
 *
 * ★★★ 【怎么用它标定 sleep.c 里的阈值】
 *
 *   第 1 步：清醒静坐，看 act 大概多少 → 记作 A_wake
 *   第 2 步：躺床上不动，看 act 降到多少 → 记作 A_sleep
 *   第 3 步：翻身时看 now 的峰值 → 记作 A_move
 *   第 4 步：清醒静坐看 hr，入睡 10 分钟后再看 hr → 差值记作 D
 *
 *   然后按下面规则调 sleep.c 顶部的宏：
 *     SL_QUIET_THRESH  = (A_sleep + A_wake) / 2     取中间值最稳
 *     SL_ACTIVE_THRESH = A_move / 2                  翻身峰值的 1/2
 *     SL_HR_DROP       = D / 2                       心率降幅的一半
 *
 *   ★ 标定完成后把这个开关改回 0（省 Flash 和串口带宽）*/
#define SLEEP_CALIB_DEBUG  1

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

/* ==================== 辅助：把毫秒格式化成 "H:MM" ====================
 *  ★ 2026-10-23 新增（睡眠时长显示用）
 *
 *  ★ 为什么手动拼字符而不用 sprintf("%02lu")：
 *    Keil 的 printf 库对宽度/补零格式会拉进额外代码（几百字节），
 *    这里只有 5 个字符，手动算更省 Flash。
 * ================================================================= */
static void fmt_dur(char *buf, uint32_t ms)
{
    uint32_t sec = ms / 1000UL;
    uint32_t h   = sec / 3600UL;
    uint32_t mn  = (sec % 3600UL) / 60UL;

    if (h > 99) { h = 99; mn = 59; }        /* 上限保护 */

    if (h >= 10) {
        buf[0] = (char)('0' + (h / 10));
        buf[1] = (char)('0' + (h % 10));
        buf[2] = ':';
        buf[3] = (char)('0' + (mn / 10));
        buf[4] = (char)('0' + (mn % 10));
        buf[5] = '\0';
    } else {
        buf[0] = (char)('0' + h);
        buf[1] = ':';
        buf[2] = (char)('0' + (mn / 10));
        buf[3] = (char)('0' + (mn % 10));
        buf[4] = '\0';
    }
}

/* ==================== ★ CRC8 校验（2026-10-23 新增，额外功能 E6）====================
 *
 *  算法：CRC-8, polynomial=0x07, init=0x00（通用标准）
 *
 *  【为什么加】
 *    串口 + 无线传输都可能出错（尤其 24 小时连续跑、或者线长有干扰）。
 *    原来 App 会照单全收，一个 bit 翻转就可能让心率显示成 191 这种离谱值。
 *    加了 CRC 后，App 可以丢弃坏帧。
 *
 *  【帧格式】
 *    数据行：$HR=76,SPO2=97,TEMP=25.3,STEP=4,SLEEP=0,SDUR=0,UP=3600*3F
 *                                                                     ↑
 *                                                              CRC8 两位十六进制
 *
 *  【★ 兼容性】
 *    App 端做成了「有 * 才校验，没有就跳过」——
 *    所以老固件（没 CRC）配新 App 也能跑，只是没有校验保护。
 * ========================================================================== */

static uint8_t crc8_calc(const char *s, uint16_t len)
{
    uint8_t crc = 0x00;
    uint8_t i;
    while (len--) {
        crc ^= (uint8_t)(*s++);
        for (i = 0; i < 8; i++) {
            if (crc & 0x80) crc = (uint8_t)((crc << 1) ^ 0x07);
            else            crc = (uint8_t)(crc << 1);
        }
    }
    return crc;
}

/**
 * 在字符串末尾追加 CRC 和换行：  ...*XX\r\n
 *  ★ 手动转十六进制（不用 sprintf 的 %02X）—— 省几百字节 printf 库代码
 *  @param buf  已拼好的内容（不含 \r\n）
 *  @param len  buf 里有效字符数
 *  @return 追加后的总长度
 */
static uint16_t append_crc(char *buf, uint16_t len)
{
    static const char HEX[] = "0123456789ABCDEF";
    uint8_t crc = crc8_calc(buf, len);

    buf[len++] = '*';
    buf[len++] = HEX[(crc >> 4) & 0x0F];
    buf[len++] = HEX[crc & 0x0F];
    buf[len++] = '\r';
    buf[len++] = '\n';
    buf[len]   = '\0';
    return len;
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

        /* ★ 2026-10-23：原来 page2 显示 Sample、page4 显示 Peaks（调试用），
         *   改成显示睡眠信息。Overflow 保留（它反映数据质量，有价值）。*/
        {
            sleep_data_t sd;
            sleep_get_data(&sd);

            oled_draw_str(0, 2, "Sleep:");
            oled_draw_str(48, 2, (sd.state == SLEEP_SLEEPING) ? "YES" : "NO");

            oled_draw_str(0, 3, "Dur:");            /* 本次睡眠时长 */
            fmt_dur(buf, (sd.state == SLEEP_SLEEPING) ? sd.sleep_ms : sd.last_dur_ms);
            oled_draw_str(36, 3, buf);

            oled_draw_str(0, 4, "Total:");          /* 开机以来累计 */
            fmt_dur(buf, sd.total_sleep_ms);
            oled_draw_str(48, 4, buf);

            oled_draw_str(0, 5, "Overflow:");       /* 保留：反映 FIFO 丢样本 */
            sprintf(buf, "%u", dg.fifo_ovf);
            oled_draw_str(66, 5, buf);

            oled_draw_str(0, 6, "WiFi:");
            oled_draw_str(42, 6, g_esp_ok ? "OK" : "--");

            oled_draw_str(0, 7, alert_is_muted() ? "MUTE ON" : "MUTE OFF");
        }
        return;                     /* 信息页占满整屏，直接返回 */
    }

    /* ================= 右下角小状态 ================= */
    if (g_alert != ALERT_NONE) {
        oled_draw_str(98,  7, "ALERT")   /* ★ 98+5*6=128，刚好放下 */;
    } else if (sleep_get_state() == SLEEP_SLEEPING) {
        /* ★ 2026-10-23 新增：睡眠中优先显示 SLEEP（WIFI 状态此时不重要）
         *   ★ 详细睡眠时长在 KEY3 系统信息页里看 */
        oled_draw_str(98,  7, "SLEEP")   /* ★ 98+5*6=128，刚好放下 */;
    } else if (g_esp_ok) {
        oled_draw_str(104, 7, "WIFI")    /* ★ 104+4*6=128，刚好放下 */;
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
    printf("  [步骤 1/7] 先听 1.5 秒，看 ESP32 有没有主动说话...\r\n");
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
    printf("\r\n  [步骤 2/7] 发 AT 测试\r\n");
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
    printf("\r\n  [步骤 3/7] 设 AP+STA 双模式（热点＋连 WiFi 共存）\r\n");
    esp_cmd_wait("AT+CWMODE=3", "OK", 3000);
    delay_ms(200);

    printf("\r\n  [步骤 4/7] 建热点（这一步 ESP32 要 1~2 秒）\r\n");
    esp_cmd_wait("AT+CWSAP=\"HealthHelper\",\"12345678\",5,3", "OK", 6000);
    delay_ms(300);

    printf("\r\n  [步骤 5/7] 开多连接\r\n");
    esp_cmd_wait("AT+CIPMUX=1", "OK", 3000);
    delay_ms(200);

    printf("\r\n  [步骤 6/7] 开 TCP 服务器（端口 8266）\r\n");
    /* ★★★ 2026-11-01 新增：先关掉所有残留连接
     *
     * 【为什么】不断电直接烧录（或上次连接没正常断开）时，ESP32 的 conn 0~4
     *   可能还占着。新客户端连进来会被分配到 conn 1、2...，
     *   而本地数据发送固定用 `AT+CIPSEND=0` → 永远失败 → 一帧都发不出去。
     *
     * 【实测表现】WiFi 能连上（信号 90%）、ping 通、TCP connect 成功，
     *   但接收零帧 —— 就是这个原因。断电重启能好，但每次烧录都断电太麻烦。 */
    {
        uint8_t k;
        for (k = 0; k < 5; k++) {
            char cc[24];
            sprintf(cc, "AT+CIPCLOSE=%u", (unsigned)k);
            (void)esp_cmd_wait(cc, "OK", 300);
        }
    }
    esp_cmd_wait("AT+CIPSERVER=1,8266", "OK", 4000);

    /* ---- 步骤 7/7：连接巴法云（远程模式，可选）----
     * 此时尚未初始化 MAX30102（它在所有慢速初始化之后），阻塞安全。
     * 失败不影响后续流程 —— 远程模式只是附加能力。 */
    printf("\r\n  [步骤 7/7] 连接巴法云（远程模式，失败不影响本地功能）\r\n");
    (void)cloud_reconnect();

    ovf = uart2_get_overflow();
    printf("\r\n  √ ESP32 初始化流程走完（接收溢出 %u 次）\r\n", (unsigned)ovf);
    printf("     热点: HealthHelper / 12345678\r\n");
    printf("     手机连: 192.168.4.1:8266\r\n\r\n");
    return 1;
}

/**
 * 把一行数据通过 ESP32 发给手机
 * 流程：AT+CIPSEND=0,<len> → 等 '>' → 发数据 → 等 SEND OK
 */
static int esp_send_data(const char *line)
{
    char cmd[32];
    int  len = (int)strlen(line);
    int  i;

    if (!g_esp_ok) return 0;

    sprintf(cmd, "AT+CIPSEND=0,%d", len);

    if (!esp_cmd_wait(cmd, ">", 400)) {    /* ★ 1500->400ms，避免长时间阻塞导致 FIFO 溢出 */
        /* 没回 '>' —— 多半是手机还没连 */
        return 0;
    }

    uart2_send(line);
    (void)i;

    /* ★ 2026-10-31：把是否收到 SEND OK 返回给调用者，
     *   睡眠事件靠它判断「发出去了才出队」，否则断线会丢事件。 */
    return esp_cmd_wait("", "SEND OK", 2000);
}

/* ==================== 巴法云远程模式（2026-11-01 新增） ====================
 *
 * 数据通路：STM32 --串口--> ESP32 --TCP--> bemfa.com:8344 --> 云主题
 *                                    小程序 --HTTPS--> apis.bemfa.com 轮询读取
 *
 * 官方协议要点（cloud.bemfa.com/docs/src/tcp.html）:
 *   · 指令以 \r\n 结尾；订阅 cmd=1，发布 cmd=2
 *   · 连接后必须尽快订阅主题，否则服务器会断开连接
 *   · 任意数据都算心跳，超过 65 秒无数据会掉线 —— 本工程 6 秒发布一次
 *     天然满足；断网恢复后由 cloud_task 自动重连
 *   · 主题名仅限字母数字，末三位数字决定设备类型（004=传感器）
 *
 * 连接号说明：CIPMUX=1 下共 0~4 五个连接。本地 TCP 服务器把接入的手机
 * 分到 0~3，云端固定用 4，互不冲突。 */
static uint8_t  g_cloud_ok       = 0;   /* 云端通道就绪 */
static uint32_t g_last_cloud_ms  = 0;   /* 上次向云端成功发送的时间 */
static uint32_t g_last_cloud_try = 0;   /* 上次尝试重连的时间 */
static uint8_t  g_cloud_fail     = 0;   /* 连续发布失败计数 */
/* ★★★ 2026-11-01 新增：开机重连次数计数（最多 3 次，失败不重试）
 *   原因：cloud_reconnect() 是同步阻塞，热点不在范围会卡满 AT 超时（实测 27 秒），
 *   期间主循环停摆、FIFO 溢出，直接把心率搞成 0 且长时间不恢复。 */
static uint8_t  g_cloud_try      = 0;   /* 已尝试连接的次数（0~3） */

/* 私钥仍是模板占位 → 视为未配置，云端流程整体跳过 */
static uint8_t cloud_configured(void)
{
#if !CLOUD_ENABLE
    return 0;                            /* 调试期整体禁用巴法云（见文件头说明） */
#else
    return (strstr(BEMFA_UID, "PLEASE") == NULL);
#endif
}

/* 向指定 TCP 连接号发一段原始文本（自带 \r\n 结尾），1=发送成功 */
static int esp_send_conn(uint8_t conn, const char *buf, int len)
{
    char cmd[32];
    sprintf(cmd, "AT+CIPSEND=%u,%d", (unsigned)conn, len);
    if (!esp_cmd_wait(cmd, ">", 400)) return 0;
    uart2_send(buf);
    return esp_cmd_wait("", "SEND OK", 2000);
}

/* 建立/恢复云端通道：WiFi（未连时）→ bemfa TCP → 订阅主题
 * 任何一步失败都返回 0，由 cloud_task 60 秒后重试。 */
static uint8_t cloud_reconnect(void)
{
    char cmd[128];

    if (!g_esp_ok || !cloud_configured()) return 0;

    /* 1) WiFi：已连时 CWJAP? 回复 +CWJAP:"ssid"；未连回复 No AP。
     *    只有匹配到 No AP 才真的去连 WiFi（已连时查询超时属正常）。 */
    if (esp_cmd_wait("AT+CWJAP?", "No AP", 2000)) {
        printf("  [云] WiFi 未连接，正在连接 \"%s\" ...\r\n", CLOUD_SSID);
        sprintf(cmd, "AT+CWJAP=\"%s\",\"%s\"", CLOUD_SSID, CLOUD_PASS);
        /* 手机热点开关时间不一，给足 25 秒 */
        if (!esp_cmd_wait(cmd, "WIFI CONNECTED", 25000)) {
            printf("  [云] 连 WiFi 失败（热点开着吗？名称密码对吗？）60 秒后重试\r\n");
            return 0;
        }
        delay_ms(500);                      /* 等 DHCP 分配 IP */
    }

    /* 2) 连巴法云 TCP 服务器（连接号 4） */
    sprintf(cmd, "AT+CIPSTART=4,\"TCP\",\"tcp.bemfa.com\",8344");
    if (!esp_cmd_wait(cmd, "CONNECT", 10000)) {
        printf("  [云] 连接 bemfa.com 失败，60 秒后重试\r\n");
        return 0;
    }

    /* 3) 订阅主题（连接后不订阅会被服务器断开） */
    {
        char sub[128];
        int  n = sprintf(sub, "cmd=1&uid=%s&topic=%s\r\n", BEMFA_UID, BEMFA_TOPIC);
        if (!esp_send_conn(4, sub, n)) {
            printf("  [云] 订阅主题失败，60 秒后重试\r\n");
            return 0;
        }
    }

    g_cloud_ok      = 1;
    g_cloud_fail    = 0;
    g_last_cloud_ms = millis();
    printf("  [云] √ 已连接巴法云并订阅主题 %s（远程查看可用）\r\n", BEMFA_TOPIC);
    return 1;
}

/* 把一条协议帧（append_crc 后的 *XX\r\n 结尾）发布到云端。
 * 去掉行尾 CRLF 后作为 msg 内容；未连接时静默跳过（本地功能不受影响）。 */
static void cloud_publish(const char *line)
{
    char pkt[256];
    int  len = (int)strlen(line);
    int  n;

    if (!g_cloud_ok || !cloud_configured()) return;
    while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) len--;
    if (len <= 0) return;

    n = sprintf(pkt, "cmd=2&uid=%s&topic=%s&msg=%.*s\r\n",
                BEMFA_UID, BEMFA_TOPIC, len, line);
    if (n <= 0) return;

    if (esp_send_conn(4, pkt, n)) {
        g_last_cloud_ms = millis();
        g_cloud_fail    = 0;
    } else {
        g_cloud_fail++;
        printf("  [云] 发布失败（连续 %u 次）\r\n", (unsigned)g_cloud_fail);
        if (g_cloud_fail >= 3) {
            g_cloud_ok = 0;                 /* 通道可能已断，交给 cloud_task 重连 */
            printf("  [云] 通道判定失效，60 秒后自动重连\r\n");
        }
    }
}

/* 主循环每拍调用（内部自带节流，不增加正常负载）：
 *   · 已连接且 50 秒没发过数据 → 补一次 ping 保活（服务器 65 秒掉线）
 *   · 未连接 → 每 60 秒完整重试一次（连 WiFi → 连云端 → 订阅）
 * 注意：重试是阻塞操作（最长约 35 秒），期间 MAX30102 FIFO 会积压，
 *       返回后立即补读 + 10 秒看门狗兜底，心率读数短暂恢复后正常。 */
static void cloud_task(void)
{
    if (!g_esp_ok || !cloud_configured()) return;

    if (g_cloud_ok && (millis() - g_last_cloud_ms) > 50000UL) {
        if (esp_send_conn(4, "ping\r\n", 6)) {
            g_last_cloud_ms = millis();
        } else {
            g_cloud_ok = 0;
            printf("  [云] 保活失败，进入重连流程\r\n");
        }
    }

    /* ★★★ 2026-11-01：开机最多连 3 次（esp_init 里 1 次 + 这里 2 次），之后放弃
     *
     * 【为什么改】cloud_reconnect() 是同步阻塞调用：AT+CWJAP 连热点默认等 15 秒，
     *   热点不在范围就卡满超时，加 CXIPSTART + 订阅，一次能卡 27 秒。
     *   实测抓包：21:53:27~21:53:54 零帧，同期 UP 从 1138 涨到 1165——
     *   主循环被这个调用占住、FIFO 只进不出 → 溢出 → 心率掉 0 且难恢复。
     *
     * 【改成】esp_init 步骤7 已试过 1 次，这里最多再试 2 次、每次间隔 60 秒；
     *   3 次都失败就彻底放弃，不再干扰采集与显示。要用云就断电重启。 */
    if (!g_cloud_ok && g_cloud_try < 2 && (millis() - g_last_cloud_try) > 60000UL) {
        g_last_cloud_try = millis();
        g_cloud_try++;
        printf("[云] 重试 %d/2（累计第 %d 次，共 3 次）...\r\n",
               g_cloud_try, g_cloud_try + 1);
        max30102_update();                  /* 重连前先清 FIFO，减少积压 */
        cloud_reconnect();
        max30102_update();
        if (!g_cloud_ok && g_cloud_try >= 2) {
            printf("[云] 三次均失败，本次开机不再重试\r\n");
        }
    }
}

/* ==================== 主程序 ==================== */
int main(void)
{
    /* uint32_t tick = 0;  ★ 已废弃：全部定时改用 millis() 真实时间 */              /* 主循环计数，每次 10ms */
    uint32_t n_send = 0;
    /* ★ 2026-10-23：加睡眠字段(SLEEP/SDUR)后再加 UP（设备运行秒数），
     *   最坏约 78 字符 → 改 96 留余量 */
    char     line[96];
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
    sleep_init();                    /* ★ 2026-10-23 睡眠检测初始化 */

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

            /* ★ 2026-10-23：睡眠检测（体动 + 心率双判据）
             *   · 体动 = mpu6050_get_activity()（动态加速度 mg）
             *   · 心率 = g_max.hr（无效时 hr_valid = 0）
             *   ★ 必须在 pedometer_update() 之后调用，否则活动量还是上一拍的 */
            sleep_update(mpu6050_get_activity(), g_max.hr, g_max.hr_valid);

#if SLEEP_CALIB_DEBUG
            /* ★ 每分钟打印一次睡眠算法内部状态（标定阈值用）
             *   ★ 为什么 60 秒才打一次：睡眠判据本身就是分钟级的，
             *     打印太频繁既没用又会拖慢主循环（串口阻塞约 1~2ms/行）*/
            {
                static uint32_t s_last_sleep_dbg = 0;
                if ((millis() - s_last_sleep_dbg) >= 60000UL) {
                    s_last_sleep_dbg = millis();
                    {
                        sleep_data_t sd;
                        sleep_get_data(&sd);
                        printf("[SLEEP] act=%ldmg now=%ldmg hr=%u base=%u quiet=%u active=%u st=%u\r\n",
                               (long)sd.act_avg, (long)sd.act_now,
                               (unsigned)sd.hr_cur, (unsigned)sd.hr_base,
                               (unsigned)sd.quiet_min, (unsigned)sd.active_min,
                               (unsigned)sd.state);
                    }
                }
            }
#endif
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
                /* ★ 2026-10-23：加了血氧两个参数（额外功能 E5 低血氧告警）*/
    g_alert = alert_update(g_max.hr, g_max.hr_valid, g_temp10, tv,
                           g_max.spo2, g_max.spo2_valid);
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

        /* --- ★ 2026-11-01 新增：云端通道维护（保活/重连，内部节流） --- */
        cloud_task();

        /* --- ★ 每 2 秒：上传到手机（用 millis() 精确计时）--- */
        if ((millis() - s_last_send) >= 2000) {
            s_last_send = millis();
            n_send++;

            /* ========================================================
             * ★ 2026-10-23 新增：先补发【睡眠状态变化事件】（如果有）
             *
             * 【为什么要单独发事件】
             *   STM32 没有 RTC，只知道"开机后第几毫秒入睡了"，
             *   不知道"几点几分"。
             *   → 所以把事件（START/END）+ 相对时间发给 App，
             *     App 用【手机系统时间】打绝对时间戳。
             *
             * 【断线怎么办】
             *   sleep.c 里有 10 条的环形缓存，断线期间事件先存着，
             *   重连后这里会一条条补发出去。
             *
             * ★ 每次只发一条，避免阻塞主循环（事件不会很多，一天也就几次）
             * ======================================================== */
            {
                uint8_t  evt_type = 0;
                uint32_t evt_ms   = 0;
                if (sleep_peek_event(&evt_type, &evt_ms)) {
                    {
                        int n_evt = sprintf(line, "$EVT=%u,T=%lu",
                                            (unsigned)evt_type,
                                            (unsigned long)(evt_ms / 1000UL));
                        if (n_evt > 0 && n_evt < 90) {
                            append_crc(line, (uint16_t)n_evt);
                            cloud_publish(line);  /* ★ 2026-11-01：睡眠事件同步发云端（远程可见） */
                            /* ★ 2026-10-31：发送成功才出队。
                             *   原来 pop 之后无条件发送，断线时这条事件就没了，
                             *   "断线补发"名不副实。改成 peek → 发 → 成功才 pop。*/
                            if (esp_send_data(line)) {
                                sleep_pop_event((uint8_t *)0, (uint32_t *)0);
                                printf("  [睡眠事件] type=%u  t=%lus 已上报\r\n",
                                       (unsigned)evt_type,
                                       (unsigned long)(evt_ms / 1000UL));
                            } else {
                                printf("  [睡眠事件] 没发出去，留在缓存等重连补发\r\n");
                            }
                        } else {
                            /* 长度异常，丢掉这一条避免卡死 */
                            sleep_pop_event((uint8_t *)0, (uint32_t *)0);
                        }
                    }
                }
            }

            /* 拼数据行 */
            {
                uint8_t hr   = g_max.hr_valid   ? g_max.hr   : 0;
                uint8_t spo2 = g_max.spo2_valid ? g_max.spo2 : 0;
                int     t10  = g_temp_valid ? (int)g_temp10 : 0;

                /* ★ 2026-10-23：追加睡眠字段
                 *   SLEEP = 0 清醒 / 1 睡眠中
                 *   SDUR  = 本次睡眠已持续多少秒（非睡眠时为 0）
                 *
                 * ★★ 为什么传"相对时长"而不是"绝对时间"：
                 *   STM32 没有 RTC（全工程无 RTC 代码），拿不到"几点"。
                 *   → App 收到后用【手机系统时间】打绝对时间戳。
                 *   → 断线时 STM32 用事件缓存（sleep_pop_event）补传。*/
                /* ★ 2026-10-23：再追加 UP = 设备开机以来运行了多少秒
                 *
                 * 【App 为什么需要它】
                 *   App 收到睡眠事件 $EVT=1,T=1800（设备第 1800 秒入睡）时，
                 *   要换算成"手机时间几点几分"。
                 *   单纯用"收到时刻"会在断线补发时出错。
                 *   → 有 UP 后，App 可以：
                 *        绝对时间 = 收到本行数据时的手机时间
                 *                 - 本行的UP
                 *                 + 事件的T
                 *     这样即使事件是断线期间产生的，也能算准。*/
                /* ★ 2026-10-23：改用手动加 CRC 的方式（额外功能 E6）
                 *   ★ sprintf 这里【不再带 \r\n】，由 append_crc 统一追加 "*XX\r\n" */
                int n_raw = sprintf(line, "$HR=%u,SPO2=%u,TEMP=%d.%d,STEP=%u,SLEEP=%u,SDUR=%lu,UP=%lu",
                                    hr, spo2,
                                    t10 / 10,
                                    (t10 % 10 < 0) ? -(t10 % 10) : (t10 % 10),
                                    (unsigned)g_steps,
                                    (unsigned)sleep_get_state(),
                                    (unsigned long)(sleep_get_duration_ms() / 1000UL),
                                    (unsigned long)(millis() / 1000UL));
                if (n_raw > 0 && n_raw < 90) {
                    append_crc(line, (uint16_t)n_raw);
                } else {
                    /* 拼装异常（理论上不会发生），至少补上换行别把协议搞乱 */
                    line[0] = '\r'; line[1] = '\n'; line[2] = '\0';
                }
            }

            printf("[%u] %s", (unsigned)n_send, line);

            /* ★ 上传前后各补喂一次 FIFO，减少积压 */
            max30102_update();
            esp_send_data(line);
            max30102_update();

            /* ★ 2026-11-01 新增：每 3 帧（约 6 秒）把最新数据发布到云端。
             *   本地 2 秒一条不变；云端 6 秒一条足够远程实时查看。
             *   云未连接时 cloud_publish 内部直接跳过。 */
            if (n_send % 3 == 0) {
                cloud_publish(line);
                max30102_update();          /* 阻塞返回先补读 FIFO */
            }
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
            } else if (e1 == 2) {                   /* ★ 2026-10-31 改：主动报警 3 秒 */
                /* 原来这里是"重新初始化 ESP32 网络"（g_esp_ok = esp_init()）。
                 *
                 * 【为什么换掉】esp_init() 要跑 6 条 AT 指令，实测耗时约 12 秒，
                 *   期间整机停摆（BUG-6）。而网络真正挂掉的概率很低，
                 *   真挂了断电重插即可恢复（AT 配置在开机初始化时下发）。
                 *   用户判断这个功能"没有意义"，改成主动报警演示。
                 *
                 * 【新功能】按一下响 3 秒 + 红灯闪 3 秒，用来演示报警长什么样。
                 *   ★ 不产生告警事件：OLED 不显示 ALERT，App 也收不到这条记录。
                 *     要测「断线事件缓存（E4）」必须用真实报警（手捂探头升温）。*/
                alert_beep_test(3000);
                s_key_msg = 7;
                printf("\r\n  [KEY1 长按] ★ 主动报警 3 秒（演示用，不产生告警记录）\r\n");
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

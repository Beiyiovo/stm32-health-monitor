/**
 ******************************************************************************
 *  alert.c —— 阈值告警模块（蜂鸣器 + 红色 LED）
 ******************************************************************************
 *  报警方式（★ 非阻塞，不影响主循环）：
 *      蜂鸣器：响 150ms / 停 150ms 间歇鸣叫
 *      红色 LED：跟随蜂鸣器同步闪烁
 ******************************************************************************
 */
#include "alert.h"

#define BEEP_ON_MS      150         /* 响的时间 */
#define BEEP_OFF_MS     150         /* 停的时间 */

static uint8_t  s_type = ALERT_NONE;    /* 当前告警类型 */
static uint32_t s_ms   = 0;             /* 内部毫秒计数 */
static uint8_t  s_phase = 0;            /* 0=响 1=停 */

/* ★★★ 2026-10-06 新增：静音功能（按键消音用）
 *   s_mute_until = 静音截止的 millis() 时间戳；0 或已过期表示不静音 */
static uint32_t s_mute_until = 0;

void alert_init(void)
{
    s_type  = ALERT_NONE;
    s_ms    = 0;
    s_phase = 0;
    s_mute_until = 0;
    buzzer(0);
    led_red(0);
}

/**
 * ★ 设置静音时长（毫秒）；传 0 表示取消静音
 * ★ 静音只关蜂鸣器，红色 LED 仍然闪（保留视觉提醒）
 */
void alert_mute(uint32_t ms)
{
    s_mute_until = (ms == 0) ? 0 : (millis() + ms);
}

uint8_t alert_is_muted(void)
{
    if (s_mute_until == 0) return 0;
    /* millis() 回绕也能正确处理（无符号减法）*/
    if ((millis() - s_mute_until) < 0x80000000UL) return 1;   /* 还没到截止时间 */
    s_mute_until = 0;
    return 0;
}

const char *alert_text(uint8_t type)
{
    switch (type) {
        case ALERT_HR_LOW:    return "心率过低";
        case ALERT_HR_HIGH:   return "心率过高";
        case ALERT_TEMP_LOW:  return "体温过低";
        case ALERT_TEMP_HIGH: return "体温过高";
        default:              return "正常";
    }
}

/* ==================== ★ 是否需要用蜂鸣器 ==================== */

/**
 * ★★★ 2026-10-07 新增（用户要求）
 *
 * 【体温过低只闪灯，不响蜂鸣器】
 *   原因：DS18B20 刚接触人体时，读数从室温（比如 25℃）慢慢升上来，
 *         这个过程必然被判为"体温过低"（< 36.0℃）。
 *         如果蜂鸣器一直响，用户会觉得设备有问题，而且很吵。
 *         实测体验：把探头握在手里，要十几秒才升到 36℃ 以上，
 *         这期间蜂鸣器一直叫，完全没必要。
 *
 *   ★ 体温【过高】仍然要响 —— 那是真异常（发烧），必须听觉提醒。
 *   ★ 心率异常也照常响。
 *
 *   返回：1 = 需要蜂鸣器，0 = 只闪灯
 */
static uint8_t alert_needs_buzzer(uint8_t type)
{
    switch (type) {
        case ALERT_TEMP_LOW:    return 0;   /* ★ 体温过低：只闪红灯 */
        default:                return 1;   /* 其他告警：蜂鸣器 + 红灯 */
    }
}

/* ==================== 周期更新 ==================== */

uint8_t alert_update(uint8_t hr, uint8_t hr_valid, int16_t temp10, uint8_t temp_valid)
{
    uint8_t new_type = ALERT_NONE;

    /* --- 判断告警 --- */
    if (hr_valid) {
        if (hr < HR_MIN)       new_type = ALERT_HR_LOW;
        else if (hr > HR_MAX)  new_type = ALERT_HR_HIGH;
    }

    /* 体温告警优先级更高（会覆盖心率告警） */
    if (temp_valid) {
        if (temp10 < TEMP_MIN)      new_type = ALERT_TEMP_LOW;
        else if (temp10 > TEMP_MAX) new_type = ALERT_TEMP_HIGH;
    }

    s_type = new_type;

    /* --- 驱动蜂鸣器和 LED（非阻塞间歇） --- */
    s_ms += 100;                            /* ★ 调用周期是 100ms */

    if (s_type == ALERT_NONE) {
        buzzer(0);
        led_red(0);
        s_ms = 0;
        s_phase = 0;
    } else {
        if (s_phase == 0) {
            /* ★ 2026-10-06：静音时蜂鸣器不响，但 LED 照样闪（保留视觉提醒）
             * ★ 2026-10-07：体温过低也【不响蜂鸣器】，只闪灯
             *   （探头刚接触时读数从室温升上来，这期间必然"过低"，不是真异常）*/
            if (!alert_is_muted() && alert_needs_buzzer(s_type)) buzzer(1);
            led_red(1);
            if (s_ms >= BEEP_ON_MS) { s_ms = 0; s_phase = 1; }
        } else {
            buzzer(0);
            led_red(0);
            if (s_ms >= BEEP_OFF_MS) { s_ms = 0; s_phase = 0; }
        }
    }

    return s_type;
}

uint8_t alert_is_active(void) { return (s_type != ALERT_NONE) ? 1 : 0; }
uint8_t alert_get_type(void)  { return s_type; }

/* ==================== 上电自检 ==================== */

/**
 * ★★★ 2026-10-06 改：开机自检延长到 3 秒（用户要求）
 *
 * 流程：
 *      ① 蜂鸣器响 + 红 LED 亮，持续 3 秒
 *      ② 停 0.5 秒
 *      ③ 再短响 2 声（"滴滴"），提示自检完成
 *
 * ★ 为什么这么设计：
 *      原来只响 200ms，太快了几乎注意不到，
 *      延长到 3 秒后，一开机就能明确知道"板上电了、蜂鸣器好的、LED 好的"。
 */
void alert_test(void)
{
    uint8_t i;

    /* ① 蜂鸣器响 + 红 LED 亮，持续 3 秒 */
    buzzer(1);
    led_red(1);
    for (i = 0; i < 30; i++) {
        delay_ms(100);
        led_onboard((i % 4) < 2 ? 1 : 0);       /* 板载 LED 同步快闪，更醒目 */
    }
    buzzer(0);
    led_red(0);
    led_onboard(0);
    delay_ms(500);

    /* ② 再短响 2 声 —— 提示"自检完成" */
    for (i = 0; i < 2; i++) {
        buzzer(1);
        led_red(1);
        delay_ms(150);
        buzzer(0);
        led_red(0);
        delay_ms(150);
    }

    delay_ms(200);
}

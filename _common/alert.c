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

/* ★★★ 2026-10-31 新增：体温过高的蜂鸣器最长鸣叫时间
 *
 * 【为什么加这个】
 *   体温探头的热滞后很明显（DS18B20 接触人体后要几十秒才稳），
 *   一旦读数冲过 37.5 ℃，蜂鸣器会一直叫到温度回落为止，
 *   实测握在手里能叫好几分钟，很吵，也不符合"提醒"的定位。
 *   ★ 用户要求：响 3 秒后停蜂鸣器，红灯继续闪。
 *
 * 【恢复条件】
 *   类型一变（比如回到正常、或换成了别的告警）就重置计时，
 *   下次再超限会重新响 3 秒。*/
#define BUZZER_MAX_MS   3000

static uint8_t  s_type = ALERT_NONE;    /* 当前告警类型 */
static uint32_t s_phase_ms = 0;         /* ★ 当前节拍的起始时刻（millis）
                                         *   2026-10-15：原名 s_ms，原来是"内部毫秒计数"（每次 +=100），
                                         *   因实际调用周期约 135ms 而非 100ms，改用真实时间戳 */
static uint8_t  s_phase = 0;            /* 0=响 1=停 */

/* ★ 2026-10-31：本次"超限状态"开始的时刻，用于体温过高的 3 秒判定 */
static uint32_t s_over_start = 0;
static uint8_t  s_prev_type  = ALERT_NONE;

/* ★ 2026-10-31：主动报警测试的截止时刻（KEY1 长按触发） */
static uint32_t s_test_until = 0;

/* ★★★ 2026-10-06 新增：静音功能（按键消音用）
 *   s_mute_until = 静音截止的 millis() 时间戳；0 或已过期表示不静音 */
static uint32_t s_mute_until = 0;

void alert_init(void)
{
    s_type  = ALERT_NONE;
    s_phase_ms = 0;
    s_phase = 0;
    s_mute_until = 0;
    s_over_start = 0;
    s_prev_type  = ALERT_NONE;
    s_test_until = 0;
    buzzer(0);
    led_red(0);
}

/* ==================== ★ 主动报警测试（KEY1 长按） ==================== */

/**
 * ★★★ 2026-10-31 新增
 *
 * 【用途】答辩演示：按一下就响 3 秒，不用真把探头捂到 37.5 ℃。
 *        也可用来验证"蜂鸣器 + 红灯"这一路硬件是通的。
 *
 * 【设计取舍】只驱动蜂鸣器和红灯，【不动 s_type】。
 *   原因：如果伪造一个告警类型，OLED 会显示 ALERT、App 会收到一条假告警记录，
 *         污染历史数据和告警统计。演示归演示，数据归数据。
 *   ★ 所以想测「断线事件缓存（E4）」，要用真实告警（手捂探头升温），不能用这个。
 */
void alert_beep_test(uint32_t ms)
{
    s_test_until = (ms == 0) ? 0 : (millis() + ms);
}

uint8_t alert_is_testing(void)
{
    if (s_test_until == 0) return 0;
    /* 与 alert_is_muted 同一套有符号减法技巧，对 millis() 回绕安全 */
    if ((uint32_t)(millis() - s_test_until) >= 0x80000000UL) return 1;
    s_test_until = 0;
    return 0;
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
    /* ★★★ 2026-10-15 修复：原来的比较方向写反了。
     *
     * 【原 bug】s_mute_until = now + ms，所以"还没到截止时间"意味着 now < until，
     *   此时 (millis() - s_mute_until) 的无符号结果是 2^32 - ms（接近 4294967296 的大数），
     *   ★ 必然 ≥ 0x80000000 → 原条件 (… < 0x80000000) 恒为假
     *   → 判为"未静音"，还顺手把 s_mute_until 清零
     *   → ★★ 消音功能 100% 失效（按了没用，蜂鸣器照响）
     *
     * 【正确判据】用有符号减法：now - until < 0 表示"还没到截止时间"。
     *   无符号减法的结果落在高半区（≥ 0x80000000）等价于有符号的负数，
     *   且对 millis() 回绕同样是正确的。*/
    if ((uint32_t)(millis() - s_mute_until) >= 0x80000000UL) return 1;   /* now < until → 静音中 */
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
        case ALERT_SPO2_LOW:  return "血氧过低";     /* ★ 2026-10-23 */
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

/**
 * ★★★ 2026-10-31 新增：体温过高的"3 秒后闭嘴"
 *
 *   从进入 ALERT_TEMP_HIGH 的那一刻开始计时，满 3 秒后返回 1，
 *   此时蜂鸣器停、红灯继续闪。温度掉回正常后 s_over_start 会重置，
 *   下次再冲高会重新响 3 秒。
 *
 *   ★ 为什么只对体温做：心率/血氧的抖动是秒级的，响 3 秒就停反而会漏掉；
 *     体温一旦上去了会持续很久，一直响没有意义。
 */
static uint8_t alert_beep_expired(void)
{
    if (s_type != ALERT_TEMP_HIGH) return 0;
    return ((millis() - s_over_start) >= BUZZER_MAX_MS) ? 1 : 0;
}

/* ==================== 周期更新 ==================== */

uint8_t alert_update(uint8_t hr, uint8_t hr_valid,
                     int16_t temp10, uint8_t temp_valid,
                     uint8_t spo2, uint8_t spo2_valid)
{
    uint8_t new_type = ALERT_NONE;

    /* --- 判断告警 --- */
    if (hr_valid) {
        if (hr < HR_MIN)       new_type = ALERT_HR_LOW;
        else if (hr > HR_MAX)  new_type = ALERT_HR_HIGH;
    }

    /* 体温告警优先级高于心率（会覆盖心率告警） */
    if (temp_valid) {
        if (temp10 < TEMP_MIN)      new_type = ALERT_TEMP_LOW;
        else if (temp10 > TEMP_MAX) new_type = ALERT_TEMP_HIGH;
    }

    /* ★ 2026-10-23 新增：低血氧告警，优先级【最高】
     *
     * 【为什么优先级最高】
     *   缺氧比心率/体温异常更紧急 —— 血氧掉到 92% 以下说明组织供氧不足，
     *   必须最先提示。
     *
     * 【为什么必须判 valid】
     *   MAX30102 在手指没放上时 spo2 = 0，
     *   如果不看 spo2_valid 就会一直报"血氧过低"，非常吵。
     *   （实测：开机没放手时 spo2 恒为 0）*/
    if (spo2_valid) {
        if (spo2 < SPO2_MIN) new_type = ALERT_SPO2_LOW;
    }

    s_type = new_type;

    /* ★★★ 2026-10-31：告警类型一变，就重置"本次超限"的计时起点。
     *   正常 → 超限、或 A 类超限 → B 类超限，都算新的一轮，重新给 3 秒。*/
    if (new_type != s_prev_type) {
        s_prev_type  = new_type;
        s_over_start = millis();
    }

    /* ★★★ 2026-10-31：主动报警测试（KEY1 长按）
     *   ★ 放在最前面、且在 s_type 判定之后，这样测试期间：
     *     - 蜂鸣器和红灯按正常节拍闪，但【不受静音、体温过低、3 秒超时的影响】
     *     - s_type 保持不变，OLED 与上报数据都不受影响
     *   测试结束后自动落回下面的正常逻辑。*/
    if (alert_is_testing()) {
        if (s_phase == 0) {
            buzzer(1);
            led_red(1);
            if ((millis() - s_phase_ms) >= BEEP_ON_MS)  { s_phase_ms = millis(); s_phase = 1; }
        } else {
            buzzer(0);
            led_red(0);
            if ((millis() - s_phase_ms) >= BEEP_OFF_MS) { s_phase_ms = millis(); s_phase = 0; }
        }
        return s_type;
    }

    /* --- 驱动蜂鸣器和 LED（非阻塞间歇） ---
     *
     * ★★★ 2026-10-15 修复两处：
     *
     * 【修复1】节拍改用 millis() 真实时间
     *   原来用 s_ms += 100 自累加，假设 alert_update() 每 100ms 被调一次。
     *   但实际周期 = 主循环周期（约 45ms）× 触发间隔（>=100ms 才进）
     *   → 实际约 135ms 一拍
     *   → "响 150ms" 因为要凑满 ≥150 需要 2 拍，实际响了约 270ms
     *   → 间歇鸣叫频率从设计的 3.3Hz 掉到约 1.9Hz，占空比还会抖动（150/100=1.5 非整数）
     *   ★ 改用时间戳后，响/停都和实际调用周期完全解耦。
     *
     * 【修复2】静音时【主动关】蜂鸣器
     *   原来只写 `if (!muted && needs) buzzer(1);` —— 静音时既不打开也不关闭，
     *   于是按下消音后蜂鸣器会【保持响】到下一个 phase 切换（约 270ms）才停。
     *   现在补上 else 分支主动关掉。*/
    if (s_type == ALERT_NONE) {
        buzzer(0);
        led_red(0);
        s_phase_ms = millis();              /* 复位节拍基准 */
        s_phase    = 0;
    } else {
        if (s_phase == 0) {
            /* ★ 2026-10-06：静音时蜂鸣器不响，但 LED 照样闪（保留视觉提醒）
             * ★ 2026-10-07：体温过低也【不响蜂鸣器】，只闪灯
             *   （探头刚接触时读数从室温升上来，这期间必然"过低"，不是真异常）
             * ★ 2026-10-31：体温过高响满 3 秒后也【不响蜂鸣器】，只闪灯*/
            if (!alert_is_muted() && alert_needs_buzzer(s_type) && !alert_beep_expired()) {
                buzzer(1);
            } else {
                buzzer(0);   /* ★ 主动关 */
            }
            led_red(1);
            if ((millis() - s_phase_ms) >= BEEP_ON_MS)  { s_phase_ms = millis(); s_phase = 1; }
        } else {
            buzzer(0);
            led_red(0);
            if ((millis() - s_phase_ms) >= BEEP_OFF_MS) { s_phase_ms = millis(); s_phase = 0; }
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

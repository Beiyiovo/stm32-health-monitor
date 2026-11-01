/**
 ******************************************************************************
 *  sleep.c —— 睡眠检测（体动 + 心率双判据）
 ******************************************************************************
 *  ★ 2026-10-23 新增。算法依据见 sleep.h 顶部说明。
 ******************************************************************************
 */
#include "sleep.h"

/* ============================================================================
 * ★ 可标定的参数（★ 这些值需要按实际使用场景实测调整，不是定论）
 *
 * 【怎么标定】
 *   ① 安静阈值 SL_QUIET_THRESH：
 *      设备放床头，人清醒静坐时看串口打印的 act_avg，
 *      人躺下入睡后再看一遍 → 两者之间取一个值
 *   ② 活动阈值 SL_ACTIVE_THRESH：
 *      翻身/起床时看 act_avg 的峰值 → 取其 1/2 左右
 *   ③ SL_HR_DROP：
 *      清醒静坐心率 vs 入睡后心率，差值的 1/2 左右
 * ========================================================================== */
#define SL_QUIET_THRESH     50      /* 平均活动量 < 50 mg → 判"安静" */
#define SL_QUIET_NOHR       35      /* 心率无效时（手指没放）用的安静阈值，比有心率时严但不至于测不到 */
#define SL_ACTIVE_THRESH    150     /* 平均活动量 > 150 mg → 判"明显活动" */
#define SL_SLEEP_MIN        10      /* 连续 10 分钟满足条件 → 判定入睡 */
#define SL_WAKE_MIN         1       /* 连续 1 分钟明显活动 → 判定醒来 */
#define SL_HR_DROP          8       /* 心率比基线低 8 bpm → 算"心率降低" */

#define SL_TICK_MS          (60UL * 1000UL)   /* 1 分钟统计一次 */
#define SL_HR_BASE_MIN      50      /* 心率基线钳位下限 */
#define SL_HR_BASE_MAX      110     /* 心率基线钳位上限 */

/* ==================== 静态状态 ==================== */
static sleep_data_t s_d;

static int32_t  s_act_sum = 0;      /* 本 tick 内活动量累加 */
static uint16_t s_act_cnt = 0;      /* 本 tick 内采样次数 */
static uint32_t s_tick_ms = 0;      /* 上一次 tick 的时刻 */

/* 事件环形缓冲 */
static uint8_t  s_evt_type[SLEEP_EVT_MAX];
static uint32_t s_evt_ms[SLEEP_EVT_MAX];
static uint8_t  s_evt_head = 0;     /* 写位置 */
static uint8_t  s_evt_tail = 0;     /* 读位置 */
static uint8_t  s_evt_cnt  = 0;     /* 当前条数 */

/* ==================== 内部：压入一个事件 ==================== */
static void evt_push(uint8_t type, uint32_t t)
{
    if (s_evt_cnt >= SLEEP_EVT_MAX) {
        /* ★ 缓存满 → 丢弃最老的一条（保证最新事件能存下） */
        s_evt_tail = (uint8_t)((s_evt_tail + 1) % SLEEP_EVT_MAX);
        s_evt_cnt--;
    }
    s_evt_type[s_evt_head] = type;
    s_evt_ms[s_evt_head]   = t;
    s_evt_head = (uint8_t)((s_evt_head + 1) % SLEEP_EVT_MAX);
    s_evt_cnt++;
    s_d.evt_cnt = s_evt_cnt;
}

/* ==================== 内部：每分钟执行一次判据 ==================== */
static void do_tick(void)
{
    int32_t act;
    uint8_t hr_ok;
    uint32_t now = millis();

    /* --- 1. 算这一分钟的平均活动量 --- */
    if (s_act_cnt > 0) act = s_act_sum / (int32_t)s_act_cnt;
    else               act = 0;
    s_act_sum = 0;
    s_act_cnt = 0;
    s_d.act_avg = act;

    if (s_d.tick_min < 255) s_d.tick_min++;

    /* --- 2. 维护心率基线（★ 只在清醒时更新，睡眠中冻结）--- */
    hr_ok = (s_d.hr_valid && s_d.hr_cur >= 30 && s_d.hr_cur <= 200) ? 1 : 0;

    if (s_d.state == SLEEP_AWAKE) {
        if (hr_ok) {
            if ((int32_t)s_d.hr_cur > (int32_t)s_d.hr_base) {
                s_d.hr_base = s_d.hr_cur;            /* 立即跟上更高的 */
            } else if (s_d.hr_base > SL_HR_BASE_MIN) {
                s_d.hr_base--;                        /* 每 tick 缓慢衰减 1 bpm */
            }
            if (s_d.hr_base < SL_HR_BASE_MIN) s_d.hr_base = SL_HR_BASE_MIN;
            if (s_d.hr_base > SL_HR_BASE_MAX) s_d.hr_base = SL_HR_BASE_MAX;
        }
    }

    /* --- 3. 状态机 --- */
    if (s_d.state == SLEEP_AWAKE) {
        /* 入睡判据：安静 + 心率降低（心率无效时只用体动，且要求更安静）*/
        uint8_t quiet_ok;
        if (hr_ok) {
            quiet_ok = (act < SL_QUIET_THRESH &&
                        s_d.hr_cur + SL_HR_DROP <= s_d.hr_base) ? 1 : 0;
        } else {
            /* ★ 心率无效（手指没放）→ 只靠体动，用更严的阈值，且要求基线已建立 */
            quiet_ok = (act < SL_QUIET_NOHR && s_d.tick_min >= 3) ? 1 : 0;
        }

        if (quiet_ok) {
            if (s_d.quiet_min < 255) s_d.quiet_min++;
            if (s_d.quiet_min >= SL_SLEEP_MIN) {
                /* ---- 判定入睡 ---- */
                s_d.state      = SLEEP_SLEEPING;
                s_d.start_ms   = now;
                s_d.sleep_ms   = 0;
                s_d.quiet_min  = 0;
                s_d.active_min = 0;
                if (s_d.sess_cnt < 65535) s_d.sess_cnt++;
                evt_push(SLEEP_EVT_START, now);
            }
        } else {
            /* ★ 单分钟超阈值不清零，只回退一格：真实睡眠中总会翻身，
               要求连续 10 分钟一动不动是不现实的，清零会让判据几乎永不触发 */
            if (s_d.quiet_min > 0) s_d.quiet_min--;
        }
    } else {
        /* 醒来判据：出现明显活动 */
        if (act > SL_ACTIVE_THRESH) {
            if (s_d.active_min < 255) s_d.active_min++;
            if (s_d.active_min >= SL_WAKE_MIN) {
                /* ---- 判定醒来 ---- */
                s_d.state       = SLEEP_AWAKE;
                s_d.last_dur_ms = (now >= s_d.start_ms) ? (now - s_d.start_ms) : 0;
                s_d.total_sleep_ms += s_d.last_dur_ms;
                s_d.last_end_ms = now;
                s_d.sleep_ms    = s_d.last_dur_ms;
                s_d.active_min  = 0;
                s_d.quiet_min   = 0;
                evt_push(SLEEP_EVT_END, now);
            }
        } else {
            /* 同理，未达活动阈值只回退一格，避免一次轻微动作就重置清醒计数 */
            if (s_d.active_min > 0) s_d.active_min--;
        }
    }
}

/* ==================== 对外接口 ==================== */

void sleep_init(void)
{
    uint8_t i;
    s_d.state          = SLEEP_AWAKE;
    s_d.start_ms       = 0;
    s_d.sleep_ms       = 0;
    s_d.last_end_ms    = 0;
    s_d.last_dur_ms    = 0;
    s_d.total_sleep_ms = 0;
    s_d.sess_cnt       = 0;
    s_d.act_avg        = 0;
    s_d.act_now        = 0;
    s_d.hr_cur         = 0;
    s_d.hr_valid       = 0;
    s_d.hr_base        = 65;        /* ★ 给个合理初值，避免开机第一分钟基线为 0 */
    s_d.quiet_min      = 0;
    s_d.active_min     = 0;
    s_d.tick_min       = 0;
    s_d.evt_cnt        = 0;

    s_act_sum = 0;
    s_act_cnt = 0;
    s_tick_ms = millis();

    s_evt_head = 0;
    s_evt_tail = 0;
    s_evt_cnt  = 0;
    for (i = 0; i < SLEEP_EVT_MAX; i++) {
        s_evt_type[i] = 0;
        s_evt_ms[i]   = 0;
    }
}

void sleep_update(int32_t activity, uint8_t hr, uint8_t hr_valid)
{
    uint32_t now = millis();

    /* 记录瞬时值（OLED 显示用）*/
    s_d.act_now  = activity;
    s_d.hr_cur   = hr;
    s_d.hr_valid = hr_valid;

    /* 累加活动量（每个采样点）*/
    if (s_act_cnt < 60000) {
        s_act_sum += activity;
        s_act_cnt++;
    }

    /* ★ 睡眠中实时增长睡眠时长（给 OLED / 上报用）*/
    if (s_d.state == SLEEP_SLEEPING) {
        s_d.sleep_ms = (now >= s_d.start_ms) ? (now - s_d.start_ms) : 0;
    }

    /* 到点就做一次分钟判据 */
    if ((now - s_tick_ms) >= SL_TICK_MS) {
        s_tick_ms = now;
        do_tick();
    }
}

uint8_t sleep_get_state(void)
{
    return s_d.state;
}

uint32_t sleep_get_duration_ms(void)
{
    if (s_d.state == SLEEP_SLEEPING) return s_d.sleep_ms;
    return 0;
}

void sleep_get_data(sleep_data_t *d)
{
    if (!d) return;
    *d = s_d;
}

/* ★ 2026-10-31 新增：只读队首，不删除。
 *   配合 esp_send_data() 的返回值，发送成功后才调 sleep_pop_event() 出队，
 *   这样断线期间积压的事件不会被白白丢掉。 */
uint8_t sleep_peek_event(uint8_t *type, uint32_t *t_ms)
{
    if (s_evt_cnt == 0) return 0;

    if (type) *type = s_evt_type[s_evt_tail];
    if (t_ms) *t_ms = s_evt_ms[s_evt_tail];

    return 1;
}

uint8_t sleep_pop_event(uint8_t *type, uint32_t *t_ms)
{
    if (s_evt_cnt == 0) return 0;

    if (type) *type = s_evt_type[s_evt_tail];
    if (t_ms) *t_ms = s_evt_ms[s_evt_tail];

    s_evt_tail = (uint8_t)((s_evt_tail + 1) % SLEEP_EVT_MAX);
    s_evt_cnt--;
    s_d.evt_cnt = s_evt_cnt;
    return 1;
}

void sleep_reset_stats(void)
{
    s_d.total_sleep_ms = 0;
    s_d.sess_cnt       = 0;
    s_d.last_dur_ms    = 0;
}

/**
 ******************************************************************************
 *  sleep.h —— 睡眠检测（体动 + 心率双判据）
 ******************************************************************************
 *  ★ 2026-10-23 新增
 *
 *  【设计依据】
 *    1. 体动判据：
 *       医学上 actigraphy（体动记录仪）是睡眠监测的常用方法，
 *       经典算法（Sadeh / Cole-Kripke）都是基于"单位时间内的活动量"判定睡眠/清醒。
 *       ⚠️ 但那些算法是为【手腕佩戴】设计的；本设备是【静置】的（放床头/桌面），
 *          采集到的是床铺/桌面的振动，信号特征完全不同。
 *          → 所以不能直接套用文献阈值，本模块的阈值是【可现场标定】的宏。
 *
 *    2. 心率判据：
 *       睡眠时心率会显著下降。文献数据（Empatica 预计算 HR 通道）：
 *         Wake = 70.9 bpm  →  N1 = 66.0  →  N2 = 61.x  （越深睡越低）
 *       → 用"比清醒基线低 SL_HR_DROP 以上"作为辅助判据。
 *
 *    3. 双判据的必要性：
 *       单独用体动：静置设备上的人不动 ≠ 睡着（坐着不动也会被判成睡眠）
 *       单独用心率：心率受情绪/咖啡因/室温影响大
 *       → 两者同时满足才判入睡，可显著降低误判。
 *
 *  【⚠️ 局限（必须知道）】
 *    · 本模块【不是医疗器械级】的睡眠分期，只做「睡眠/清醒」二分类
 *    · 无法区分 N1/N2/N3/REM
 *    · 阈值需要按实际使用场景标定（见 slib 的宏）
 ******************************************************************************
 */
#ifndef __SLEEP_H
#define __SLEEP_H

#include "bsp.h"

/* ==================== 状态 ==================== */
#define SLEEP_AWAKE      0      /* 清醒 */
#define SLEEP_SLEEPING   1      /* 睡眠 */

/* ==================== 事件类型（用于断线补传）==================== */
#define SLEEP_EVT_START  1      /* 入睡（记下此刻的 millis）*/
#define SLEEP_EVT_END    2      /* 醒来 */

/* ==================== 事件缓存深度 ====================
 *  App 断线时先存这里，重连后补传。
 *  ★ 每条 5 字节（type 1 + t_ms 4），10 条 = 50 字节 RAM，可接受 */
#define SLEEP_EVT_MAX    10

/* ==================== 对外数据结构 ==================== */
typedef struct {
    uint8_t  state;             /* SLEEP_AWAKE / SLEEP_SLEEPING */

    /* ---- 时间（★ 都是 millis 相对时间，绝对时间由 App 打时间戳）---- */
    uint32_t start_ms;          /* 本次入睡时刻（0 = 还没睡过）*/
    uint32_t sleep_ms;          /* 本次已睡时长（ms），睡眠中实时增长 */
    uint32_t last_end_ms;       /* 上次醒来的时刻（0 = 还没醒过）*/
    uint32_t last_dur_ms;       /* 上次睡眠的完整时长 */
    uint32_t total_sleep_ms;    /* 开机以来累计睡眠时长 */
    uint16_t sess_cnt;          /* 开机以来睡眠次数 */

    /* ---- 算法内部状态（调试用）---- */
    int32_t  act_avg;           /* 上一分钟的平均活动量（mg）*/
    int32_t  act_now;           /* 最近一次瞬时活动量（mg）*/
    uint8_t  hr_cur;            /* 当前心率 */
    uint8_t  hr_valid;
    uint8_t  hr_base;           /* 心率基线 */
    uint8_t  quiet_min;         /* 已连续多少分钟"安静 + 心率低" */
    uint8_t  active_min;        /* 已连续多少分钟"明显活动" */
    uint8_t  tick_min;          /* 开机以来过了多少分钟（tick 计数）*/

    /* ---- 事件缓存（环形）---- */
    uint8_t  evt_cnt;           /* 当前缓存了几个待补传事件 */
} sleep_data_t;

/* ==================== 接口 ==================== */

/** 初始化（清空全部状态）*/
void sleep_init(void);

/**
 * 周期调用 —— 建议跟计步一起（每 25~50ms 一次）
 *
 * @param activity  当前动态加速度（mg），来自 mpu6050_get_activity()
 * @param hr        当前心率（bpm），0 = 无效
 * @param hr_valid  心率是否有效
 */
void sleep_update(int32_t activity, uint8_t hr, uint8_t hr_valid);

/** 当前状态：SLEEP_AWAKE / SLEEP_SLEEPING */
uint8_t sleep_get_state(void);

/** 本次睡眠已持续多久（ms）；非睡眠状态返回 0 */
uint32_t sleep_get_duration_ms(void);

/** 取完整状态（给 OLED / 上报用）*/
void sleep_get_data(sleep_data_t *d);

/**
 * 取一个待补传的事件（先入先出）
 *
 * @param type   输出：SLEEP_EVT_START / SLEEP_EVT_END
 * @param t_ms   输出：事件发生的 millis 相对时间
 * @return 1 = 取到了；0 = 没有事件
 *
 * ★ 用途：ESP32 断线时事件先存在缓存里，重连后逐条补传给 App，
 *   App 收到后用【手机系统时间】打绝对时间戳。
 */
uint8_t sleep_pop_event(uint8_t *type, uint32_t *t_ms);

/**
 * 只读队首事件，不删除
 *
 * ★ 2026-10-31 修正：原写法是 pop 之后直接发，发送失败事件就丢了，
 *   "断线补发"实际不成立。改成 peek → 发送 → 成功才 pop。
 *
 * @param type   输出：SLEEP_EVT_START / SLEEP_EVT_END
 * @param t_ms   输出：事件发生的 millis 相对时间
 * @return 1 = 有事件；0 = 缓存为空
 */
uint8_t sleep_peek_event(uint8_t *type, uint32_t *t_ms);

/** 清空累计统计（不清当前状态）*/
void sleep_reset_stats(void);

#endif /* __SLEEP_H */

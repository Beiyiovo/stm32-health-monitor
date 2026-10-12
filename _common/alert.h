/**
 ******************************************************************************
 *  alert.h —— 阈值告警模块（蜂鸣器 + 红色 LED）
 ******************************************************************************
 *  任务书要求：
 *      心率正常范围 60~100 bpm
 *      体温正常范围 36.0~37.5 ℃
 *      超范围时：蜂鸣器 + 红色 LED 提醒，响应时间 ≤ 5 秒
 ******************************************************************************
 */
#ifndef __ALERT_H
#define __ALERT_H

#include "bsp.h"

#define ALERT_NONE      0
#define ALERT_HR_LOW    1       /* 心率过低 */
#define ALERT_HR_HIGH   2       /* 心率过高 */
#define ALERT_TEMP_LOW  3       /* 体温过低 */
#define ALERT_TEMP_HIGH 4       /* 体温过高 */

void    alert_init(void);

/**
 * 周期调用（建议每 100ms 一次）
 *   hr       心率 bpm
 *   hr_valid 心率是否有效（无效则不判心率）
 *   temp10   体温 ×10（如 365 = 36.5℃）
 *   temp_valid 体温是否有效
 * 返回当前告警类型（ALERT_xxx）
 */
uint8_t alert_update(uint8_t hr, uint8_t hr_valid, int16_t temp10, uint8_t temp_valid);

uint8_t alert_is_active(void);
uint8_t alert_get_type(void);
const char *alert_text(uint8_t type);       /* 返回中文告警文字 */

void    alert_test(void);                   /* 上电自检：响一声 + 闪一下 */

/* ★★★ 2026-10-06 新增：蜂鸣器静音（按键消音用）
 *   ms = 静音时长（毫秒），0 = 取消静音立即恢复
 *   ★ 静音只是关掉蜂鸣器，红色 LED 仍然会闪（保持视觉提醒）*/
void    alert_mute(uint32_t ms);
uint8_t alert_is_muted(void);               /* 1 = 正在静音中 */

#endif /* __ALERT_H */

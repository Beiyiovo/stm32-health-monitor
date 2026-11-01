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
/* ★ 2026-10-23 新增：低血氧告警（额外功能 E5，不在任务书要求内）
 *   ★ 判据：SpO2 < 92%（正常 95~100%，90~94% 属轻度缺氧）*/
#define ALERT_SPO2_LOW  5       /* 血氧过低 */

void    alert_init(void);

/**
 * 周期调用（建议每 100ms 一次）
 *   hr       心率 bpm
 *   hr_valid 心率是否有效（无效则不判心率）
 *   temp10   体温 ×10（如 365 = 36.5℃）
 *   temp_valid 体温是否有效
 *   spo2     血氧 %
 *   spo2_valid 血氧是否有效（★ 无效时不判，避免手指没放上误报）
 * 返回当前告警类型（ALERT_xxx）
 *
 * ★ 2026-10-23：加了 spo2 / spo2_valid 两个参数（额外功能 E5）
 */
uint8_t alert_update(uint8_t hr, uint8_t hr_valid,
                     int16_t temp10, uint8_t temp_valid,
                     uint8_t spo2, uint8_t spo2_valid);

uint8_t alert_is_active(void);
uint8_t alert_get_type(void);
const char *alert_text(uint8_t type);       /* 返回中文告警文字 */

void    alert_test(void);                   /* 上电自检：响一声 + 闪一下 */

/* ★★★ 2026-10-06 新增：蜂鸣器静音（按键消音用）
 *   ms = 静音时长（毫秒），0 = 取消静音立即恢复
 *   ★ 静音只是关掉蜂鸣器，红色 LED 仍然会闪（保持视觉提醒）*/
void    alert_mute(uint32_t ms);
uint8_t alert_is_muted(void);               /* 1 = 正在静音中 */

/* ★★★ 2026-10-31 新增：主动报警测试（KEY1 长按触发）
 *   ms = 持续时间（毫秒），0 = 立即停止
 *   ★ 只驱动蜂鸣器与红灯，【不改变告警状态】，也不产生上报事件。
 *     用途：答辩演示"报警长什么样"，不用真把体温捂到 37.6 ℃。
 *   ★ 测试期间优先级高于正常告警逻辑，结束后自动回到原状态。*/
void    alert_beep_test(uint32_t ms);
uint8_t alert_is_testing(void);             /* 1 = 主动报警进行中 */

#endif /* __ALERT_H */

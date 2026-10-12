/**
 ******************************************************************************
 *  max30102.h —— MAX30102 心率血氧传感器驱动
 ******************************************************************************
 */
#ifndef __MAX30102_H
#define __MAX30102_H

#include "bsp.h"

#define MAX30102_ADDR   0x57        /* ★ 本模块的 I2C 地址 */
#define MAX30102_PARTID 0x15        /* PART_ID 寄存器应该读到这个值 */

typedef struct {
    uint32_t ir;            /* 红外原始值（18 位） */
    uint32_t red;           /* 红光原始值（18 位） */
    uint8_t  hr;            /* 心率 bpm（0 = 无效） */
    uint8_t  spo2;          /* 血氧 %（0 = 无效） */
    uint8_t  finger;        /* 手指是否放上（1 = 放上了） */
    uint8_t  hr_valid;      /* 心率值是否有效 */
    uint8_t  spo2_valid;    /* 血氧值是否有效 */
    int32_t  ir_ac;         /* 调试用：IR 的交流分量 */
    int32_t  ir_dc;         /* 调试用：IR 的直流分量 */
} max30102_data_t;

uint8_t max30102_init(void);
uint8_t max30102_update(void);          /* ★ 建议每 10~20ms 调用一次 */
max30102_data_t max30102_get(void);
uint32_t max30102_get_debug_cnt(void);   /* ★ 只要样本总数 */
void    max30102_reset_algo(void);      /* 重新开始测量（手指拿开再放上时调） */

/**
 * ★★★ 2026-10-06 新增：算法内部状态（调试用）
 * 用来诊断"为什么心率出不来"——能看到 DC / AC / 峰值 / 间隔每一步的值
 */
typedef struct {
    int32_t  ir_dc;                 /* IR 直流分量（滑动平均） */
    int32_t  red_dc;                /* RED 直流分量 */
    int32_t  ir_ac;                 /* IR 交流分量（最近一次） */
    int32_t  red_ac;                /* RED 交流分量（最近一次） */
    int32_t  ir_ac_amp;             /* IR 交流幅度跟踪值（血氧用） */
    int32_t  red_ac_amp;            /* RED 交流幅度跟踪值 */
    int32_t  ac_max;                /* 本心搏周期内的 AC 最大值 */
    int32_t  ac_min;                /* 本心搏周期内的 AC 最小值 */
    int32_t  ac_max_ever;           /* ★ 史上最大 AC 幅度（判断信号够不够强） */
    uint8_t  peak_state;            /* 0=找峰 1=找谷 */
    uint32_t sample_cnt;            /* 累计处理的样本数 */
    uint8_t  peak_cnt;              /* 已记录几个峰间隔 */
    uint8_t  peak_idx;              /* 环形缓冲写位置 */
    uint32_t last_peak;             /* 上一个峰的样本号 */
    uint16_t peak_interval[8];      /* 最近的峰间隔（样本数） */
    uint8_t  fifo_ovf;              /* ★ FIFO 溢出计数（丢了多少样本） */

    /* ★★★ max30102_update() 调用统计（定位"样本数一直为 0"） */
    uint32_t upd_calls;             /* update() 被调用次数 */
    uint32_t upd_fail_wr;           /* 读 FIFO_WR 失败次数 */
    uint32_t upd_fail_rd;           /* 读 FIFO_RD 失败次数 */
    uint32_t upd_fail_data;         /* 读 FIFO_DATA 失败次数 */
    uint32_t upd_ok;                /* 成功处理的样本数 */
    uint8_t  upd_last_wr;           /* 最近读到的写指针 */
    uint8_t  upd_last_rd;           /* 最近读到的读指针 */
    uint8_t  upd_last_n;            /* 最近算出的可用样本数 */

    /* ★★★ 初始化后的寄存器读回值（验证配置有没有真正写进芯片） */
    uint8_t  init_partid;           /* PART_ID，正品 = 0x15 */
    uint8_t  init_mode;             /* MODE_CFG 读回，期望 0x03 */
    uint8_t  init_spo2;             /* SPO2_CFG 读回，期望 0x27 */
    uint8_t  init_led;              /* LED1_PA  读回，期望 0x24 */
    uint8_t  init_fcfg;             /* FIFO_CFG 读回，期望 0x4F */
    uint8_t  init_wr1;              /* 初始化后第一次读 FIFO_WR */
    uint8_t  init_wr2;              /* 200ms 后第二次读 FIFO_WR（应该变了）*/

    /* ★★★ 现场对照读（诊断时刻临时读一次，和 update() 里存的值对比） */
    uint8_t  live_wr;               /* 现场读 FIFO_WR */
    uint8_t  live_rd;               /* 现场读 FIFO_RD */
} max30102_dbg_t;

void max30102_get_debug(max30102_dbg_t *d);

#endif /* __MAX30102_H */

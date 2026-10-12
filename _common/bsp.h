/**
 ******************************************************************************
 *  bsp.h —— 501 个人健康助手 · 板级支持包（公共头文件）
 ******************************************************************************
 *  ★ 所有引脚分配集中在这里，改引脚只改这个文件
 *  ★ 所有工程（04~09）都引用本文件
 ******************************************************************************
 */
#ifndef __BSP_H
#define __BSP_H

#include "stm32f10x.h"
#include <stdio.h>
#include <string.h>

/* ==================== ★ 引脚分配（已锁定，改要同步文档） ==================== */

/* --- I2C1：OLED + MPU6050 + MAX30102 共用 --- */
#define I2C_GPIO            GPIOB
#define I2C_SCL_PIN         GPIO_Pin_6      /* PB6 —— 板丝印【B6】 */
#define I2C_SDA_PIN         GPIO_Pin_7      /* PB7 —— 板丝印【B7】 */

/* --- DS18B20 温度（单总线） --- */
#define DS_GPIO             GPIOA
#define DS_PIN              GPIO_Pin_0      /* PA0 —— 板丝印【A0】 */

/* --- 蜂鸣器（★ 低电平触发，2026-10-05 实测） --- */
#define BUZZ_GPIO           GPIOB
#define BUZZ_PIN            GPIO_Pin_0      /* PB0 —— 板丝印【B0】 */

/* --- 红色 LED（高电平点亮，串 220Ω） --- */
#define LED_R_GPIO          GPIOB
#define LED_R_PIN           GPIO_Pin_1      /* PB1 —— 板丝印【B1】 */

/* --- 三个按键（按下 = 低电平） --- */
#define KEY1_GPIO           GPIOA
#define KEY1_PIN            GPIO_Pin_1      /* PA1 —— 板丝印【A1】 */
#define KEY2_GPIO           GPIOA
#define KEY2_PIN            GPIO_Pin_4      /* PA4 —— 板丝印【A4】 */
#define KEY3_GPIO           GPIOA
#define KEY3_PIN            GPIO_Pin_5      /* PA5 —— 板丝印【A5】 */

/* --- 板载 LED（★ 低电平点亮） --- */
#define LED_ONBOARD_GPIO    GPIOC
#define LED_ONBOARD_PIN     GPIO_Pin_13     /* PC13 */

/* --- 串口 --- */
/* USART1 = PA9(TX) / PA10(RX) —— 接 USB-TTL，给电脑看打印 */
/* USART2 = PA2(TX) / PA3(RX)  —— 接 ESP32-C3 的 GPIO06/GPIO07 */

/* ==================== ★ 告警阈值（来自任务书） ==================== */
#define HR_MIN              60              /* 心率下限 bpm */
#define HR_MAX              100             /* 心率上限 bpm */
/* ★★★ 2026-10-06 调整：体温下限 360 → 350
 *
 * 【为什么改】
 *   任务书写"体温 36.0~37.5℃"，那是【人体核心体温】（腋下/口腔测量）的范围。
 *   本设计的 DS18B20 是【接触式体表测量】，贴在皮肤表面测到的温度
 *   比核心体温低 1~3℃（受环境温度、末梢血液循环影响）：
 *       手指捏探头    → 32 ~ 35.5℃
 *       贴额头        → 35.5 ~ 37.0℃
 *       腋下夹紧      → 36.0 ~ 37.0℃
 *
 *   实测（2026-10-06）：探头在室温下 25.0℃；手指捏住升到 35.3~35.4℃。
 *   如果沿用 36.0℃ 下限，正常体表测量会被误判成"体温过低"→ 蜂鸣器一直报警。
 *
 * 【处理办法】
 *   把判定下限设为 35.0℃，等效核心体温约 36.0℃，
 *   与任务书要求一致。★ 这一点在论文里作为"体表—核心温度换算"说明。
 *
 * 【上限】保持 37.5℃ 不变（体表超过 37.5℃ 已属异常，不会漏报）*/
#define TEMP_MIN            350             /* 体温下限，×10（体表测量，等效核心 36.0） */
#define TEMP_MAX            375             /* 体温上限，×10 */

/* ★ 体温"有效性"下限（低于此值视为探头没接触人体）*/
#define TEMP_VALID_MIN      330

/* ==================== 公共函数 ==================== */

void bsp_init(void);                        /* 初始化板载 LED + 按键 */

void delay_ms(uint32_t ms);                 /* 精确毫秒延时（基于 DWT 周期计数器） */
void delay_us(uint32_t us);                 /* 精确微秒延时（DS18B20 / 软件 I2C 时序用） */
uint32_t millis(void);                      /* ★ 毫秒级时间戳（单调递增，带回绕补偿） */

void led_onboard(int on);                   /* 板载 LED，1=亮 */
void led_red(int on);                       /* 红色 LED，1=亮 */
void buzzer(int on);                        /* 蜂鸣器，1=响（★ 低电平触发） */

uint8_t key1_pressed(void);
uint8_t key_get_event(uint8_t idx);
uint8_t key_any_just_pressed(void);         /* ★ 刚按下的键号（1/2/3），0=无 */         /* ★ 按键事件：0=无 1=短按 2=长按 */
uint8_t key2_pressed(void);
uint8_t key3_pressed(void);

/* 串口 */
void uart1_init(void);                      /* 调试打印，115200 */
void uart2_init(void);                      /* ESP32 通信，115200 */
void uart2_send(const char *s);
int  uart2_recv_line(char *buf, int maxlen, uint32_t timeout_ms);
int  uart2_getc_nonblock(char *c);          /* 非阻塞取一字节，1=取到，0=没有 */
void uart2_clear(void);                     /* 清空接收缓冲 */
uint32_t uart2_get_overflow(void);          /* ★ 接收溢出次数（诊断 ESP32 发太快） */
uint16_t uart2_available(void);             /* ★ 缓冲区里还有多少字节 */

#endif /* __BSP_H */

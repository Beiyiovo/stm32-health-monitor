/**
 ******************************************************************************
 *  oled.h —— SSD1306 OLED 驱动（128×64，I2C）
 ******************************************************************************
 */
#ifndef __OLED_H
#define __OLED_H

#include "bsp.h"

#define OLED_ADDR       0x3C        /* ★ 本项目 OLED 的 I2C 地址 */
#define OLED_W          128
#define OLED_H          64

void oled_init(void);
void oled_clear(void);
void oled_refresh(void);                                    /* 把缓冲区刷到屏幕 */

void oled_draw_str(uint8_t x, uint8_t page, const char *s); /* page: 0~7，每页 8 像素高 */
void oled_draw_num(uint8_t x, uint8_t page, int32_t num);

/**
 * ★ 显示中文（16×16 点阵）
 *   x    : 起始列（0 ~ 112）
 *   page : 起始页（0 ~ 6，中文占 2 页）
 * 字库在 cnfont_data.h（由 generate_cnfont.py 生成）
 * ★ 屏幕只有 8 页 → 中文最多 4 行，每行最多 8 个字
 */
void oled_draw_cn(uint8_t x, uint8_t page, const char *s);
void oled_printf(uint8_t x, uint8_t page, const char *fmt, ...);

void oled_rect_clear(uint8_t x, uint8_t page, uint8_t w, uint8_t pages);

/**
 * ★★★ 只刷新一页（分帧刷新，避免长时间阻塞主循环）
 * 整屏刷一遍约 374ms，单页只要约 46ms。
 * 用法：主循环里轮着调 oled_refresh_page(0)~oled_refresh_page(7)
 */
void oled_refresh_page(uint8_t page);

#endif /* __OLED_H */

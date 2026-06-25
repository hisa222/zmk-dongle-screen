/*
 * Copyright (c) 2024 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <stdbool.h>

/**
 * Touch event data structure
 */
struct touch_event_data {
    uint16_t x;          // Touch X coordinate (0-239)
    uint16_t y;          // Touch Y coordinate (0-279)
    bool touched;        // Touch state (true = touched, false = released)
    uint32_t timestamp;  // Event timestamp (ms)
};

/**
 * Touch event callback type
 */
typedef void (*touch_event_callback_t)(const struct touch_event_data *event);

/**
 * touch handler init
 */
int touch_handler_init(void);

/**
 * callback registration
 */
int touch_handler_register_callback(touch_event_callback_t callback);

/**
 * get last event
 */
int touch_handler_get_last_event(struct touch_event_data *event);

/**
 * register LVGL indev (LVGL8: lv_indev_drv_init + lv_indev_drv_register)
 */
int touch_handler_register_lvgl_indev(void);

/**
 * true while a touch/swipe gesture is in progress (swipe_state.in_progress)
 */
bool touch_handler_is_swiping(void);

/**
 * UI interaction flag shared with screen widgets.
 *
 * brightness slider や system settings ボタン操作中は true にする。
 * custom_status_screen.c 側はこれを見てスワイプ遷移を抑止する。
 *
 * brightness_screen.c: PRESSED で true、RELEASED/PRESS_LOST で false
 *                      縦スワイプキャンセル時は即 false (スワイプ通過のため)
 * system_settings_widget.c: ボタン PRESSED で true、RELEASED/PRESS_LOST で false
 */
extern volatile bool ui_interaction_active;

/*
 * Copyright (c) 2024 The ZMK Contributors
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <lvgl.h>
#include <stdbool.h>

/*
 * brightness_screen が現在使うスライダー操作状態。
 * touch_handler.c の weak 関数 display_settings_is_interacting() を
 * brightness_screen.c でオーバーライドして参照する。
 */
struct zmk_widget_brightness_screen {
    lv_obj_t *obj;          /* = parent そのもの（コンテナなし）*/
    lv_obj_t *title_label;
    lv_obj_t *slider;
    lv_obj_t *value_label;
    lv_obj_t *icon_low;
    lv_obj_t *icon_high;
    lv_obj_t *nav_hint;
};

int  zmk_widget_brightness_screen_init(struct zmk_widget_brightness_screen *widget,
                                       lv_obj_t *parent);

/* スクリーン表示/非表示時に呼ぶ。スライダー値同期とフラグ管理を行う。 */
void zmk_widget_brightness_screen_show(struct zmk_widget_brightness_screen *widget);
void zmk_widget_brightness_screen_hide(struct zmk_widget_brightness_screen *widget);

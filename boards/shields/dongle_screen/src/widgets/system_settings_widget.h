/*
 * Copyright (c) 2024 The ZMK Contributors
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <lvgl.h>
#include <stdbool.h>

/*
 * custom_status_screen.c 側で定義される共有フラグ。
 * brightness_screen / system_settings_widget が操作中に true にする。
 */
extern bool ui_interaction_active;

struct zmk_widget_system_settings {
    lv_obj_t *obj;              /* = parent そのもの（コンテナなし）*/
    lv_obj_t *title_label;
    lv_obj_t *bootloader_btn;
    lv_obj_t *reset_btn;
    lv_obj_t *nav_hint;
};

int  zmk_widget_system_settings_init(struct zmk_widget_system_settings *widget,
                                     lv_obj_t *parent);
void zmk_widget_system_settings_show(struct zmk_widget_system_settings *widget);
void zmk_widget_system_settings_hide(struct zmk_widget_system_settings *widget);

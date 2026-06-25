/*
 * Copyright (c) 2024 The ZMK Contributors
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <lvgl.h>
#include <stdbool.h>

struct zmk_widget_brightness_screen {
    lv_obj_t *obj;             /* root container */
    lv_obj_t *title_label;
    lv_obj_t *value_label;
    lv_obj_t *slider;
    lv_obj_t *hint_label;

    uint8_t current_value;
    bool dragging;
};

int  zmk_widget_brightness_screen_init(struct zmk_widget_brightness_screen *widget,
                                       lv_obj_t *parent);
void zmk_widget_brightness_screen_show(struct zmk_widget_brightness_screen *widget);
void zmk_widget_brightness_screen_hide(struct zmk_widget_brightness_screen *widget);

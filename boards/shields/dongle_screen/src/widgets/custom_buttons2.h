/*
 * Copyright (c) 2024 The ZMK Contributors
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <lvgl.h>
#include <stdbool.h>

struct zmk_widget_custom_buttons2 {
    lv_obj_t *obj;
    lv_obj_t *title_label;
    lv_obj_t *custom_button_1_btn;
    lv_obj_t *custom_button_2_btn;
    lv_obj_t *custom_button_3_btn;
    lv_obj_t *custom_button_4_btn;
    lv_obj_t *custom_button_5_btn;
    lv_obj_t *custom_button_6_btn;
    lv_obj_t *nav_hint;
};

int zmk_widget_custom_buttons2_init(struct zmk_widget_custom_buttons *widget,
                                   lv_obj_t *parent);

static inline lv_obj_t *zmk_widget_custom_buttons2_obj(struct zmk_widget_custom_buttons2 *widget)
{
    return widget->obj;
}

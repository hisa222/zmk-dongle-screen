/*
 * Copyright (c) 2024 The ZMK Contributors
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <lvgl.h>
#include <stdbool.h>

struct zmk_custom_buttons {
    lv_obj_t *obj;
    lv_obj_t *title_label;
    lv_obj_t *custom_button_1;
    lv_obj_t *custom_button_2;
    lv_obj_t *custom_button_3;
    lv_obj_t *custom_button_4;
    lv_obj_t *custom_button_5;
    lv_obj_t *custom_button_6;
    lv_obj_t *nav_hint;
};

int zmk_custom_buttons_init(struct zmk_custom_buttons *widget,
                                   lv_obj_t *parent);

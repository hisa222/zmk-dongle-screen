/*
 * Copyright (c) 2024 The ZMK Contributors
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <lvgl.h>
#include <stdbool.h>

struct zmk_widget_media_control {
    lv_obj_t *obj;
    lv_obj_t *title_label;
    lv_obj_t *bri_down_btn;
    lv_obj_t *prtscn_btn;
    lv_obj_t *bri_up_btn;
    lv_obj_t *mute_btn;
    lv_obj_t *vol_down_btn;
    lv_obj_t *vol_up_btn;
    lv_obj_t *nav_hint;
};

int zmk_widget_media_control_init(struct zmk_widget_media_control *widget,
                                   lv_obj_t *parent);

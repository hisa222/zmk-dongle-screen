/*
 * Copyright (c) 2024 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <lvgl.h>

struct zmk_widget_system_settings {
    lv_obj_t *obj;               /* Container object                */
    lv_obj_t *title_label;       /* Title label                     */
    lv_obj_t *bootloader_btn;    /* "Enter Bootloader" button       */
    lv_obj_t *bootloader_label;  /* (unused - kept for compat)      */
    lv_obj_t *reset_btn;         /* "System Reset" button           */
    lv_obj_t *reset_label;       /* (unused - kept for compat)      */
    lv_obj_t *parent;            /* Parent screen                   */

    /* Brightness selector UI */
    lv_obj_t *brightness_label;      /* "Brightness:" label         */
    lv_obj_t *brightness_value;      /* Current value display       */
    lv_obj_t *brightness_left_btn;   /* Decrease brightness button  */
    lv_obj_t *brightness_right_btn;  /* Increase brightness button  */
};

/* Dynamic allocation */
struct zmk_widget_system_settings *zmk_widget_system_settings_create(lv_obj_t *parent);
void zmk_widget_system_settings_destroy(struct zmk_widget_system_settings *widget);

/* Widget control */
int  zmk_widget_system_settings_init(struct zmk_widget_system_settings *widget, lv_obj_t *parent);
void zmk_widget_system_settings_show(struct zmk_widget_system_settings *widget);
void zmk_widget_system_settings_hide(struct zmk_widget_system_settings *widget);

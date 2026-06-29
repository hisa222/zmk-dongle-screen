/*
 * Copyright (c) 2020 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#include <zmk/display.h>
#include "keyboard_name_status.h"

int zmk_widget_keyboard_name_status_init(struct zmk_widget_keyboard_name_status *widget,
                                         lv_obj_t *parent)
{
    widget->obj = lv_label_create(parent);

    /* CONFIG_ZMK_KEYBOARD_NAME で定義されたキーボード名を表示 */
    lv_label_set_text(widget->obj, CONFIG_ZMK_KEYBOARD_NAME);

    /* フォント設定 */
    lv_obj_set_style_text_font(widget->obj, &lv_font_montserrat_40, 0);

    lv_obj_set_style_text_color(widget->obj, lv_color_hex(0x8CFFDE), 0);

    return 0;
}

lv_obj_t *zmk_widget_keyboard_name_status_obj(struct zmk_widget_keyboard_name_status *widget)
{
    return widget->obj;
}

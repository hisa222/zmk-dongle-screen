/*
 * Copyright (c) 2024 The ZMK Contributors
 * SPDX-License-Identifier: MIT
 *
 * Main Screen Buttons Widget (LVGL8 / ZMK 3.5)
 *
 * custom_buttons(.c/.h) とは完全に独立したウィジェットです。
 * custom_status_screen のメイン画面(create_main_screen)専用に、
 * CONFIG のオン/オフで出し分けられる 3 個 x 2 段のボタン群を提供します。
 *
 *   [CONFIG_DONGLE_SCREEN_MAIN_BUTTONS_ROW1 = y]
 *     | main_btn_1 | | main_btn_2 | | main_btn_3 |
 *
 *   [CONFIG_DONGLE_SCREEN_MAIN_BUTTONS_ROW2 = y]
 *     | main_btn_4 | | main_btn_5 | | main_btn_6 |
 *
 * 位置は custom_status_screen.c 側で lv_obj_align() を使って
 * 自由に編集してください（本ウィジェットは初期位置を仮置きするのみ）。
 */
#pragma once
#include <lvgl.h>
#include <stdbool.h>

struct zmk_widget_main_screen_buttons {
    lv_obj_t *obj;

    /* ROW1 (CONFIG_DONGLE_SCREEN_MAIN_BUTTONS_ROW1 が y のときのみ非NULL) */
    lv_obj_t *main_btn_1;
    lv_obj_t *main_btn_2;
    lv_obj_t *main_btn_3;

    /* ROW2 (CONFIG_DONGLE_SCREEN_MAIN_BUTTONS_ROW2 が y のときのみ非NULL) */
    lv_obj_t *main_btn_4;
    lv_obj_t *main_btn_5;
    lv_obj_t *main_btn_6;
};

/*
 * widget を parent(通常は main screen の lv_obj_t) 上に生成する。
 * 各ボタンは仮の位置(LV_ALIGN_CENTER, 0, 0)に重なって生成されるため、
 * 呼び出し側(custom_status_screen.c)で必ず lv_obj_align() により
 * 位置を再設定してください。
 */
int zmk_widget_main_screen_buttons_init(struct zmk_widget_main_screen_buttons *widget,
                                        lv_obj_t *parent);

static inline lv_obj_t *zmk_widget_main_screen_buttons_obj(struct zmk_widget_main_screen_buttons *widget)
{
    return widget->obj;
}

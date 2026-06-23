/*
 * Copyright (c) 2024 The ZMK Contributors
 * SPDX-License-Identifier: MIT
 *
 * Brightness Screen Widget
 * iOS風スライダーで画面輝度を調整する専用画面
 */

#pragma once

#include <lvgl.h>

struct zmk_widget_brightness_screen {
    lv_obj_t *obj;              /* フルスクリーンコンテナ         */
    lv_obj_t *title_label;      /* タイトル                       */
    lv_obj_t *slider;           /* 輝度スライダー                 */
    lv_obj_t *value_label;      /* "80%" 表示ラベル               */
    lv_obj_t *icon_low;         /* 低輝度アイコン (☀ 小)         */
    lv_obj_t *icon_high;        /* 高輝度アイコン (☀ 大)         */
    lv_obj_t *nav_hint;         /* 操作ヒント                     */
};

int  zmk_widget_brightness_screen_init(struct zmk_widget_brightness_screen *widget,
                                       lv_obj_t *parent);
void zmk_widget_brightness_screen_show(struct zmk_widget_brightness_screen *widget);
void zmk_widget_brightness_screen_hide(struct zmk_widget_brightness_screen *widget);

/**
 * スライダードラッグ中は true を返す。
 * touch_handler.c の display_settings_is_interacting() weak 関数を
 * このモジュールがオーバーライドして使用する。
 */
bool brightness_screen_is_interacting(void);

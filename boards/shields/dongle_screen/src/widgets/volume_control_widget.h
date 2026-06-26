/*
 * Copyright (c) 2026
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

struct zmk_widget_volume_control {
    lv_obj_t *obj;

    /* タイトル */
    lv_obj_t *title_label;

    /* 見た目のボタン */
    lv_obj_t *mute_btn;
    lv_obj_t *vol_down_btn;
    lv_obj_t *vol_up_btn;

    /* ナビゲーションヒント */
    lv_obj_t *nav_hint;
};

/**
 * 音量コントロールウィジェットを初期化する
 *
 * レイアウト
 *
 *        [ Mute ]
 *
 *   [ Vol- ]   [ Vol+ ]
 *
 * ボタン以外からスワイプ開始すると画面遷移可能。
 * ボタン押下中は ui_interaction_active=true となり、
 * 誤って画面遷移しないようにする。
 */
int zmk_widget_volume_control_init(
    struct zmk_widget_volume_control *widget,
    lv_obj_t *parent);

/**
 * 画面表示時
 */
void zmk_widget_volume_control_show(
    struct zmk_widget_volume_control *widget);

/**
 * 画面非表示時
 */
void zmk_widget_volume_control_hide(
    struct zmk_widget_volume_control *widget);

#ifdef __cplusplus
}
#endif

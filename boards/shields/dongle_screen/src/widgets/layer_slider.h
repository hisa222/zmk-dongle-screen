/*
 * Copyright (c) 2024 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 *
 * layer_slider.h
 *
 * レイヤー切替に連動して横方向にスクロールするウィジェット。
 * 参考元の layer_roller（縦ローラー）を横方向に再実装したもの。
 *
 * 表示構造:
 *   [ 前レイヤー名 ]  [ 現在レイヤー名（大・強調色） ]  [ 次レイヤー名 ]
 *   ← ─── lv_obj (container / clip) ──────────────────── →
 *      内部に lv_label を横一列に並べ、lv_anim で x 座標をスライド
 */

#pragma once

#include <lvgl.h>
#include <zephyr/kernel.h>

struct zmk_widget_layer_slider {
    sys_snode_t node;
    lv_obj_t  *obj;       /* 外枠コンテナ（クリッピング境界） */
};

int zmk_widget_layer_slider_init(struct zmk_widget_layer_slider *widget, lv_obj_t *parent);
lv_obj_t *zmk_widget_layer_slider_obj(struct zmk_widget_layer_slider *widget);

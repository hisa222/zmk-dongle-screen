/*
 * Copyright (c) 2024 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 *
 * layer_slider.c
 *
 * レイヤー切替に連動して「横方向」にスライドアニメーションするウィジェット。
 *
 * 動作概要:
 *  - 表示するのはアクティブレイヤーのみ（1枚）。
 *  - レイヤーインデックスが増加した場合: 新ラベルが右からスライドイン。
 *  - レイヤーインデックスが減少した場合: 新ラベルが左からスライドイン。
 *  - 実装: 2枚のラベル（cur/next）を交互に使い、表示中のラベルをアウト、
 *    新しいラベルをインさせる。
 */

#include "layer_slider.h"

#include <string.h>
#include <zmk/display.h>
#include <zmk/events/layer_state_changed.h>
#include <zmk/event_manager.h>
#include <zmk/keymap.h>

#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

/* ------------------------------------------------------------------ */
/* パラメータ設定                                                        */
/* ------------------------------------------------------------------ */

#define WIDGET_W         240
#define WIDGET_H          60
#define ANIM_DURATION_MS 300

/* ------------------------------------------------------------------ */
/* レイヤーカラー（反映先 layer_status.c に合わせる）                    */
/* ------------------------------------------------------------------ */

static const uint32_t layer_colors[] = {
    0x8CFFDE, 0xA3FF7F, 0x7FE3FF, 0xFFFFFF,
    0x8CFFDE, 0xA3FF7F, 0x7FE3FF, 0xFFFFFF,
    0xFFFB8C, 0xFF9B7F, 0xDE8CFF, 0xFF0000,
};

/* ------------------------------------------------------------------ */
/* 内部状態                                                             */
/* ------------------------------------------------------------------ */

struct layer_slider_state {
    uint8_t index;
};

struct layer_slider_runtime {
    lv_obj_t *label_a;      /* ラベルA（2枚交互に使う） */
    lv_obj_t *label_b;      /* ラベルB */
    lv_obj_t *label_in;     /* 現在スライドインしているラベル（ポインタ） */
    lv_obj_t *label_out;    /* 現在スライドアウトしているラベル（ポインタ） */
    uint8_t   shown_index;
    bool      anim_running;
};

static sys_slist_t widgets = SYS_SLIST_STATIC_INIT(&widgets);
static struct layer_slider_runtime g_runtime;

/* ------------------------------------------------------------------ */
/* ユーティリティ                                                        */
/* ------------------------------------------------------------------ */

static void get_layer_name(uint8_t index, char *buf, size_t buf_size)
{
    const char *name = zmk_keymap_layer_name(index);
    if (name && *name) {
        strncpy(buf, name, buf_size - 1);
        buf[buf_size - 1] = '\0';
    } else {
        snprintf(buf, buf_size, "%u", (unsigned)index);
    }
}

static void apply_label(lv_obj_t *label, uint8_t index)
{
    char buf[16];
    get_layer_name(index, buf, sizeof(buf));
    lv_label_set_text(label, buf);

    lv_color_t color = (index < ARRAY_SIZE(layer_colors))
                       ? lv_color_hex(layer_colors[index])
                       : lv_color_white();
    lv_obj_set_style_text_color(label, color, LV_PART_MAIN);
}

static void reset_container_style(lv_obj_t *obj)
{
    lv_obj_set_style_bg_opa(obj,       LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(obj, 0,             LV_PART_MAIN);
    lv_obj_set_style_pad_top(obj,      0,             LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(obj,   0,             LV_PART_MAIN);
    lv_obj_set_style_pad_left(obj,     0,             LV_PART_MAIN);
    lv_obj_set_style_pad_right(obj,    0,             LV_PART_MAIN);
    lv_obj_set_style_outline_width(obj, 0,            LV_PART_MAIN);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
}

/* ------------------------------------------------------------------ */
/* アニメーション                                                        */
/* ------------------------------------------------------------------ */

static void anim_x_exec_cb(void *obj, int32_t value)
{
    lv_obj_set_x((lv_obj_t *)obj, value);
}

static void anim_out_ready_cb(lv_anim_t *a)
{
    /* アウトしたラベルを画面外に退避して非表示 */
    lv_obj_t *label = (lv_obj_t *)a->var;
    lv_obj_set_x(label, WIDGET_W * 2); /* 画面外 */
    lv_obj_add_flag(label, LV_OBJ_FLAG_HIDDEN);

    g_runtime.anim_running = false;
}

static void start_slide_anim(struct layer_slider_runtime *rt,
                              uint8_t new_idx, int8_t direction)
{
    /* direction > 0: インデックス増加 → 新ラベルは右からイン、旧ラベルは左へアウト */
    /* direction < 0: インデックス減少 → 新ラベルは左からイン、旧ラベルは右へアウト */

    int32_t in_start  =  direction * WIDGET_W;  /* インの開始位置（画面外） */
    int32_t out_end   = -direction * WIDGET_W;  /* アウトの終了位置（画面外） */

    /* 進行中アニメーションをキャンセル */
    if (rt->anim_running) {
        lv_anim_del(rt->label_in,  anim_x_exec_cb);
        lv_anim_del(rt->label_out, anim_x_exec_cb);
        /* アウト中のラベルを即座に退避 */
        lv_obj_set_x(rt->label_out, WIDGET_W * 2);
        lv_obj_add_flag(rt->label_out, LV_OBJ_FLAG_HIDDEN);
    }

    /* 新ラベルと旧ラベルを入れ替え */
    lv_obj_t *new_in  = (rt->label_in == rt->label_a) ? rt->label_b : rt->label_a;
    lv_obj_t *new_out = rt->label_in; /* 現在表示中が今度はアウトへ */

    rt->label_in  = new_in;
    rt->label_out = new_out;

    /* 新ラベルに内容を設定して画面外の開始位置に配置 */
    apply_label(new_in, new_idx);
    lv_obj_set_x(new_in, in_start);
    lv_obj_clear_flag(new_in, LV_OBJ_FLAG_HIDDEN);

    rt->shown_index  = new_idx;
    rt->anim_running = true;

    /* イン アニメーション（新ラベルが中央へ） */
    lv_anim_t a_in;
    lv_anim_init(&a_in);
    lv_anim_set_var(&a_in, new_in);
    lv_anim_set_exec_cb(&a_in, anim_x_exec_cb);
    lv_anim_set_time(&a_in, ANIM_DURATION_MS);
    lv_anim_set_path_cb(&a_in, lv_anim_path_ease_out);
    lv_anim_set_values(&a_in, in_start, 0);
    lv_anim_start(&a_in);

    /* アウト アニメーション（旧ラベルが画面外へ） */
    lv_anim_t a_out;
    lv_anim_init(&a_out);
    lv_anim_set_var(&a_out, new_out);
    lv_anim_set_exec_cb(&a_out, anim_x_exec_cb);
    lv_anim_set_time(&a_out, ANIM_DURATION_MS);
    lv_anim_set_path_cb(&a_out, lv_anim_path_ease_in);
    lv_anim_set_values(&a_out, 0, out_end);
    lv_anim_set_ready_cb(&a_out, anim_out_ready_cb);
    lv_anim_start(&a_out);
}

/* ------------------------------------------------------------------ */
/* ZMK ウィジェットコールバック                                           */
/* ------------------------------------------------------------------ */

static void layer_slider_set_layer(struct layer_slider_runtime *rt,
                                   struct layer_slider_state state)
{
    uint8_t new_idx = state.index;
    uint8_t old_idx = rt->shown_index;

    if (new_idx == old_idx) {
        return;
    }

    int8_t direction = (new_idx > old_idx) ? 1 : -1;
    start_slide_anim(rt, new_idx, direction);
}

static void layer_slider_update_cb(struct layer_slider_state state)
{
    struct zmk_widget_layer_slider *widget;
    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, widget, node) {
        layer_slider_set_layer(&g_runtime, state);
    }
}

static struct layer_slider_state layer_slider_get_state(const zmk_event_t *eh)
{
    uint8_t index = zmk_keymap_highest_layer_active();
    return (struct layer_slider_state){ .index = index };
}

ZMK_DISPLAY_WIDGET_LISTENER(widget_layer_slider, struct layer_slider_state,
                             layer_slider_update_cb, layer_slider_get_state)
ZMK_SUBSCRIPTION(widget_layer_slider, zmk_layer_state_changed);

/* ------------------------------------------------------------------ */
/* 初期化                                                               */
/* ------------------------------------------------------------------ */

int zmk_widget_layer_slider_init(struct zmk_widget_layer_slider *widget, lv_obj_t *parent)
{
    struct layer_slider_runtime *rt = &g_runtime;

    /* ── 外枠コンテナ（クリッピング境界） ── */
    widget->obj = lv_obj_create(parent);
    lv_obj_set_size(widget->obj, WIDGET_W, WIDGET_H);
    reset_container_style(widget->obj);

    /* ── ラベルA / B（2枚交互に使う） ── */
    rt->label_a = lv_label_create(widget->obj);
    lv_obj_set_size(rt->label_a, WIDGET_W, WIDGET_H);
    lv_obj_set_pos(rt->label_a, 0, 0);
    lv_obj_set_style_text_align(rt->label_a, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_font(rt->label_a, &lv_font_montserrat_40, LV_PART_MAIN);

    rt->label_b = lv_label_create(widget->obj);
    lv_obj_set_size(rt->label_b, WIDGET_W, WIDGET_H);
    lv_obj_set_pos(rt->label_b, WIDGET_W * 2, 0); /* 初期は画面外 */
    lv_obj_set_style_text_align(rt->label_b, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_font(rt->label_b, &lv_font_montserrat_40, LV_PART_MAIN);
    lv_obj_add_flag(rt->label_b, LV_OBJ_FLAG_HIDDEN);

    /* ── ポインタ初期化 ── */
    rt->label_in  = rt->label_a; /* 最初に表示するのは A */
    rt->label_out = rt->label_b;
    rt->anim_running = false;

    /* ── 初期レイヤー表示 ── */
    uint8_t idx = zmk_keymap_highest_layer_active();
    rt->shown_index = idx;
    apply_label(rt->label_a, idx);

    /* ── slist 登録 & リスナー開始 ── */
    sys_slist_append(&widgets, &widget->node);
    widget_layer_slider_init();

    return 0;
}

lv_obj_t *zmk_widget_layer_slider_obj(struct zmk_widget_layer_slider *widget)
{
    return widget->obj;
}

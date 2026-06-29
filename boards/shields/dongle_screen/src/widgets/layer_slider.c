/*
 * Copyright (c) 2024 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 *
 * layer_slider.c
 *
 * レイヤー切替に連動して「横方向」にスライドアニメーションするウィジェット。
 * 参考元の layer_roller（lv_roller による縦スクロール）を横スクロールで再実装。
 *
 * ─────────────────────────────────────────────────────────────
 * 動作概要
 * ─────────────────────────────────────────────────────────────
 *  ・lv_roller は縦方向専用のため、横スクロールには lv_label + lv_anim を使う。
 *  ・外枠コンテナ（obj）は LVGL8 のデフォルト（子をクリップ）を利用し、
 *    内部のラベルがはみ出さないようにする。
 *  ・内部に「前レイヤー」「現在レイヤー」「次レイヤー」の3つのラベルを横並びに配置。
 *  ・レイヤー変更時に lv_anim で x 座標を動かし、目的のラベルがセンターに来るようにする。
 *  ・アニメーション完了後にラベル内容を組み換え（無限スクロール風）。
 *
 * ─────────────────────────────────────────────────────────────
 * レイアウト（WIDGET_W = 表示幅、ITEM_W = 各ラベル幅）
 * ─────────────────────────────────────────────────────────────
 *
 *   ┌──────────────────────────────────┐  ← obj (WIDGET_W, クリップ)
 *   │  [prev]      [current]    [next] │
 *   └──────────────────────────────────┘
 *     x=-ITEM_W    x=0         x=+ITEM_W
 *
 *   ITEM_W == WIDGET_W とすると1画面に1項目が収まり、
 *   アニメーションで ±ITEM_W だけ track を動かす。
 *
 * ─────────────────────────────────────────────────────────────
 * カスタマイズ箇所
 * ─────────────────────────────────────────────────────────────
 *  WIDGET_W / WIDGET_H  : ウィジェット全体のサイズ
 *  ITEM_W               : 各レイヤー名ラベルの幅（通常 WIDGET_W と同じ）
 *  ANIM_DURATION_MS     : スライドアニメーション時間（ms）
 *  layer_colors[]       : レイヤーインデックスごとの文字色
 */

#include "layer_slider.h"

#include <ctype.h>
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

#define WIDGET_W        240   /* コンテナ（表示領域）の幅 [px] */
#define WIDGET_H         60   /* コンテナ（表示領域）の高さ [px] */
#define ITEM_W          WIDGET_W  /* 1スロットの幅（= 表示幅で1項目が収まる） */
#define ANIM_DURATION_MS 350  /* スライドアニメーション時間 [ms] */

/* track コンテナ内の3ラベルの x 原点 */
#define X_PREV   (-ITEM_W)
#define X_CUR    (0)
#define X_NEXT   (ITEM_W)

/* ------------------------------------------------------------------ */
/* レイヤーカラー設定                                                    */
/* ------------------------------------------------------------------ */

/* 反映先 layer_status.c の layer_colors[] に合わせる */
static const uint32_t layer_colors[] = {
    0x8CFFDE, /* Layer 0 */
    0xA3FF7F, /* Layer 1 */
    0x7FE3FF, /* Layer 2 */
    0xFFFFFF, /* Layer 3 */
    0x8CFFDE, /* Layer 4 */
    0xA3FF7F, /* Layer 5 */
    0x7FE3FF, /* Layer 6 */
    0xFFFFFF, /* Layer 7 */
    0xFFFB8C, /* Layer 8 */
    0xFF9B7F, /* Layer 9 */
    0xDE8CFF, /* Layer 10 */
    0xFF0000, /* Layer 11 */
};

/* ------------------------------------------------------------------ */
/* 内部状態                                                             */
/* ------------------------------------------------------------------ */

struct layer_slider_state {
    uint8_t index; /* zmk_keymap_highest_layer_active() の戻り値 */
};

/*
 * per-widget の実行時データ。
 * ウィジェットは通常 1 個だが、slist で複数対応する。
 */
struct layer_slider_runtime {
    lv_obj_t *track;         /* 3ラベルを乗せる横長コンテナ */
    lv_obj_t *label_prev;    /* 左：前のレイヤー */
    lv_obj_t *label_cur;     /* 中央：現在のレイヤー */
    lv_obj_t *label_next;    /* 右：次のレイヤー */
    uint8_t   current_index; /* 直前に表示したレイヤーインデックス */
    bool      anim_running;  /* アニメーション実行中フラグ */
};

/* ウィジェットリスト */
static sys_slist_t widgets = SYS_SLIST_STATIC_INIT(&widgets);

/*
 * zmk_widget_layer_slider は公開ヘッダで定義されており obj のみ持つ。
 * 追加の実行時データは別途管理する（ウィジェットが 1 個前提の簡易実装）。
 */
static struct layer_slider_runtime g_runtime;

/* ------------------------------------------------------------------ */
/* ユーティリティ                                                        */
/* ------------------------------------------------------------------ */

/* レイヤーインデックスに対応する名前を返す（NULL なら番号文字列） */
static void get_layer_name(uint8_t index, char *buf, size_t buf_size)
{
    const char *name = zmk_keymap_layer_name(index);
    if (name && *name) {
        strncpy(buf, name, buf_size - 1);
        buf[buf_size - 1] = '\0';
    } else {
        snprintf(buf, buf_size, "%u", index);
    }
}

/* ラベルにレイヤー名と色を設定する */
static void set_label_for_index(lv_obj_t *label, uint8_t index)
{
    char buf[16];
    get_layer_name(index, buf, sizeof(buf));
    lv_label_set_text(label, buf);

    if (index < ARRAY_SIZE(layer_colors)) {
        lv_obj_set_style_text_color(label, lv_color_hex(layer_colors[index]), LV_PART_MAIN);
    } else {
        lv_obj_set_style_text_color(label, lv_color_white(), LV_PART_MAIN);
    }
}

/* ------------------------------------------------------------------ */
/* アニメーション                                                        */
/* ------------------------------------------------------------------ */

/*
 * track コンテナの x 座標を変化させることで全ラベルが横移動する。
 * lv_anim の exec_cb として使用。
 */
static void anim_x_exec_cb(void *obj, int32_t value)
{
    lv_obj_set_x((lv_obj_t *)obj, value);
}

/*
 * アニメーション完了後のコールバック。
 * ラベルを組み換えて track を x=0 に戻す（無限スクロール風）。
 */
static void anim_ready_cb(lv_anim_t *a)
{
    struct layer_slider_runtime *rt = &g_runtime;
    uint8_t idx = rt->current_index;
    uint8_t total = ZMK_KEYMAP_LAYERS_LEN;

    /* track を原点に戻す */
    lv_obj_set_x(rt->track, 0);

    /*
     * ラベルを再配置:
     *   label_prev = idx-1, label_cur = idx, label_next = idx+1
     * (端では折り返さず最初/最後で止める)
     */
    uint8_t prev_idx = (idx > 0) ? (idx - 1) : idx;
    uint8_t next_idx = (idx < total - 1) ? (idx + 1) : idx;

    set_label_for_index(rt->label_prev, prev_idx);
    set_label_for_index(rt->label_cur,  idx);
    set_label_for_index(rt->label_next, next_idx);

    /* 現在ラベルのフォントを強調（大）に戻す */
    lv_obj_set_style_text_font(rt->label_cur,  &lv_font_montserrat_40, LV_PART_MAIN);
    lv_obj_set_style_text_font(rt->label_prev, &lv_font_montserrat_28, LV_PART_MAIN);
    lv_obj_set_style_text_font(rt->label_next, &lv_font_montserrat_28, LV_PART_MAIN);

    rt->anim_running = false;
}

/*
 * レイヤー変更時にアニメーションを開始する。
 * direction: +1 = 右から左（インデックス増加）、-1 = 左から右（インデックス減少）
 */
static void start_slide_anim(struct layer_slider_runtime *rt, int8_t direction)
{
    if (rt->anim_running) {
        lv_anim_del(rt->track, anim_x_exec_cb);
    }
    rt->anim_running = true;

    int32_t target_x = -direction * ITEM_W; /* 右移動なら track を左へ */

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, rt->track);
    lv_anim_set_exec_cb(&a, anim_x_exec_cb);
    lv_anim_set_time(&a, ANIM_DURATION_MS);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    lv_anim_set_values(&a, 0, target_x);
    lv_anim_set_ready_cb(&a, anim_ready_cb);
    lv_anim_start(&a);
}

/* ------------------------------------------------------------------ */
/* ZMK ウィジェットコールバック                                           */
/* ------------------------------------------------------------------ */

static void layer_slider_set_layer(struct layer_slider_runtime *rt,
                                    struct layer_slider_state state)
{
    uint8_t new_idx = state.index;
    uint8_t old_idx = rt->current_index;
    uint8_t total   = ZMK_KEYMAP_LAYERS_LEN;

    if (new_idx == old_idx) {
        return;
    }

    int8_t direction = (new_idx > old_idx) ? 1 : -1;

    /*
     * アニメーション前に「移動先」ラベルを正しい内容に更新する。
     * direction == +1（右→左）なら label_next を new_idx に設定。
     * direction == -1（左→右）なら label_prev を new_idx に設定。
     */
    if (direction > 0) {
        set_label_for_index(rt->label_next, new_idx);
        /* 次レイヤーが端の場合は次々レイヤーをプリセット（見えないが一貫性のため） */
        set_label_for_index(rt->label_prev, (old_idx > 0) ? (old_idx - 1) : old_idx);
    } else {
        set_label_for_index(rt->label_prev, new_idx);
        set_label_for_index(rt->label_next, (old_idx < total - 1) ? (old_idx + 1) : old_idx);
    }

    rt->current_index = new_idx;

    start_slide_anim(rt, direction);
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
    /* ── 外枠コンテナ（クリッピング用） ── */
    widget->obj = lv_obj_create(parent);
    lv_obj_set_size(widget->obj, WIDGET_W, WIDGET_H);
    lv_obj_set_style_bg_opa(widget->obj, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(widget->obj, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(widget->obj, 0, LV_PART_MAIN);
    lv_obj_clear_flag(widget->obj, LV_OBJ_FLAG_SCROLLABLE);
    /*
     * LVGL8 のデフォルトは子をクリップする（overflow hidden 相当）。
     * LV_OBJ_FLAG_OVERFLOW_VISIBLE を付けなければ自動的にクリップされる。
     * 念のため OVERFLOW_VISIBLE が付いている場合は明示的に外す。
     */
    lv_obj_clear_flag(widget->obj, LV_OBJ_FLAG_OVERFLOW_VISIBLE);

    /* ── track コンテナ（3ラベルを乗せる横長プレート） ── */
    /*
     * track の幅は 3 * ITEM_W。x を ±ITEM_W だけ動かすことでスライド。
     * track は親コンテナ（obj）の子なので、overflow hidden でクリップされる。
     */
    struct layer_slider_runtime *rt = &g_runtime;
    rt->track = lv_obj_create(widget->obj);
    lv_obj_set_size(rt->track, ITEM_W * 3, WIDGET_H);
    lv_obj_set_pos(rt->track, 0, 0);
    lv_obj_set_style_bg_opa(rt->track, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(rt->track, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(rt->track, 0, LV_PART_MAIN);
    lv_obj_clear_flag(rt->track, LV_OBJ_FLAG_SCROLLABLE);

    /* ── 共通ラベルスタイル ── */
    static lv_style_t label_style;
    lv_style_init(&label_style);
    lv_style_set_bg_opa(&label_style, LV_OPA_TRANSP);
    lv_style_set_pad_all(&label_style, 0);

    /* ── 3つのラベルを生成し track 内に横並び配置 ── */
    /* prev ラベル（左） */
    rt->label_prev = lv_label_create(rt->track);
    lv_obj_add_style(rt->label_prev, &label_style, 0);
    lv_obj_set_size(rt->label_prev, ITEM_W, WIDGET_H);
    lv_obj_set_pos(rt->label_prev, X_PREV + ITEM_W, 0); /* track 内の相対座標 */
    lv_obj_set_style_text_align(rt->label_prev, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_font(rt->label_prev, &lv_font_montserrat_28, LV_PART_MAIN);
    lv_obj_set_style_text_color(rt->label_prev,
                                lv_palette_darken(LV_PALETTE_GREY, 2), LV_PART_MAIN);

    /* cur ラベル（中央） */
    rt->label_cur = lv_label_create(rt->track);
    lv_obj_add_style(rt->label_cur, &label_style, 0);
    lv_obj_set_size(rt->label_cur, ITEM_W, WIDGET_H);
    lv_obj_set_pos(rt->label_cur, X_CUR + ITEM_W, 0);
    lv_obj_set_style_text_align(rt->label_cur, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_font(rt->label_cur, &lv_font_montserrat_40, LV_PART_MAIN);
    lv_obj_set_style_text_color(rt->label_cur, lv_color_white(), LV_PART_MAIN);

    /* next ラベル（右） */
    rt->label_next = lv_label_create(rt->track);
    lv_obj_add_style(rt->label_next, &label_style, 0);
    lv_obj_set_size(rt->label_next, ITEM_W, WIDGET_H);
    lv_obj_set_pos(rt->label_next, X_NEXT + ITEM_W, 0);
    lv_obj_set_style_text_align(rt->label_next, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    lv_obj_set_style_text_font(rt->label_next, &lv_font_montserrat_28, LV_PART_MAIN);
    lv_obj_set_style_text_color(rt->label_next,
                                lv_palette_darken(LV_PALETTE_GREY, 2), LV_PART_MAIN);

    /* ── 初期レイヤー名を設定 ── */
    uint8_t idx   = zmk_keymap_highest_layer_active();
    uint8_t total = ZMK_KEYMAP_LAYERS_LEN;
    rt->current_index = idx;
    rt->anim_running  = false;

    uint8_t prev_idx = (idx > 0)          ? (idx - 1) : idx;
    uint8_t next_idx = (idx < total - 1)  ? (idx + 1) : idx;

    set_label_for_index(rt->label_prev, prev_idx);
    set_label_for_index(rt->label_cur,  idx);
    set_label_for_index(rt->label_next, next_idx);

    /* ── slist 登録 & リスナー開始 ── */
    sys_slist_append(&widgets, &widget->node);
    widget_layer_slider_init();

    return 0;
}

lv_obj_t *zmk_widget_layer_slider_obj(struct zmk_widget_layer_slider *widget)
{
    return widget->obj;
}

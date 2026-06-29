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
 *  - lv_label 3枚（prev / cur / next）を横並びに配置した track を
 *    lv_anim で x 方向に動かすことで横スクロールを実現する。
 *  - 外枠コンテナ（obj）で子をクリップし、前後ラベルが見えないようにする。
 *  - アニメーション完了後にラベルを組み換えて原点に戻す（無限スクロール風）。
 *
 * レイヤー名の取得:
 *  - 反映先の layer_status.c と同様に
 *    zmk_keymap_layer_name(zmk_keymap_highest_layer_active()) を使用する。
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

#define WIDGET_W         240  /* 表示領域の幅 [px] */
#define WIDGET_H          60  /* 表示領域の高さ [px] */
#define ITEM_W           WIDGET_W
#define ANIM_DURATION_MS 350

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
    lv_obj_t *track;
    lv_obj_t *label_prev;
    lv_obj_t *label_cur;
    lv_obj_t *label_next;
    uint8_t   shown_index;   /* アニメーション完了後に表示中のインデックス */
    uint8_t   target_index;  /* アニメーション中の目標インデックス */
    bool      anim_running;
};

static sys_slist_t widgets = SYS_SLIST_STATIC_INIT(&widgets);
static struct layer_slider_runtime g_runtime;

/* ------------------------------------------------------------------ */
/* ユーティリティ                                                        */
/* ------------------------------------------------------------------ */

static void get_layer_name(uint8_t index, char *buf, size_t buf_size)
{
    /* 反映先 layer_status.c と同じ取得方法 */
    const char *name = zmk_keymap_layer_name(index);
    if (name && *name) {
        strncpy(buf, name, buf_size - 1);
        buf[buf_size - 1] = '\0';
    } else {
        snprintf(buf, buf_size, "%u", (unsigned)index);
    }
}

static void apply_label(lv_obj_t *label, uint8_t index, bool is_current)
{
    char buf[16];
    get_layer_name(index, buf, sizeof(buf));
    lv_label_set_text(label, buf);

    lv_color_t color;
    if (index < ARRAY_SIZE(layer_colors)) {
        color = lv_color_hex(layer_colors[index]);
    } else {
        color = lv_color_white();
    }

    if (is_current) {
        lv_obj_set_style_text_color(label, color, LV_PART_MAIN);
        lv_obj_set_style_text_font(label, &lv_font_montserrat_40, LV_PART_MAIN);
        lv_obj_set_style_text_opa(label, LV_OPA_COVER, LV_PART_MAIN);
    } else {
        /* 前後レイヤーは暗くして小さめに表示 */
        lv_obj_set_style_text_color(label, lv_palette_darken(LV_PALETTE_GREY, 1), LV_PART_MAIN);
        lv_obj_set_style_text_font(label, &lv_font_montserrat_20, LV_PART_MAIN);
        lv_obj_set_style_text_opa(label, LV_OPA_50, LV_PART_MAIN);
    }
}

/* ------------------------------------------------------------------ */
/* アニメーション                                                        */
/* ------------------------------------------------------------------ */

static void anim_x_exec_cb(void *obj, int32_t value)
{
    lv_obj_set_x((lv_obj_t *)obj, value);
}

static void anim_ready_cb(lv_anim_t *a)
{
    struct layer_slider_runtime *rt = &g_runtime;
    uint8_t idx   = rt->target_index;
    uint8_t total = ZMK_KEYMAP_LAYERS_LEN;

    rt->shown_index  = idx;
    rt->anim_running = false;

    /* track を原点に戻す */
    lv_obj_set_x(rt->track, 0);

    /* ラベルを組み換え: prev=idx-1, cur=idx, next=idx+1 */
    uint8_t prev_idx = (idx > 0)          ? (idx - 1) : idx;
    uint8_t next_idx = (idx < total - 1)  ? (idx + 1) : idx;

    /* track 内の各ラベルを原点位置に再配置してから内容を設定 */
    lv_obj_set_pos(rt->label_prev, 0,        0);
    lv_obj_set_pos(rt->label_cur,  ITEM_W,   0);
    lv_obj_set_pos(rt->label_next, ITEM_W*2, 0);

    apply_label(rt->label_prev, prev_idx, false);
    apply_label(rt->label_cur,  idx,      true);
    apply_label(rt->label_next, next_idx, false);
}

static void start_slide_anim(struct layer_slider_runtime *rt, int8_t direction)
{
    if (rt->anim_running) {
        lv_anim_del(rt->track, anim_x_exec_cb);
        /* 中断された場合は track を原点に戻してからやり直す */
        lv_obj_set_x(rt->track, 0);
    }
    rt->anim_running = true;

    /* direction > 0: インデックス増加 → track を左へ動かす（target_x < 0） */
    int32_t target_x = -(int32_t)direction * ITEM_W;

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
    uint8_t old_idx = rt->shown_index;
    uint8_t total   = ZMK_KEYMAP_LAYERS_LEN;

    if (new_idx == old_idx && !rt->anim_running) {
        return;
    }

    int8_t direction = (new_idx >= old_idx) ? 1 : -1;

    rt->target_index = new_idx;

    /*
     * アニメーション開始前に「スライドインしてくる側」のラベルを
     * new_idx の内容に更新する。
     *
     * track のレイアウト（origin 状態）:
     *   pos 0       : label_prev  (x = 0)
     *   pos ITEM_W  : label_cur   (x = ITEM_W)   ← 現在表示中
     *   pos ITEM_W*2: label_next  (x = ITEM_W*2)
     *
     * direction > 0（右→左）: label_next が新しいレイヤー名を表示
     * direction < 0（左→右）: label_prev が新しいレイヤー名を表示
     */
    if (direction > 0) {
        /* 次のレイヤーを label_next にセット */
        apply_label(rt->label_next, new_idx, true);
        /* prev には old の前を表示しておく（クリップされて見えないが整合性のため） */
        uint8_t prev_of_old = (old_idx > 0) ? (old_idx - 1) : old_idx;
        apply_label(rt->label_prev, prev_of_old, false);
        /* cur は現在のまま（アニメーション中に見える） */
        apply_label(rt->label_cur, old_idx, false);
    } else {
        /* 前のレイヤーを label_prev にセット */
        apply_label(rt->label_prev, new_idx, true);
        /* next には old の次を表示しておく */
        uint8_t next_of_old = (old_idx < total - 1) ? (old_idx + 1) : old_idx;
        apply_label(rt->label_next, next_of_old, false);
        apply_label(rt->label_cur, old_idx, false);
    }

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

/*
 * lv_obj_create で作ったコンテナはデフォルトで以下が有効になっている:
 *   - padding (8px 程度)
 *   - scrollable
 *   - border
 * これらをすべてリセットしないと子の位置がズレる。
 */
static void reset_container_style(lv_obj_t *obj)
{
    lv_obj_set_style_bg_opa(obj,      LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(obj, 0,            LV_PART_MAIN);
    lv_obj_set_style_pad_top(obj,     0,             LV_PART_MAIN);
    lv_obj_set_style_pad_bottom(obj,  0,             LV_PART_MAIN);
    lv_obj_set_style_pad_left(obj,    0,             LV_PART_MAIN);
    lv_obj_set_style_pad_right(obj,   0,             LV_PART_MAIN);
    lv_obj_set_style_outline_width(obj, 0,           LV_PART_MAIN);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_OVERFLOW_VISIBLE);
}

int zmk_widget_layer_slider_init(struct zmk_widget_layer_slider *widget, lv_obj_t *parent)
{
    struct layer_slider_runtime *rt = &g_runtime;

    /* ── 外枠コンテナ（クリッピング境界） ── */
    widget->obj = lv_obj_create(parent);
    lv_obj_set_size(widget->obj, WIDGET_W, WIDGET_H);
    reset_container_style(widget->obj);

    /* ── track（3ラベルを乗せる横長プレート） ── */
    rt->track = lv_obj_create(widget->obj);
    lv_obj_set_size(rt->track, ITEM_W * 3, WIDGET_H);
    lv_obj_set_pos(rt->track, 0, 0);
    reset_container_style(rt->track);
    /* track 自身の子はクリップしない（ラベルが track 内に収まっているので不要） */
    lv_obj_add_flag(rt->track, LV_OBJ_FLAG_OVERFLOW_VISIBLE);

    /* ── ラベル生成 ── */
    /*
     * track 内の座標:
     *   label_prev: x=0         （表示領域より左 → 外枠でクリップ）
     *   label_cur:  x=ITEM_W    （表示領域中央 → 見える）
     *   label_next: x=ITEM_W*2  （表示領域より右 → 外枠でクリップ）
     *
     * アニメーションで track.x を -ITEM_W にすると label_cur が左へ消え
     * label_next が中央に来る。
     */
    rt->label_prev = lv_label_create(rt->track);
    lv_obj_set_size(rt->label_prev, ITEM_W, WIDGET_H);
    lv_obj_set_pos(rt->label_prev, 0, 0);
    lv_obj_set_style_text_align(rt->label_prev, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);

    rt->label_cur = lv_label_create(rt->track);
    lv_obj_set_size(rt->label_cur, ITEM_W, WIDGET_H);
    lv_obj_set_pos(rt->label_cur, ITEM_W, 0);
    lv_obj_set_style_text_align(rt->label_cur, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);

    rt->label_next = lv_label_create(rt->track);
    lv_obj_set_size(rt->label_next, ITEM_W, WIDGET_H);
    lv_obj_set_pos(rt->label_next, ITEM_W * 2, 0);
    lv_obj_set_style_text_align(rt->label_next, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);

    /* ── 初期表示 ── */
    uint8_t idx   = zmk_keymap_highest_layer_active();
    uint8_t total = ZMK_KEYMAP_LAYERS_LEN;
    rt->shown_index  = idx;
    rt->target_index = idx;
    rt->anim_running = false;

    uint8_t prev_idx = (idx > 0)         ? (idx - 1) : idx;
    uint8_t next_idx = (idx < total - 1) ? (idx + 1) : idx;

    apply_label(rt->label_prev, prev_idx, false);
    apply_label(rt->label_cur,  idx,      true);
    apply_label(rt->label_next, next_idx, false);

    /* ── slist 登録 & リスナー開始 ── */
    sys_slist_append(&widgets, &widget->node);
    widget_layer_slider_init();

    return 0;
}

lv_obj_t *zmk_widget_layer_slider_obj(struct zmk_widget_layer_slider *widget)
{
    return widget->obj;
}

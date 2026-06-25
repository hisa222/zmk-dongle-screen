/*
 * Copyright (c) 2024 The ZMK Contributors
 * SPDX-License-Identifier: MIT
 *
 * Brightness Screen Widget (LVGL8 / ZMK 3.5)
 *
 * Prospector (LVGL9/ZMK4) から移植した設計方針:
 *
 * [1] カスタムスライダードラッグ
 *   LVGL のデフォルトスライダードラッグは lvgl_input_read() の座標変換後の
 *   論理座標でドラッグ量を計算するため、90° 回転ディスプレイ環境では
 *   タッチ移動量と値変化量が一致しないケースがある。
 *   → LV_EVENT_PRESSING で lv_indev_get_act() / lv_indev_get_point() を使い
 *     論理 delta_x から手動でスライダー値を計算するカスタムドラッグを実装。
 *
 * [2] 縦スワイプ検出とキャンセル
 *   スライダードラッグ中に縦方向の移動が閾値を超えたら drag_cancelled=true にし、
 *   ui_interaction_active を false に戻すことでスワイプ遷移を通過させる。
 *
 * [3] display_settings_is_interacting() の実装
 *   touch_handler.c の weak 関数をオーバーライド。
 *   brightness_screen が表示中で、かつカスタムドラッグ中 (s_drag.active &&
 *   !s_drag.drag_cancelled) の場合に true を返し、スワイプをブロックする。
 *
 * [4] LVGL8 対応
 *   - lv_indev_get_act()  (LVGL9: lv_indev_active())
 *   - lv_indev_get_point()
 *   - ui_interaction_active は custom_status_screen.c で定義済みの volatile bool を
 *     extern 参照する。
 *
 * スライダー配置 (論理 280×240, ROTATED_270):
 *   lv_obj_align(slider, LV_ALIGN_CENTER, 0, 50)
 *   → 論理X: slider中央付近, 論理Y: ~170 (ext_click=24込みで ~146〜202)
 *   生タッチ座標との対応 (logical_y = 239 - current_x):
 *     論理Y 146 → raw_x = 93
 *     論理Y 202 → raw_x = 37
 */

#include "brightness_screen.h"
#include "../custom_status_screen.h"
#include "../display_settings.h"

#include <zephyr/logging/log.h>
#include <errno.h>
#include <stdio.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

/* ================================================================== */
/* 実バックライト反映                                                  */
/* ================================================================== */

/*
 * brightness.c で定義されている set_screen_brightness() を extern で参照する。
 * (void) でインクルードを避けて直接宣言。
 */
extern void set_screen_brightness(uint8_t value, bool ambient);

/* ================================================================== */
/* スクリーンアクティブフラグ                                          */
/* ================================================================== */

static bool brightness_screen_active = false;

/* ================================================================== */
/* display_settings_is_interacting() — touch_handler weak をオーバーライド */
/* ================================================================== */

/*
 * touch_handler.c の raise_swipe_event() から呼ばれる。
 * true を返すとスワイプイベントの発生をブロックする。
 *
 * 条件:
 *   1. brightness_screen が表示中
 *   2. カスタムドラッグが進行中 (s_drag.active && !s_drag.drag_cancelled)
 *
 * NOTE: この関数は Zephyr ISR スレッドから呼ばれる場合があるため
 *       LVGL API を呼んではいけない。フラグ参照のみ。
 */
bool display_settings_is_interacting(void);  /* suppress -Wmissing-prototypes */

/* ================================================================== */
/* カスタムスライダードラッグ状態                                      */
/* ================================================================== */

#define SLIDER_SWIPE_THRESHOLD 30

static struct {
    bool    active;
    int32_t start_x;
    int32_t start_y;
    int32_t start_value;
    int32_t current_value;
    int32_t min_val;
    int32_t max_val;
    int32_t slider_width;
    bool    drag_cancelled;
} s_drag = {0};

/* display_settings_is_interacting の実体 */
bool display_settings_is_interacting(void)
{
    if (!brightness_screen_active) {
        return false;
    }
    /* ドラッグが進行中 (キャンセルされていない) 場合のみブロック */
    return s_drag.active && !s_drag.drag_cancelled;
}

/* ================================================================== */
/* カスタムスライダードラッグコールバック                              */
/* ================================================================== */

/*
 * LV_EVENT_PRESSED  : ドラッグ開始状態を記録、ui_interaction_active = true
 * LV_EVENT_PRESSING : 横ドラッグ → 値更新 / 縦スワイプ → キャンセル
 * LV_EVENT_RELEASED : 最終値を確定、ui_interaction_active = false
 *
 * user_data: value_label (lv_obj_t*) — PRESSING 中にリアルタイム更新する
 */
static void slider_custom_drag_cb(lv_event_t *e)
{
    lv_event_code_t code   = lv_event_get_code(e);
    lv_obj_t       *slider = lv_event_get_target(e);

    /* LVGL8: lv_indev_get_act() でアクティブな indev を取得 */
    lv_indev_t *indev = lv_indev_get_act();
    if (!indev) {
        return;
    }

    lv_point_t point;
    lv_indev_get_point(indev, &point);

    if (code == LV_EVENT_PRESSED) {
        /* ドラッグ開始 */
        s_drag.active         = true;
        s_drag.start_x        = point.x;
        s_drag.start_y        = point.y;
        s_drag.start_value    = lv_slider_get_value(slider);
        s_drag.current_value  = s_drag.start_value;
        s_drag.min_val        = lv_slider_get_min_value(slider);
        s_drag.max_val        = lv_slider_get_max_value(slider);
        s_drag.slider_width   = lv_obj_get_width(slider);
        s_drag.drag_cancelled = false;

        /* スワイプ遷移をブロックする */
        ui_interaction_active = true;

        LOG_DBG("Slider drag start: logical(%d,%d) value=%d",
                (int)point.x, (int)point.y, (int)s_drag.start_value);

    } else if (code == LV_EVENT_PRESSING) {
        if (!s_drag.active || s_drag.drag_cancelled) {
            return;
        }

        int32_t delta_x = point.x - s_drag.start_x;
        int32_t delta_y = point.y - s_drag.start_y;
        int32_t abs_dx  = (delta_x < 0) ? -delta_x : delta_x;
        int32_t abs_dy  = (delta_y < 0) ? -delta_y : delta_y;

        /*
         * 縦スワイプ検出: Y移動が閾値超かつ X移動の2倍以上
         * → ドラッグをキャンセルして画面遷移スワイプを優先する
         */
        if (abs_dy > SLIDER_SWIPE_THRESHOLD && abs_dy > abs_dx * 2) {
            LOG_INF("Vertical swipe on slider - cancelling drag");
            lv_slider_set_value(slider, s_drag.start_value, LV_ANIM_OFF);
            s_drag.current_value  = s_drag.start_value;
            s_drag.drag_cancelled = true;
            /*
             * ui_interaction_active を false にしてスワイプが通過できるようにする。
             * display_settings_is_interacting() も drag_cancelled=true で false を返す。
             */
            ui_interaction_active = false;
            return;
        }

        /* 横ドラッグ: delta_x からスライダー値を手動計算 */
        if (s_drag.slider_width == 0) {
            return;
        }

        int32_t value_range = s_drag.max_val - s_drag.min_val;
        int32_t value_delta = (delta_x * value_range) / s_drag.slider_width;
        int32_t new_value   = s_drag.start_value + value_delta;

        if (new_value < s_drag.min_val) { new_value = s_drag.min_val; }
        if (new_value > s_drag.max_val) { new_value = s_drag.max_val; }

        s_drag.current_value = new_value;
        lv_slider_set_value(slider, new_value, LV_ANIM_OFF);

        /* ラベルとバックライトをリアルタイム更新 */
        lv_obj_t *value_label = (lv_obj_t *)lv_event_get_user_data(e);
        if (value_label) {
            lv_label_set_text_fmt(value_label, "%d%%", (int)new_value);
        }
        set_screen_brightness((uint8_t)new_value, false);

        LOG_DBG("Slider dragging: delta_x=%d value=%d",
                (int)delta_x, (int)new_value);

    } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        if (!s_drag.active) {
            return;
        }

        /*
         * LVGL デフォルトハンドラが座標変換の影響で誤った値を書き込む
         * 場合があるため、カスタム計算値で上書きする。
         */
        lv_slider_set_value(slider, s_drag.current_value, LV_ANIM_OFF);

        bool was_cancelled    = s_drag.drag_cancelled;
        s_drag.active         = false;
        s_drag.drag_cancelled = false;
        ui_interaction_active = false;

        if (!was_cancelled) {
            /* 永続保存 */
            (void)display_settings_set_brightness((uint8_t)s_drag.current_value);
            (void)display_settings_save();
            LOG_INF("Slider drag end: value=%d (saved)", (int)s_drag.current_value);
        } else {
            LOG_DBG("Slider drag cancelled");
        }
    }
}

/* ================================================================== */
/* iOS 風スタイルヘルパー                                              */
/* ================================================================== */

static void apply_slider_style(lv_obj_t *slider)
{
    /* トラック */
    lv_obj_set_style_radius(slider, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(slider, lv_color_hex(0x3A3A3C), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(slider, 0, LV_PART_MAIN);

    /* インジケータ */
    lv_obj_set_style_radius(slider, LV_RADIUS_CIRCLE, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(slider, lv_color_hex(0x007AFF), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_INDICATOR);

    /* ノブ */
    lv_obj_set_style_radius(slider, LV_RADIUS_CIRCLE, LV_PART_KNOB);
    lv_obj_set_style_bg_color(slider, lv_color_white(), LV_PART_KNOB);
    lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_KNOB);
    lv_obj_set_style_pad_all(slider, 10, LV_PART_KNOB);
    lv_obj_set_style_border_width(slider, 0, LV_PART_KNOB);
}

/* ================================================================== */
/* Widget init                                                         */
/* ================================================================== */

int zmk_widget_brightness_screen_init(struct zmk_widget_brightness_screen *widget,
                                      lv_obj_t *parent)
{
    if (!widget || !parent) {
        return -EINVAL;
    }

    display_settings_init();

    widget->obj = parent;

    /* ---- タイトル ---- */
    widget->title_label = lv_label_create(parent);
    if (!widget->title_label) { return -ENOMEM; }
    lv_label_set_text(widget->title_label, "Brightness");
    lv_obj_set_style_text_color(widget->title_label,
                                lv_color_hex(0xFFFFFF), LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(widget->title_label,
                               &lv_font_montserrat_20, LV_STATE_DEFAULT);
    lv_obj_align(widget->title_label, LV_ALIGN_TOP_MID, 0, 14);

    /* ---- 輝度値ラベル "80%" ---- */
    widget->value_label = lv_label_create(parent);
    if (!widget->value_label) { return -ENOMEM; }
    lv_obj_set_style_text_color(widget->value_label,
                                lv_color_hex(0x007AFF), LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(widget->value_label,
                               &lv_font_montserrat_40, LV_STATE_DEFAULT);
    lv_label_set_text_fmt(widget->value_label, "%d%%",
                          display_settings_get_brightness());
    lv_obj_align(widget->value_label, LV_ALIGN_CENTER, 0, -20);

    /* ---- スライダー ---- */
    widget->slider = lv_slider_create(parent);
    if (!widget->slider) { return -ENOMEM; }
    lv_obj_set_size(widget->slider, 200, 8);
    lv_obj_align(widget->slider, LV_ALIGN_CENTER, 0, 50);
    lv_slider_set_range(widget->slider,
                        CONFIG_DONGLE_SCREEN_MIN_BRIGHTNESS,
                        CONFIG_DONGLE_SCREEN_MAX_BRIGHTNESS);
    lv_slider_set_value(widget->slider,
                        display_settings_get_brightness(),
                        LV_ANIM_OFF);
    lv_obj_set_ext_click_area(widget->slider, 24);

    apply_slider_style(widget->slider);

    /*
     * LVGL デフォルトのスクロール・クリックを無効化し、
     * カスタムドラッグのみで制御する。
     */
    lv_obj_clear_flag(widget->slider, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(widget->slider, LV_OBJ_FLAG_CLICKABLE);

    /*
     * カスタムドラッグハンドラを登録。
     * user_data として value_label を渡し、PRESSING 中にリアルタイム更新する。
     */
    lv_obj_add_event_cb(widget->slider, slider_custom_drag_cb,
                        LV_EVENT_PRESSED,    widget->value_label);
    lv_obj_add_event_cb(widget->slider, slider_custom_drag_cb,
                        LV_EVENT_PRESSING,   widget->value_label);
    lv_obj_add_event_cb(widget->slider, slider_custom_drag_cb,
                        LV_EVENT_RELEASED,   widget->value_label);
    lv_obj_add_event_cb(widget->slider, slider_custom_drag_cb,
                        LV_EVENT_PRESS_LOST, widget->value_label);

    /* ---- 左アイコン (暗い) ---- */
    widget->icon_low = lv_label_create(parent);
    if (!widget->icon_low) { return -ENOMEM; }
    lv_label_set_text(widget->icon_low, LV_SYMBOL_MINUS);
    lv_obj_set_style_text_color(widget->icon_low,
                                lv_color_hex(0x666666), LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(widget->icon_low,
                               &lv_font_montserrat_20, LV_STATE_DEFAULT);
    lv_obj_align_to(widget->icon_low, widget->slider,
                    LV_ALIGN_OUT_LEFT_MID, -14, 0);

    /* ---- 右アイコン (明るい) ---- */
    widget->icon_high = lv_label_create(parent);
    if (!widget->icon_high) { return -ENOMEM; }
    lv_label_set_text(widget->icon_high, LV_SYMBOL_PLUS);
    lv_obj_set_style_text_color(widget->icon_high,
                                lv_color_hex(0xFFFFFF), LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(widget->icon_high,
                               &lv_font_montserrat_20, LV_STATE_DEFAULT);
    lv_obj_align_to(widget->icon_high, widget->slider,
                    LV_ALIGN_OUT_RIGHT_MID, 14, 0);

    /* ---- ナビゲーションヒント ---- */
    widget->nav_hint = lv_label_create(parent);
    if (!widget->nav_hint) { return -ENOMEM; }
    lv_label_set_text(widget->nav_hint, "< swipe >");
    lv_obj_set_style_text_color(widget->nav_hint,
                                lv_color_hex(0x444444), LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(widget->nav_hint,
                               &lv_font_montserrat_20, LV_STATE_DEFAULT);
    lv_obj_align(widget->nav_hint, LV_ALIGN_BOTTOM_MID, 0, -10);

    return 0;
}

/* ================================================================== */
/* Show / Hide                                                         */
/* ================================================================== */

void zmk_widget_brightness_screen_show(struct zmk_widget_brightness_screen *widget)
{
    if (!widget || !widget->slider) {
        return;
    }

    brightness_screen_active = true;

    /* ドラッグ状態をリセット */
    s_drag.active         = false;
    s_drag.drag_cancelled = false;
    ui_interaction_active = false;

    /* 現在の保存値でスライダーとラベルを同期 */
    uint8_t val = display_settings_get_brightness();
    lv_slider_set_value(widget->slider, val, LV_ANIM_OFF);
    if (widget->value_label) {
        lv_label_set_text_fmt(widget->value_label, "%d%%", val);
    }
}

void zmk_widget_brightness_screen_hide(struct zmk_widget_brightness_screen *widget)
{
    ARG_UNUSED(widget);

    brightness_screen_active = false;

    /* ドラッグ状態をリセット */
    s_drag.active         = false;
    s_drag.drag_cancelled = false;
    ui_interaction_active = false;
}

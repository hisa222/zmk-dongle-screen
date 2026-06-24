/*
 * Copyright (c) 2024 The ZMK Contributors
 * SPDX-License-Identifier: MIT
 *
 * Brightness Screen Widget
 *
 * 設計方針:
 *   parent はスクリーン自体（lv_obj_create(NULL)）。
 *   中間コンテナを作らず、全 LVGL オブジェクトを直接 parent に配置する。
 *
 * [修正] スライダーがスワイプとして誤認識される問題の解決。
 *
 *   根本原因:
 *     touch_input_callback (Zephyrスレッド) と LVGL処理 (専用スレッド) は非同期。
 *     タッチ DOWN → UP の間に LVGL が LV_EVENT_PRESSED を処理する保証がなく、
 *     LVGLイベント起点で slider_dragging=true にするアプローチは
 *     タッチ UP 時の swipe 判定に間に合わないケースがある。
 *
 *   解決策:
 *     display_settings_is_interacting(raw_x, raw_y) を生タッチ座標ベースで実装。
 *     touch_handler.c から DOWN 時に直接呼ばれ、スライダー領域なら即座に
 *     swipe_already_raised=true にしてスワイプをブロックする。
 *     LVGLイベントに依存しないためスレッド競合が発生しない。
 *
 *   スライダー座標 (論理 280x240、ROTATED_270):
 *     lv_obj_align(slider, LV_ALIGN_CENTER, 0, 50)
 *     → 論理X: 40〜240 (ext_click=24込みで 16〜264)
 *     → 論理Y: 170〜178 (ext_click=24込みで 146〜202)
 *
 *   生座標との対応 (lvgl_input_read: logical_x=current_y, logical_y=239-current_x):
 *     論理Y 146〜202 → current_x: 37〜93
 *     論理X 16〜264  → current_y: 16〜264 (ほぼ全域のため X のみで判定)
 *
 *   スライダードラッグ:
 *     LVGL8 デフォルトのスライダードラッグは lvgl_input_read の変換後座標で
 *     ドラッグ量を計算するため、座標変換の影響で追従しないケースがある。
 *     LV_EVENT_PRESSING で lv_indev_get_act() / lv_indev_get_point() を使い、
 *     論理座標の delta_x から手動で値を計算するカスタムドラッグを実装する。
 */

#include "brightness_screen.h"
#include "../display_settings.h"

#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

extern void set_screen_brightness(uint8_t value, bool ambient);

/* ------------------------------------------------------------------ */
/* スライダー画面アクティブフラグ                                      */
/* ------------------------------------------------------------------ */

/* brightness_screen が現在表示中かどうか */
static bool brightness_screen_active = false;

/*
 * [修正] display_settings_is_interacting(raw_x, raw_y)
 *
 * touch_handler.c の weak 関数をオーバーライド。
 * 生タッチ座標を受け取り、スライダー領域かどうかを直接判定する。
 *
 * 判定条件:
 *   1. brightness_screen が表示中であること
 *   2. raw_x が 37〜93 の範囲 (論理Y 146〜202 に対応、スライダーY範囲)
 *
 * この関数は Zephyrスレッド (touch_input_callback) から呼ばれるため、
 * LVGL操作を行ってはいけない。フラグ参照のみ。
 */
bool display_settings_is_interacting(uint16_t raw_x, uint16_t raw_y) {
    ARG_UNUSED(raw_y);

    if (!brightness_screen_active) {
        return false;
    }

    /*
     * スライダーの論理Y範囲 (ext_click=24込み): 146〜202
     * 論理Y = 239 - raw_x  →  raw_x = 239 - 論理Y
     * 論理Y=146 → raw_x=93
     * 論理Y=202 → raw_x=37
     * よって raw_x: 37〜93 がスライダーY範囲に対応する
     */
    if (raw_x >= 37 && raw_x <= 93) {
        return true;
    }

    return false;
}

/* ------------------------------------------------------------------ */
/* カスタムスライダードラッグ                                          */
/* ------------------------------------------------------------------ */

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

/*
 * カスタムスライダードラッグハンドラ
 *
 * LVGL8: lv_indev_get_act() でアクティブな indev を取得し、
 *        lv_indev_get_point() で論理座標を読む。
 *
 * LV_EVENT_PRESSED  : ドラッグ開始状態を記録
 * LV_EVENT_PRESSING : 横ドラッグ → 値更新 / 縦スワイプ → キャンセル
 * LV_EVENT_RELEASED : 最終値を確定して NVS に保存
 */
static void slider_custom_drag_cb(lv_event_t *e)
{
    lv_event_code_t code   = lv_event_get_code(e);
    lv_obj_t       *slider = lv_event_get_target(e);

    /* LVGL8: lv_indev_get_act() */
    lv_indev_t *indev = lv_indev_get_act();
    if (!indev) return;

    lv_point_t point;
    lv_indev_get_point(indev, &point);

    if (code == LV_EVENT_PRESSED) {
        s_drag.active        = true;
        s_drag.start_x       = point.x;
        s_drag.start_y       = point.y;
        s_drag.start_value   = lv_slider_get_value(slider);
        s_drag.current_value = s_drag.start_value;
        s_drag.min_val       = lv_slider_get_min_value(slider);
        s_drag.max_val       = lv_slider_get_max_value(slider);
        s_drag.slider_width  = lv_obj_get_width(slider);
        s_drag.drag_cancelled = false;

        LOG_DBG("Slider drag start: logical(%d,%d) value=%d",
                (int)point.x, (int)point.y, (int)s_drag.start_value);

    } else if (code == LV_EVENT_PRESSING) {
        if (!s_drag.active || s_drag.drag_cancelled) return;

        int32_t delta_x = point.x - s_drag.start_x;
        int32_t delta_y = point.y - s_drag.start_y;
        int32_t abs_dx  = (delta_x < 0) ? -delta_x : delta_x;
        int32_t abs_dy  = (delta_y < 0) ? -delta_y : delta_y;

        /*
         * 縦スワイプ検出: Y移動が閾値超かつ X移動の2倍以上
         * → ドラッグをキャンセルして画面遷移スワイプを優先する
         * 注意: display_settings_is_interacting() は touch_handler 側で
         *       DOWN時に既にブロックしているが、ドラッグ中に大きく縦に
         *       ずれた場合にも値を元に戻す保険として残す
         */
        if (abs_dy > SLIDER_SWIPE_THRESHOLD && abs_dy > abs_dx * 2) {
            LOG_INF("Vertical swipe on slider - cancelling drag");
            lv_slider_set_value(slider, s_drag.start_value, LV_ANIM_OFF);
            s_drag.current_value  = s_drag.start_value;
            s_drag.drag_cancelled = true;
            return;
        }

        /* 横ドラッグ: delta_x から値を手動計算 */
        if (s_drag.slider_width == 0) return;

        int32_t value_range = s_drag.max_val - s_drag.min_val;
        int32_t value_delta = (delta_x * value_range) / s_drag.slider_width;
        int32_t new_value   = s_drag.start_value + value_delta;

        if (new_value < s_drag.min_val) new_value = s_drag.min_val;
        if (new_value > s_drag.max_val) new_value = s_drag.max_val;

        s_drag.current_value = new_value;
        lv_slider_set_value(slider, new_value, LV_ANIM_OFF);

        /* ラベルとバックライトをリアルタイム更新 */
        lv_obj_t *value_label = lv_event_get_user_data(e);
        if (value_label) {
            lv_label_set_text_fmt(value_label, "%d%%", (int)new_value);
        }
        set_screen_brightness((uint8_t)new_value, false);

        LOG_DBG("Slider dragging: delta_x=%d value=%d",
                (int)delta_x, (int)new_value);

    } else if (code == LV_EVENT_RELEASED) {
        if (!s_drag.active) return;

        /*
         * LVGL デフォルトハンドラが座標変換の影響で誤った値を
         * 書き込む場合があるため、カスタム計算値で上書きする。
         */
        lv_slider_set_value(slider, s_drag.current_value, LV_ANIM_OFF);

        bool was_cancelled    = s_drag.drag_cancelled;
        s_drag.active         = false;
        s_drag.drag_cancelled = false;

        if (!was_cancelled) {
            display_settings_set_manual_brightness((uint8_t)s_drag.current_value);
            display_settings_save_if_dirty();
            LOG_INF("Slider drag end: value=%d (saved)", (int)s_drag.current_value);
        } else {
            LOG_DBG("Slider drag cancelled");
        }
    }
}

/* ------------------------------------------------------------------ */
/* iOS 風スタイル                                                      */
/* ------------------------------------------------------------------ */
static void apply_slider_style(lv_obj_t *slider)
{
    lv_obj_set_style_radius(slider, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(slider, lv_color_hex(0x3A3A3C), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_width(slider, 0, LV_PART_MAIN);

    lv_obj_set_style_radius(slider, LV_RADIUS_CIRCLE, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(slider, lv_color_hex(0x007AFF), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_INDICATOR);

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
    if (!parent) return -EINVAL;

    display_settings_init();

    widget->obj = parent;

    /* ---- タイトル ---- */
    widget->title_label = lv_label_create(parent);
    lv_label_set_text(widget->title_label, "Brightness");
    lv_obj_set_style_text_color(widget->title_label,
                                lv_color_hex(0xFFFFFF), LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(widget->title_label,
                               &lv_font_montserrat_20, LV_STATE_DEFAULT);
    lv_obj_align(widget->title_label, LV_ALIGN_TOP_MID, 0, 14);

    /* ---- 輝度値ラベル "80%" ---- */
    widget->value_label = lv_label_create(parent);
    lv_obj_set_style_text_color(widget->value_label,
                                lv_color_hex(0x007AFF), LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(widget->value_label,
                               &lv_font_montserrat_40, LV_STATE_DEFAULT);
    lv_label_set_text_fmt(widget->value_label, "%d%%",
                          display_settings_get_manual_brightness());
    lv_obj_align(widget->value_label, LV_ALIGN_CENTER, 0, -20);

    /* ---- スライダー ---- */
    widget->slider = lv_slider_create(parent);
    lv_obj_set_size(widget->slider, 200, 8);
    lv_obj_align(widget->slider, LV_ALIGN_CENTER, 0, 50);
    lv_slider_set_range(widget->slider,
                        CONFIG_DONGLE_SCREEN_MIN_BRIGHTNESS,
                        CONFIG_DONGLE_SCREEN_MAX_BRIGHTNESS);
    lv_slider_set_value(widget->slider,
                        display_settings_get_manual_brightness(),
                        LV_ANIM_OFF);
    lv_obj_set_ext_click_area(widget->slider, 24);

    apply_slider_style(widget->slider);

    /*
     * [修正] カスタムドラッグハンドラを登録。
     *
     * 旧実装 (LVGL デフォルトドラッグ):
     *   lv_obj_add_event_cb(slider, slider_pressed_cb,       LV_EVENT_PRESSED,       NULL);
     *   lv_obj_add_event_cb(slider, slider_released_cb,      LV_EVENT_RELEASED,      NULL);
     *   lv_obj_add_event_cb(slider, slider_value_changed_cb, LV_EVENT_VALUE_CHANGED, value_label);
     *   問題: lvgl_input_read の座標変換後の論理座標でドラッグ量を計算するため、
     *         タッチパネルの物理方向と論理方向のずれでスライダーが追従しない。
     *
     * 新実装 (カスタムドラッグ):
     *   PRESSING イベントで lv_indev_get_point() から論理座標の delta_x を取得し、
     *   slider_width に対する比率でスライダー値を手動計算する。
     *   user_data に value_label を渡して PRESSING 内でリアルタイム更新。
     */
    lv_obj_add_event_cb(widget->slider, slider_custom_drag_cb,
                        LV_EVENT_PRESSED,  widget->value_label);
    lv_obj_add_event_cb(widget->slider, slider_custom_drag_cb,
                        LV_EVENT_PRESSING, widget->value_label);
    lv_obj_add_event_cb(widget->slider, slider_custom_drag_cb,
                        LV_EVENT_RELEASED, widget->value_label);

    /* ---- アイコン ---- */
    widget->icon_low = lv_label_create(parent);
    lv_label_set_text(widget->icon_low, LV_SYMBOL_MINUS);
    lv_obj_set_style_text_color(widget->icon_low,
                                lv_color_hex(0x666666), LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(widget->icon_low,
                               &lv_font_montserrat_20, LV_STATE_DEFAULT);
    lv_obj_align_to(widget->icon_low, widget->slider,
                    LV_ALIGN_OUT_LEFT_MID, -14, 0);

    widget->icon_high = lv_label_create(parent);
    lv_label_set_text(widget->icon_high, LV_SYMBOL_PLUS);
    lv_obj_set_style_text_color(widget->icon_high,
                                lv_color_hex(0xFFFFFF), LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(widget->icon_high,
                               &lv_font_montserrat_20, LV_STATE_DEFAULT);
    lv_obj_align_to(widget->icon_high, widget->slider,
                    LV_ALIGN_OUT_RIGHT_MID, 14, 0);

    /* ---- ナビゲーションヒント ---- */
    widget->nav_hint = lv_label_create(parent);
    lv_label_set_text(widget->nav_hint, "< swipe >");
    lv_obj_set_style_text_color(widget->nav_hint,
                                lv_color_hex(0x444444), LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(widget->nav_hint,
                               &lv_font_montserrat_20, LV_STATE_DEFAULT);
    lv_obj_align(widget->nav_hint, LV_ALIGN_BOTTOM_MID, 0, -10);

    return 0;
}

void zmk_widget_brightness_screen_show(struct zmk_widget_brightness_screen *widget)
{
    if (!widget || !widget->slider) return;

    brightness_screen_active = true;

    uint8_t val = display_settings_get_manual_brightness();
    lv_slider_set_value(widget->slider, val, LV_ANIM_OFF);
    if (widget->value_label) {
        lv_label_set_text_fmt(widget->value_label, "%d%%", val);
    }

    /* ドラッグ状態をリセット */
    s_drag.active         = false;
    s_drag.drag_cancelled = false;
}

void zmk_widget_brightness_screen_hide(struct zmk_widget_brightness_screen *widget)
{
    brightness_screen_active = false;

    /* ドラッグ状態をリセット */
    s_drag.active         = false;
    s_drag.drag_cancelled = false;
}

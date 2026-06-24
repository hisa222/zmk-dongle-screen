/*
 * Copyright (c) 2024 The ZMK Contributors
 * SPDX-License-Identifier: MIT
 *
 * Brightness Screen Widget
 *
 * 設計方針:
 *   parent はスクリーン自体（lv_obj_create(NULL)）。
 *   中間コンテナを作らず、全 LVGL オブジェクトを直接 parent に配置する。
 *   これにより LVGL8 のタッチイベント伝播問題を回避する。
 *
 * スライダー:
 *   [修正] LVGL8 デフォルトのスライダードラッグを廃止し、カスタムドラッグに変更。
 *
 *   背景:
 *     lvgl_input_read() でタッチ座標を X/Y 変換して LVGL に渡しているため、
 *     LVGL8 デフォルトのスライダードラッグは変換後の座標でドラッグ量を計算する。
 *     ディスプレイが 90° 回転しているため、ユーザーが横に指を動かしても
 *     LVGL は縦移動として受け取り、スライダーが正しく追従しない。
 *
 *   解決策:
 *     LV_EVENT_PRESSING で lv_indev_get_act() / lv_indev_get_point() を使い、
 *     LVGL 変換後の論理座標から delta_x を手動計算してスライダー値を更新する。
 *     縦スワイプを検出した場合はドラッグをキャンセルし、画面遷移を優先する。
 *
 *   縦スワイプ抑制:
 *     slider_dragging フラグで display_settings_is_interacting() をオーバーライド。
 *     カスタムドラッグ中に縦スワイプを検出した場合は即座に false に戻し、
 *     touch_handler 側でスワイプイベントを通過させる。
 */

#include "brightness_screen.h"
#include "../display_settings.h"

#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

extern void set_screen_brightness(uint8_t value, bool ambient);

/* ------------------------------------------------------------------ */
/* スライダードラッグ中フラグ（touch_handler の weak 関数をオーバーライド） */
/* ------------------------------------------------------------------ */
static volatile bool slider_dragging = false;

bool display_settings_is_interacting(void)
{
    return slider_dragging;
}

/* ------------------------------------------------------------------ */
/* カスタムスライダードラッグ状態                                      */
/* ------------------------------------------------------------------ */

/* 縦スワイプとみなす最小 Y 移動量（論理座標ピクセル）                */
#define SLIDER_SWIPE_THRESHOLD 30

static struct {
    bool     active;
    int32_t  start_x;
    int32_t  start_y;
    int32_t  start_value;
    int32_t  current_value;
    int32_t  min_val;
    int32_t  max_val;
    int32_t  slider_width;
    bool     drag_cancelled;
} s_drag = {0};

/* ------------------------------------------------------------------ */
/* カスタムスライダードラッグハンドラ                                  */
/*                                                                     */
/* LVGL8 では lv_indev_get_act() で現在アクティブな indev を取得し、  */
/* lv_indev_get_point() で論理座標を読む。                             */
/*                                                                     */
/* LV_EVENT_PRESSED  : ドラッグ開始状態を記録                         */
/* LV_EVENT_PRESSING : 横ドラッグ → 値更新 / 縦スワイプ → キャンセル  */
/* LV_EVENT_RELEASED : 最終値を確定して NVS に保存                    */
/* ------------------------------------------------------------------ */
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
        /* ドラッグ開始 — 初期状態を記録 */
        s_drag.active        = true;
        s_drag.start_x       = point.x;
        s_drag.start_y       = point.y;
        s_drag.start_value   = lv_slider_get_value(slider);
        s_drag.current_value = s_drag.start_value;
        s_drag.min_val       = lv_slider_get_min_value(slider);
        s_drag.max_val       = lv_slider_get_max_value(slider);
        s_drag.slider_width  = lv_obj_get_width(slider);
        s_drag.drag_cancelled = false;
        slider_dragging      = true;  /* スワイプをブロック */

        LOG_DBG("Slider drag start: x=%d, y=%d, value=%d",
                (int)point.x, (int)point.y, (int)s_drag.start_value);

    } else if (code == LV_EVENT_PRESSING) {
        if (!s_drag.active || s_drag.drag_cancelled) return;

        int32_t delta_x = point.x - s_drag.start_x;
        int32_t delta_y = point.y - s_drag.start_y;
        int32_t abs_dx  = (delta_x < 0) ? -delta_x : delta_x;
        int32_t abs_dy  = (delta_y < 0) ? -delta_y : delta_y;

        /* 縦スワイプ検出: Y 移動が閾値超 かつ X 移動の 2 倍以上 */
        if (abs_dy > SLIDER_SWIPE_THRESHOLD && abs_dy > abs_dx * 2) {
            /* ドラッグキャンセル — 元の値に戻し、スワイプを通過させる */
            LOG_INF("Vertical swipe on slider - cancelling drag");
            lv_slider_set_value(slider, s_drag.start_value, LV_ANIM_OFF);
            s_drag.current_value  = s_drag.start_value;
            s_drag.drag_cancelled = true;
            slider_dragging       = false;  /* スワイプを通過させる */
            return;
        }

        /* 横ドラッグ — 手動でスライダー値を計算 */
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

        LOG_DBG("Slider dragging: delta_x=%d, value=%d", (int)delta_x, (int)new_value);

    } else if (code == LV_EVENT_RELEASED) {
        if (!s_drag.active) return;

        /*
         * CRITICAL: LVGL デフォルトハンドラが座標変換の影響で誤った値を
         * 書き込む場合があるため、カスタム計算値で上書きする。
         */
        lv_slider_set_value(slider, s_drag.current_value, LV_ANIM_OFF);

        bool was_cancelled   = s_drag.drag_cancelled;
        s_drag.active        = false;
        s_drag.drag_cancelled = false;
        slider_dragging      = false;

        if (!was_cancelled) {
            /* NVS に保存 */
            display_settings_set_manual_brightness((uint8_t)s_drag.current_value);
            display_settings_save_if_dirty();
            LOG_INF("Slider drag end: final_value=%d (saved)", (int)s_drag.current_value);
        } else {
            LOG_DBG("Slider drag cancelled (swipe)");
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
/* parent = lv_obj_create(NULL) のスクリーン。コンテナなし。          */
/* ================================================================== */

int zmk_widget_brightness_screen_init(struct zmk_widget_brightness_screen *widget,
                                      lv_obj_t *parent)
{
    if (!parent) return -EINVAL;

    display_settings_init();

    /*
     * widget->obj = parent そのもの。
     * 中間コンテナを作らない。
     */
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
     * [修正] カスタムドラッグハンドラを PRESSED / PRESSING / RELEASED に登録。
     * user_data に value_label を渡し、PRESSING 内でリアルタイム更新する。
     *
     * 旧実装（LVGL デフォルトドラッグ）:
     *   lv_obj_add_event_cb(slider, slider_pressed_cb,       LV_EVENT_PRESSED,       NULL);
     *   lv_obj_add_event_cb(slider, slider_released_cb,      LV_EVENT_RELEASED,      NULL);
     *   lv_obj_add_event_cb(slider, slider_value_changed_cb, LV_EVENT_VALUE_CHANGED, value_label);
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
    /* スクリーン切り替えは lv_scr_load() で行うため、ここでは値の同期のみ */
    if (!widget || !widget->slider) return;
    uint8_t val = display_settings_get_manual_brightness();
    lv_slider_set_value(widget->slider, val, LV_ANIM_OFF);
    if (widget->value_label) {
        lv_label_set_text_fmt(widget->value_label, "%d%%", val);
    }
    /* ドラッグ状態を必ずリセット */
    slider_dragging       = false;
    s_drag.active         = false;
    s_drag.drag_cancelled = false;
}

void zmk_widget_brightness_screen_hide(struct zmk_widget_brightness_screen *widget)
{
    /* ドラッグ状態を必ずリセット */
    slider_dragging       = false;
    s_drag.active         = false;
    s_drag.drag_cancelled = false;
}

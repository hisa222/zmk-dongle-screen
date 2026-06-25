/*
 * Copyright (c) 2024 The ZMK Contributors
 * SPDX-License-Identifier: MIT
 *
 * Brightness Screen Widget
 *
 * Prospector の操作感を完全に踏襲:
 * - スライダー操作中は ui_interaction_active=true
 * - 縦スワイプ検知時は即 ui_interaction_active=false → スワイプを通過させる
 * - 値変更はリアルタイム反映
 * - NVS 保存は RELEASED 時のみ
 *
 * display_settings_is_interacting() は引数なし版(prospector互換)に統一。
 * touch_handler.c の weak 宣言も引数なし版に合わせること。
 */

#include "brightness_screen.h"
#include "../display_settings.h"
#include "../touch_handler.h"

#include <zephyr/logging/log.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

extern void set_screen_brightness(uint8_t value, bool ambient);

/* ------------------------------------------------------------------ */
/* brightness screen active flag                                      */
/* ------------------------------------------------------------------ */

static bool brightness_screen_active = false;

/*
 * touch_handler.c から呼ばれる weak override (引数なし版).
 *
 * brightness screen が表示中かどうかだけを返す。
 * 座標判定は廃止し、prospector と同様にシンプルな active フラグのみで管理する。
 * スライダー操作中の誤スワイプ防止は ui_interaction_active で行う。
 */
bool display_settings_is_interacting(void)
{
    return brightness_screen_active && ui_interaction_active;
}

/* ------------------------------------------------------------------ */
/* custom drag state (prospector の slider_drag_state と同等)         */
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
 * スライダーのカスタムドラッグコールバック
 *
 * Prospector の ds_custom_slider_drag_cb と同じロジック:
 * - PRESSED    : 初期状態を記録、ui_interaction_active=true
 * - PRESSING   : delta_x でスライダー値を計算、縦スワイプなら即キャンセル
 *                (キャンセル時は ui_interaction_active=false でスワイプを通過)
 * - RELEASED   : 最終値をセット、NVS 保存、ui_interaction_active=false
 * - PRESS_LOST : RELEASED と同じクリーンアップ
 */
static void slider_custom_drag_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *slider = lv_event_get_target(e);

    lv_indev_t *indev = lv_indev_get_act();  /* LVGL8 API */
    if (!indev) {
        return;
    }

    lv_point_t point;
    lv_indev_get_point(indev, &point);

    if (code == LV_EVENT_PRESSED) {
        ui_interaction_active = true;

        s_drag.active         = true;
        s_drag.start_x        = point.x;
        s_drag.start_y        = point.y;
        s_drag.start_value    = lv_slider_get_value(slider);
        s_drag.current_value  = s_drag.start_value;
        s_drag.min_val        = lv_slider_get_min_value(slider);
        s_drag.max_val        = lv_slider_get_max_value(slider);
        s_drag.slider_width   = lv_obj_get_width(slider);
        s_drag.drag_cancelled = false;
        return;
    }

    if (code == LV_EVENT_PRESSING) {
        if (!s_drag.active || s_drag.drag_cancelled) {
            return;
        }

        int32_t delta_x  = point.x - s_drag.start_x;
        int32_t delta_y  = point.y - s_drag.start_y;
        int32_t abs_dx   = (delta_x < 0) ? -delta_x : delta_x;
        int32_t abs_dy   = (delta_y < 0) ? -delta_y : delta_y;

        /*
         * 縦スワイプ検知: Y 移動量が閾値を超えかつ X の 2 倍以上
         * → スライダー変更をキャンセルし、ui_interaction_active=false にして
         *   スワイプイベントが通過できるようにする (prospector と同じ)
         */
        if (abs_dy > SLIDER_SWIPE_THRESHOLD && abs_dy > abs_dx * 2) {
            lv_slider_set_value(slider, s_drag.start_value, LV_ANIM_OFF);
            s_drag.current_value  = s_drag.start_value;
            s_drag.drag_cancelled = true;
            ui_interaction_active = false;  /* スワイプを通過させる */
            return;
        }

        if (s_drag.slider_width == 0) {
            return;
        }

        /* 水平ドラッグ: delta_x を直接マッピング (prospector と同じ) */
        int32_t value_range = s_drag.max_val - s_drag.min_val;
        int32_t value_delta = (delta_x * value_range) / s_drag.slider_width;
        int32_t new_value   = s_drag.start_value + value_delta;

        if (new_value < s_drag.min_val) new_value = s_drag.min_val;
        if (new_value > s_drag.max_val) new_value = s_drag.max_val;

        s_drag.current_value = new_value;
        lv_slider_set_value(slider, new_value, LV_ANIM_OFF);

        /* リアルタイムラベル更新 */
        lv_obj_t *value_label = lv_event_get_user_data(e);
        if (value_label) {
            lv_label_set_text_fmt(value_label, "%d%%", (int)new_value);
        }

        /* リアルタイム輝度反映 */
        set_screen_brightness((uint8_t)new_value, false);
        return;
    }

    if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        if (s_drag.active) {
            /* LVGL の default handler が上書きする前に正しい値を再セット */
            lv_slider_set_value(slider, s_drag.current_value, LV_ANIM_OFF);

            bool was_cancelled = s_drag.drag_cancelled;
            s_drag.active         = false;
            s_drag.drag_cancelled = false;
            ui_interaction_active = false;

            if (!was_cancelled) {
                /* NVS 保存 */
                display_settings_set_manual_brightness((uint8_t)s_drag.current_value);
                display_settings_save_if_dirty();

                /* 正しい値で VALUE_CHANGED を再発火 (ラベル最終更新) */
                lv_obj_t *value_label = lv_event_get_user_data(e);
                if (value_label) {
                    lv_label_set_text_fmt(value_label, "%d%%", (int)s_drag.current_value);
                }
            }
        }
        return;
    }
}

/* ------------------------------------------------------------------ */
/* style                                                              */
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

    widget->title_label = lv_label_create(parent);
    lv_label_set_text(widget->title_label, "Brightness");
    lv_obj_set_style_text_color(widget->title_label,
                                lv_color_hex(0xFFFFFF), LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(widget->title_label,
                               &lv_font_montserrat_20, LV_STATE_DEFAULT);
    lv_obj_align(widget->title_label, LV_ALIGN_TOP_MID, 0, 14);

    widget->value_label = lv_label_create(parent);
    lv_obj_set_style_text_color(widget->value_label,
                                lv_color_hex(0x007AFF), LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(widget->value_label,
                               &lv_font_montserrat_40, LV_STATE_DEFAULT);
    lv_label_set_text_fmt(widget->value_label, "%d%%",
                          display_settings_get_manual_brightness());
    lv_obj_align(widget->value_label, LV_ALIGN_CENTER, 0, -20);

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

    lv_obj_add_event_cb(widget->slider, slider_custom_drag_cb,
                        LV_EVENT_PRESSED,    widget->value_label);
    lv_obj_add_event_cb(widget->slider, slider_custom_drag_cb,
                        LV_EVENT_PRESSING,   widget->value_label);
    lv_obj_add_event_cb(widget->slider, slider_custom_drag_cb,
                        LV_EVENT_RELEASED,   widget->value_label);
    lv_obj_add_event_cb(widget->slider, slider_custom_drag_cb,
                        LV_EVENT_PRESS_LOST, widget->value_label);

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
    if (!widget || !widget->slider) {
        return;
    }

    brightness_screen_active = true;
    ui_interaction_active    = false;

    uint8_t val = display_settings_get_manual_brightness();
    lv_slider_set_value(widget->slider, val, LV_ANIM_OFF);
    if (widget->value_label) {
        lv_label_set_text_fmt(widget->value_label, "%d%%", val);
    }

    s_drag.active         = false;
    s_drag.drag_cancelled = false;
}

void zmk_widget_brightness_screen_hide(struct zmk_widget_brightness_screen *widget)
{
    brightness_screen_active = false;
    ui_interaction_active    = false;

    s_drag.active         = false;
    s_drag.drag_cancelled = false;

    if (!widget) {
        return;
    }

    /*
     * 親 screen は custom_status_screen.c 側で lv_obj_clean() されるので、
     * ここでは lv_obj_del() はしない。
     * 削除済みオブジェクトを次回 show/init が触らないようポインタを NULL 化。
     */
    widget->title_label = NULL;
    widget->value_label = NULL;
    widget->slider      = NULL;
    widget->icon_low    = NULL;
    widget->icon_high   = NULL;
    widget->nav_hint    = NULL;
    widget->obj         = NULL;
}

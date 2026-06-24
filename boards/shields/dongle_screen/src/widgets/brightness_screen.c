/*
 * Copyright (c) 2024 The ZMK Contributors
 * SPDX-License-Identifier: MIT
 *
 * Brightness Screen Widget
 *
 * Prospector の操作感に寄せるため、
 * - スライダー操作中は ui_interaction_active=true
 * - スワイプ遷移は抑止
 * - 値変更はリアルタイム反映
 * - NVS 保存は RELEASED 時のみ
 *
 * さらに touch_handler.c から brightness screen 上のスライダー領域を
 * 生タッチ座標で判定できるよう display_settings_is_interacting() を提供する。
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
 * touch_handler.c から呼ばれる weak override.
 * raw touch 座標ベースで brightness slider 領域かどうかを判定する。
 *
 * 論理 280x240 / ROTATED_270 前提:
 * slider: LV_ALIGN_CENTER, 0, 50
 * size  : 200 x 8
 * ext_click_area: 24
 *
 * 論理Y 146〜202 付近が raw_x 37〜93 に対応するため、
 * raw_x のみで広めに判定する。
 */
bool display_settings_is_interacting(uint16_t raw_x, uint16_t raw_y)
{
    ARG_UNUSED(raw_y);

    if (!brightness_screen_active) {
        return false;
    }

    if (raw_x >= 37 && raw_x <= 93) {
        return true;
    }

    return false;
}

/* ------------------------------------------------------------------ */
/* custom drag state                                                  */
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

static void slider_custom_drag_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *slider = lv_event_get_target(e);

    lv_indev_t *indev = lv_indev_get_act();
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

        int32_t delta_x = point.x - s_drag.start_x;
        int32_t delta_y = point.y - s_drag.start_y;
        int32_t abs_dx = (delta_x < 0) ? -delta_x : delta_x;
        int32_t abs_dy = (delta_y < 0) ? -delta_y : delta_y;

        /*
         * 大きく縦に振った場合はスライダー変更をキャンセル。
         * ただし ui_interaction_active 自体は RELEASED まで true のままなので、
         * この touch 中に画面遷移が走ることはない。
         */
        if (abs_dy > SLIDER_SWIPE_THRESHOLD && abs_dy > abs_dx * 2) {
            lv_slider_set_value(slider, s_drag.start_value, LV_ANIM_OFF);
            s_drag.current_value = s_drag.start_value;
            s_drag.drag_cancelled = true;
            return;
        }

        if (s_drag.slider_width == 0) {
            return;
        }

        int32_t value_range = s_drag.max_val - s_drag.min_val;
        int32_t value_delta = (delta_x * value_range) / s_drag.slider_width;
        int32_t new_value = s_drag.start_value + value_delta;

        if (new_value < s_drag.min_val) new_value = s_drag.min_val;
        if (new_value > s_drag.max_val) new_value = s_drag.max_val;

        s_drag.current_value = new_value;
        lv_slider_set_value(slider, new_value, LV_ANIM_OFF);

        lv_obj_t *value_label = lv_event_get_user_data(e);
        if (value_label) {
            lv_label_set_text_fmt(value_label, "%d%%", (int)new_value);
        }

        set_screen_brightness((uint8_t)new_value, false);
        return;
    }

    if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        if (s_drag.active) {
            lv_slider_set_value(slider, s_drag.current_value, LV_ANIM_OFF);

            if (!s_drag.drag_cancelled) {
                display_settings_set_manual_brightness((uint8_t)s_drag.current_value);
                display_settings_save_if_dirty();
            }
        }

        s_drag.active = false;
        s_drag.drag_cancelled = false;
        ui_interaction_active = false;
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
                        LV_EVENT_PRESSED, widget->value_label);
    lv_obj_add_event_cb(widget->slider, slider_custom_drag_cb,
                        LV_EVENT_PRESSING, widget->value_label);
    lv_obj_add_event_cb(widget->slider, slider_custom_drag_cb,
                        LV_EVENT_RELEASED, widget->value_label);
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
    ui_interaction_active = false;

    uint8_t val = display_settings_get_manual_brightness();
    lv_slider_set_value(widget->slider, val, LV_ANIM_OFF);
    if (widget->value_label) {
        lv_label_set_text_fmt(widget->value_label, "%d%%", val);
    }

    s_drag.active = false;
    s_drag.drag_cancelled = false;
}

void zmk_widget_brightness_screen_hide(struct zmk_widget_brightness_screen *widget)
{
    brightness_screen_active = false;
    ui_interaction_active = false;

    s_drag.active = false;
    s_drag.drag_cancelled = false;

    if (!widget) {
        return;
    }

    /*
     * 親 screen は custom_status_screen.c 側で lv_obj_clean() されるので、
     * ここでは lv_obj_del() はしない。
     * ただし、削除済みオブジェクトを次回 show/init が触らないように
     * ポインタは必ず NULL に戻す。
     */
    widget->title_label = NULL;
    widget->value_label = NULL;
    widget->slider = NULL;
    widget->icon_low = NULL;
    widget->icon_high = NULL;
    widget->nav_hint = NULL;
    widget->obj = NULL;
}

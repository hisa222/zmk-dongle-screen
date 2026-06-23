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
 *   LVGL8 ネイティブのスライダードラッグを使用。
 *   LV_EVENT_VALUE_CHANGED のみ受け取る。
 *   縦スワイプ抑制は display_settings_is_interacting() weak オーバーライドで対処。
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
/* イベントハンドラ                                                    */
/* ------------------------------------------------------------------ */

static void slider_pressed_cb(lv_event_t *e)
{
    slider_dragging = true;
}

static void slider_released_cb(lv_event_t *e)
{
    slider_dragging = false;
}

static void slider_value_changed_cb(lv_event_t *e)
{
    lv_obj_t *slider = lv_event_get_target(e);
    int32_t value    = lv_slider_get_value(slider);

    /* value_label はユーザーデータで渡す */
    lv_obj_t *value_label = lv_event_get_user_data(e);
    if (value_label) {
        lv_label_set_text_fmt(value_label, "%d%%", (int)value);
    }

    set_screen_brightness((uint8_t)value, false);
    display_settings_set_manual_brightness((uint8_t)value);
    display_settings_save_if_dirty();
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

    lv_obj_add_event_cb(widget->slider, slider_pressed_cb,
                        LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(widget->slider, slider_released_cb,
                        LV_EVENT_RELEASED, NULL);
    /* user_data に value_label を渡して VALUE_CHANGED 内で更新 */
    lv_obj_add_event_cb(widget->slider, slider_value_changed_cb,
                        LV_EVENT_VALUE_CHANGED, widget->value_label);

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
    slider_dragging = false;
}

void zmk_widget_brightness_screen_hide(struct zmk_widget_brightness_screen *widget)
{
    slider_dragging = false;
}

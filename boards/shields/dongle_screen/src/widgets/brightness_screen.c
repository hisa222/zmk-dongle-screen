/*
 * Copyright (c) 2024 The ZMK Contributors
 * SPDX-License-Identifier: MIT
 *
 * Brightness screen
 *
 * 方針:
 * - スライダ操作中は ui_interaction_active = true
 * - VALUE_CHANGED で即時反映
 * - RELEASED / PRESS_LOST で解除
 * - slider のノブだけでなくトラック上タップでも値変更できるようにする
 */

#include "brightness_screen.h"
#include "../custom_status_screen.h"
#include "../display_settings.h"
#include "../touch_handler.h"

#include <zephyr/logging/log.h>
#include <errno.h>
#include <stdio.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

/* ================================================================== */
/* Helpers                                                            */
/* ================================================================== */

static void update_value_label(struct zmk_widget_brightness_screen *widget, uint8_t value)
{
    if (!widget || !widget->value_label) {
        return;
    }

    char text[8];
    snprintk(text, sizeof(text), "%u%%", value);
    lv_label_set_text(widget->value_label, text);
}

static void slider_press_start(struct zmk_widget_brightness_screen *widget)
{
    widget->dragging = true;
    ui_interaction_active = true;
}

static void slider_press_end(struct zmk_widget_brightness_screen *widget)
{
    widget->dragging = false;
    ui_interaction_active = false;
}

/* ================================================================== */
/* Slider event callback                                              */
/* ================================================================== */

static void brightness_slider_event_cb(lv_event_t *e)
{
    struct zmk_widget_brightness_screen *widget =
        (struct zmk_widget_brightness_screen *)lv_event_get_user_data(e);

    if (!widget) {
        return;
    }

    lv_event_code_t code = lv_event_get_code(e);

    switch (code) {
    case LV_EVENT_PRESSED:
        slider_press_start(widget);
        break;

    case LV_EVENT_PRESSING:
        if (!widget->dragging) {
            slider_press_start(widget);
        }
        break;

    case LV_EVENT_VALUE_CHANGED: {
        int value = lv_slider_get_value(widget->slider);
        if (value < 0) {
            value = 0;
        } else if (value > 100) {
            value = 100;
        }

        widget->current_value = (uint8_t)value;
        update_value_label(widget, widget->current_value);
        (void)display_settings_set_brightness(widget->current_value);
        break;
    }

    case LV_EVENT_RELEASED:
    case LV_EVENT_PRESS_LOST:
        slider_press_end(widget);
        (void)display_settings_set_brightness(widget->current_value);
        break;

    default:
        break;
    }
}

/* ================================================================== */
/* Widget init                                                        */
/* ================================================================== */

int zmk_widget_brightness_screen_init(struct zmk_widget_brightness_screen *widget,
                                      lv_obj_t *parent)
{
    if (!widget || !parent) {
        return -EINVAL;
    }

    display_settings_init();

    widget->current_value = display_settings_get_brightness();
    widget->dragging = false;

    widget->obj = lv_obj_create(parent);
    if (!widget->obj) {
        return -ENOMEM;
    }

    lv_obj_set_size(widget->obj, LV_PCT(100), LV_PCT(100));
    lv_obj_center(widget->obj);
    lv_obj_clear_flag(widget->obj, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_set_style_bg_opa(widget->obj, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(widget->obj, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(widget->obj, 0, LV_PART_MAIN);

    widget->title_label = lv_label_create(widget->obj);
    if (!widget->title_label) {
        return -ENOMEM;
    }

    lv_label_set_text(widget->title_label, "Brightness");
    lv_obj_set_style_text_color(widget->title_label,
                                lv_color_hex(0xFFFFFF), LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(widget->title_label,
                               &lv_font_montserrat_20, LV_STATE_DEFAULT);
    lv_obj_align(widget->title_label, LV_ALIGN_TOP_MID, 0, 14);

    widget->value_label = lv_label_create(widget->obj);
    if (!widget->value_label) {
        return -ENOMEM;
    }

    lv_obj_set_style_text_color(widget->value_label,
                                lv_color_hex(0xFFFFFF), LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(widget->value_label,
                               &lv_font_montserrat_28, LV_STATE_DEFAULT);
    lv_obj_align(widget->value_label, LV_ALIGN_CENTER, 0, -28);
    update_value_label(widget, widget->current_value);

    widget->slider = lv_slider_create(widget->obj);
    if (!widget->slider) {
        return -ENOMEM;
    }

    /*
     * 少し太く・広くして操作しやすくする
     */
    lv_obj_set_size(widget->slider, 240, 32);
    lv_obj_align(widget->slider, LV_ALIGN_CENTER, 0, 28);
    lv_slider_set_range(widget->slider, 0, 100);
    lv_slider_set_value(widget->slider, widget->current_value, LV_ANIM_OFF);

    /*
     * トラック
     */
    lv_obj_set_style_bg_color(widget->slider,
                              lv_color_hex(0x303030), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(widget->slider,
                            LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(widget->slider, 16, LV_PART_MAIN);
    lv_obj_set_style_border_width(widget->slider, 0, LV_PART_MAIN);

    /*
     * インジケータ
     */
    lv_obj_set_style_bg_color(widget->slider,
                              lv_color_hex(0x4A90E2), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(widget->slider,
                            LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(widget->slider, 16, LV_PART_INDICATOR);

    /*
     * ノブを大きくする
     */
    lv_obj_set_style_bg_color(widget->slider,
                              lv_color_hex(0xFFFFFF), LV_PART_KNOB);
    lv_obj_set_style_bg_opa(widget->slider,
                            LV_OPA_COVER, LV_PART_KNOB);
    lv_obj_set_style_radius(widget->slider, LV_RADIUS_CIRCLE, LV_PART_KNOB);
    lv_obj_set_style_pad_all(widget->slider, 10, LV_PART_KNOB);

    /*
     * ここが重要:
     * LVGL 側で slider 全体がタップ・ドラッグを受け取れるよう clickable を明示。
     */
    lv_obj_add_flag(widget->slider, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(widget->slider, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_add_event_cb(widget->slider,
                        brightness_slider_event_cb,
                        LV_EVENT_ALL,
                        widget);

    widget->hint_label = lv_label_create(widget->obj);
    if (!widget->hint_label) {
        return -ENOMEM;
    }

    lv_label_set_text(widget->hint_label, "< swipe >");
    lv_obj_set_style_text_color(widget->hint_label,
                                lv_color_hex(0x444444), LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(widget->hint_label,
                               &lv_font_montserrat_18, LV_STATE_DEFAULT);
    lv_obj_align(widget->hint_label, LV_ALIGN_BOTTOM_MID, 0, -10);

    return 0;
}

void zmk_widget_brightness_screen_show(struct zmk_widget_brightness_screen *widget)
{
    if (!widget) {
        return;
    }

    display_settings_init();

    widget->current_value = display_settings_get_brightness();
    widget->dragging = false;
    ui_interaction_active = false;

    if (widget->slider) {
        lv_slider_set_value(widget->slider, widget->current_value, LV_ANIM_OFF);
    }

    update_value_label(widget, widget->current_value);
}

void zmk_widget_brightness_screen_hide(struct zmk_widget_brightness_screen *widget)
{
    if (!widget) {
        return;
    }

    widget->dragging = false;
    ui_interaction_active = false;

    widget->title_label = NULL;
    widget->value_label = NULL;
    widget->slider = NULL;
    widget->hint_label = NULL;
    widget->obj = NULL;
}

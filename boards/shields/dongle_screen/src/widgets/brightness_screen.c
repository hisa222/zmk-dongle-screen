/*
 * Copyright (c) 2024 The ZMK Contributors
 * SPDX-License-Identifier: MIT
 *
 * Brightness Screen Widget
 *
 * prospector-zmk-module の ds_custom_slider_drag_cb と同じアプローチで
 * タッチパネルの座標変換に対応したスライダードラッグを実装する。
 *
 * 座標系について (dongle-screen / Waveshare 1.69" Round LCD):
 *   タッチパネル物理: 240×280 (portrait)
 *   ディスプレイ論理: 280×240 (landscape, ST7789V で90°回転)
 *   LVGL indev read: touch_Y → logical_X (direct)
 *                    touch_X → logical_Y (invert: 239 - touch_X)
 *
 * touch_handler.c の lvgl_input_read() は上記変換済みの座標を
 * LVGL に渡しているため、スライダードラッグでの X 方向は
 * prospectorと同様に「ドラッグ方向が反転」しない。
 * ただし念のため Pressing イベントで縦スワイプを検知して
 * スワイプ優先にする処理は入れておく。
 */

#include "brightness_screen.h"
#include "../display_settings.h"

#include <zephyr/logging/log.h>
#include <string.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

/* ------------------------------------------------------------------ */
/* set_screen_brightness (brightness.c で定義、non-static)            */
/* ------------------------------------------------------------------ */
extern void set_screen_brightness(uint8_t value, bool ambient);

/* ------------------------------------------------------------------ */
/* スライダードラッグ状態                                              */
/* ------------------------------------------------------------------ */
#define SLIDER_SWIPE_THRESHOLD 30  /* 縦スワイプとみなす最低ピクセル */

static struct {
    lv_obj_t  *active_slider;
    int32_t    start_x;
    int32_t    start_y;
    int32_t    start_value;
    int32_t    current_value;
    int32_t    min_val;
    int32_t    max_val;
    int32_t    slider_width;
    bool       drag_cancelled;
} drag_state;

/* touch_handler.c の weak 関数をオーバーライド */
static bool ui_interaction_active = false;

bool brightness_screen_is_interacting(void)
{
    return ui_interaction_active;
}

/* display_settings_is_interacting() を brightness_screen_is_interacting()
 * に委譲するオーバーライド実装 */
bool display_settings_is_interacting(void)
{
    return brightness_screen_is_interacting();
}

/* ------------------------------------------------------------------ */
/* スライダーカスタムドラッグコールバック                              */
/* ------------------------------------------------------------------ */
static lv_obj_t *gs_value_label = NULL;   /* ドラッグ中のラベル更新用 */

static void slider_drag_cb(lv_event_t *e)
{
    lv_event_code_t code   = lv_event_get_code(e);
    lv_obj_t       *slider = lv_event_get_target(e);
    lv_indev_t      *indev = lv_indev_get_act();
    if (!indev) return;

    lv_point_t point;
    lv_indev_get_point(indev, &point);

    if (code == LV_EVENT_PRESSED) {
        drag_state.active_slider  = slider;
        drag_state.start_x        = point.x;
        drag_state.start_y        = point.y;
        drag_state.start_value    = lv_slider_get_value(slider);
        drag_state.current_value  = drag_state.start_value;
        drag_state.min_val        = lv_slider_get_min_value(slider);
        drag_state.max_val        = lv_slider_get_max_value(slider);
        drag_state.slider_width   = lv_obj_get_width(slider);
        drag_state.drag_cancelled = false;
        ui_interaction_active     = true;
        LOG_DBG("Slider drag start: x=%d y=%d val=%d",
                (int)point.x, (int)point.y, (int)drag_state.start_value);

    } else if (code == LV_EVENT_PRESSING) {
        if (drag_state.active_slider != slider) return;
        if (drag_state.drag_cancelled) return;

        int32_t delta_x     = point.x - drag_state.start_x;
        int32_t delta_y     = point.y - drag_state.start_y;
        int32_t abs_delta_x = (delta_x < 0) ? -delta_x : delta_x;
        int32_t abs_delta_y = (delta_y < 0) ? -delta_y : delta_y;

        /* 縦スワイプ検知 → ドラッグキャンセルしてスワイプを通す */
        if (abs_delta_y > SLIDER_SWIPE_THRESHOLD &&
            abs_delta_y > abs_delta_x * 2) {
            LOG_INF("Vertical swipe on slider - cancelling drag");
            lv_slider_set_value(slider, drag_state.start_value, LV_ANIM_OFF);
            drag_state.current_value  = drag_state.start_value;
            drag_state.drag_cancelled = true;
            ui_interaction_active     = false;
            return;
        }

        /* 水平ドラッグ → 値を計算して適用 */
        int32_t value_range = drag_state.max_val - drag_state.min_val;
        int32_t value_delta = (delta_x * value_range) / drag_state.slider_width;
        int32_t new_value   = drag_state.start_value + value_delta;

        if (new_value < drag_state.min_val) new_value = drag_state.min_val;
        if (new_value > drag_state.max_val) new_value = drag_state.max_val;

        drag_state.current_value = new_value;
        lv_slider_set_value(slider, new_value, LV_ANIM_OFF);

        /* ラベルとバックライトをリアルタイム更新 */
        if (gs_value_label) {
            lv_label_set_text_fmt(gs_value_label, "%d%%", (int)new_value);
        }
        set_screen_brightness((uint8_t)new_value, false);

    } else if (code == LV_EVENT_RELEASED) {
        if (drag_state.active_slider == slider) {
            lv_slider_set_value(slider, drag_state.current_value, LV_ANIM_OFF);

            bool was_cancelled        = drag_state.drag_cancelled;
            drag_state.active_slider  = NULL;
            drag_state.drag_cancelled = false;
            ui_interaction_active     = false;

            if (!was_cancelled) {
                lv_event_send(slider, LV_EVENT_VALUE_CHANGED, NULL);
                LOG_INF("Slider drag end: value=%d", (int)drag_state.current_value);
            } else {
                LOG_DBG("Slider drag cancelled (swipe)");
            }
            return;
        }
        drag_state.active_slider  = NULL;
        drag_state.drag_cancelled = false;
        ui_interaction_active     = false;
    }
}

/* ------------------------------------------------------------------ */
/* VALUE_CHANGED イベント → NVS 保存                                  */
/* ------------------------------------------------------------------ */
static void slider_value_changed_cb(lv_event_t *e)
{
    lv_obj_t *slider = lv_event_get_target(e);

    /* drag_state.active_slider が NULL = ドラッグ終了後の確定イベント */
    if (drag_state.active_slider != NULL) return;

    int32_t value = lv_slider_get_value(slider);
    display_settings_set_manual_brightness((uint8_t)value);
    display_settings_save_if_dirty();

    if (gs_value_label) {
        lv_label_set_text_fmt(gs_value_label, "%d%%", (int)value);
    }
    LOG_INF("Brightness saved: %d", (int)value);
}

/* ------------------------------------------------------------------ */
/* iOS 風スライダースタイル適用                                        */
/* ------------------------------------------------------------------ */
static void apply_ios_slider_style(lv_obj_t *slider)
{
    /* トラック */
    lv_obj_set_style_radius(slider, 3, LV_PART_MAIN);
    lv_obj_set_style_bg_color(slider, lv_color_hex(0x3A3A3C), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_MAIN);
    /* インジケータ */
    lv_obj_set_style_radius(slider, 3, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(slider, lv_color_hex(0x007AFF), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_INDICATOR);
    /* ノブ */
    lv_obj_set_style_radius(slider, LV_RADIUS_CIRCLE, LV_PART_KNOB);
    lv_obj_set_style_bg_color(slider, lv_color_white(), LV_PART_KNOB);
    lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_KNOB);
    lv_obj_set_style_pad_all(slider, 8, LV_PART_KNOB);
    lv_obj_set_style_shadow_width(slider, 4, LV_PART_KNOB);
    lv_obj_set_style_shadow_color(slider, lv_color_black(), LV_PART_KNOB);
    lv_obj_set_style_shadow_opa(slider, LV_OPA_30, LV_PART_KNOB);
}

/* ================================================================== */
/* Widget init                                                         */
/* ================================================================== */

int zmk_widget_brightness_screen_init(struct zmk_widget_brightness_screen *widget,
                                      lv_obj_t *parent)
{
    LOG_INF("brightness_screen: init");

    if (!parent) return -EINVAL;

    display_settings_init();

    /* ---- フルスクリーンコンテナ ---- */
    widget->obj = lv_obj_create(parent);
    if (!widget->obj) return -ENOMEM;

    lv_obj_set_size(widget->obj, LV_HOR_RES, LV_VER_RES);
    lv_obj_set_pos(widget->obj, 0, 0);
    lv_obj_clear_flag(widget->obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(widget->obj, lv_color_hex(0x0A0A0A), LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(widget->obj, LV_OPA_COVER, LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(widget->obj, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(widget->obj, 0, LV_STATE_DEFAULT);

    /* ---- タイトル ---- */
    widget->title_label = lv_label_create(widget->obj);
    lv_label_set_text(widget->title_label, "Brightness");
    lv_obj_set_style_text_color(widget->title_label,
                                lv_color_hex(0xFFFFFF), LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(widget->title_label,
                               &lv_font_montserrat_20, LV_STATE_DEFAULT);
    lv_obj_align(widget->title_label, LV_ALIGN_TOP_MID, 0, 14);

    /* ---- 輝度値ラベル "80%" ---- */
    widget->value_label = lv_label_create(widget->obj);
    lv_obj_set_style_text_color(widget->value_label,
                                lv_color_hex(0x007AFF), LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(widget->value_label,
                               &lv_font_montserrat_40, LV_STATE_DEFAULT);
    lv_label_set_text_fmt(widget->value_label, "%d%%",
                          display_settings_get_manual_brightness());
    lv_obj_align(widget->value_label, LV_ALIGN_CENTER, 0, -20);

    gs_value_label = widget->value_label;

    /* ---- スライダー ---- */
    /*
     * 画面幅 280px。スライダーは左右に16pxマージンで 248px。
     * アイコンを両端に置くため少し短くして 200px、中央に配置。
     */
    widget->slider = lv_slider_create(widget->obj);
    lv_obj_set_size(widget->slider, 200, 6);
    lv_obj_align(widget->slider, LV_ALIGN_CENTER, 0, 50);
    lv_slider_set_range(widget->slider,
                        CONFIG_DONGLE_SCREEN_MIN_BRIGHTNESS,
                        CONFIG_DONGLE_SCREEN_MAX_BRIGHTNESS);
    lv_slider_set_value(widget->slider,
                        display_settings_get_manual_brightness(),
                        LV_ANIM_OFF);
    lv_obj_set_ext_click_area(widget->slider, 20);  /* タッチ領域を拡大 */

    apply_ios_slider_style(widget->slider);

    /* カスタムドラッグハンドラ (3イベント登録) */
    lv_obj_add_event_cb(widget->slider, slider_drag_cb,
                        LV_EVENT_PRESSED,  NULL);
    lv_obj_add_event_cb(widget->slider, slider_drag_cb,
                        LV_EVENT_PRESSING, NULL);
    lv_obj_add_event_cb(widget->slider, slider_drag_cb,
                        LV_EVENT_RELEASED, NULL);
    /* 確定イベント → NVS保存 */
    lv_obj_add_event_cb(widget->slider, slider_value_changed_cb,
                        LV_EVENT_VALUE_CHANGED, NULL);

    /* ---- 低輝度アイコン (スライダー左) ---- */
    widget->icon_low = lv_label_create(widget->obj);
    lv_label_set_text(widget->icon_low, LV_SYMBOL_MINUS);
    lv_obj_set_style_text_color(widget->icon_low,
                                lv_color_hex(0x888888), LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(widget->icon_low,
                               &lv_font_montserrat_20, LV_STATE_DEFAULT);
    lv_obj_align_to(widget->icon_low, widget->slider,
                    LV_ALIGN_OUT_LEFT_MID, -12, 0);

    /* ---- 高輝度アイコン (スライダー右) ---- */
    widget->icon_high = lv_label_create(widget->obj);
    lv_label_set_text(widget->icon_high, LV_SYMBOL_PLUS);
    lv_obj_set_style_text_color(widget->icon_high,
                                lv_color_hex(0xFFFFFF), LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(widget->icon_high,
                               &lv_font_montserrat_20, LV_STATE_DEFAULT);
    lv_obj_align_to(widget->icon_high, widget->slider,
                    LV_ALIGN_OUT_RIGHT_MID, 12, 0);

    /* ---- ナビゲーションヒント ---- */
    widget->nav_hint = lv_label_create(widget->obj);
    lv_label_set_text(widget->nav_hint,
                      LV_SYMBOL_LEFT " swipe " LV_SYMBOL_RIGHT);
    lv_obj_set_style_text_color(widget->nav_hint,
                                lv_color_hex(0x555555), LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(widget->nav_hint,
                               &lv_font_montserrat_20, LV_STATE_DEFAULT);
    lv_obj_align(widget->nav_hint, LV_ALIGN_BOTTOM_MID, 0, -10);

    /* 初期状態は非表示 */
    lv_obj_add_flag(widget->obj, LV_OBJ_FLAG_HIDDEN);

    LOG_INF("brightness_screen: init done");
    return 0;
}

void zmk_widget_brightness_screen_show(struct zmk_widget_brightness_screen *widget)
{
    if (!widget || !widget->obj) return;

    /* 表示のたびに最新値に同期 */
    lv_slider_set_value(widget->slider,
                        display_settings_get_manual_brightness(),
                        LV_ANIM_OFF);
    lv_label_set_text_fmt(widget->value_label, "%d%%",
                          display_settings_get_manual_brightness());
    gs_value_label = widget->value_label;

    lv_obj_move_foreground(widget->obj);
    lv_obj_clear_flag(widget->obj, LV_OBJ_FLAG_HIDDEN);
    LOG_INF("brightness_screen: shown");
}

void zmk_widget_brightness_screen_hide(struct zmk_widget_brightness_screen *widget)
{
    if (!widget || !widget->obj) return;
    lv_obj_add_flag(widget->obj, LV_OBJ_FLAG_HIDDEN);
    gs_value_label = NULL;
    LOG_INF("brightness_screen: hidden");
}

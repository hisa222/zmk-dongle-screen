/*
 * Copyright (c) 2024 The ZMK Contributors
 * SPDX-License-Identifier: MIT
 *
 * Custom Buttons Widget (LVGL8 / ZMK 3.5)
 * system_settings_widget.c と同じ hitbox 方式を流用。
 *
 *    | Btn1 | | Btn2 | | Btn3 |
 *
 *    | Btn4 | | Btn5 | | Btn6 |
 */

#include "custom_buttons2.h"
#include "../custom_status_screen.h"
#include "../touch_handler.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <errno.h>

#include <zmk/behavior.h>
#include <dt-bindings/zmk/keys.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

/* ================================================================== */
/* Tunables                                                            */
/* ================================================================== */

#define ACTION_BTN_W 76
#define ACTION_BTN_H 60
#define ACTION_HIT_W 60
#define ACTION_HIT_H 50

/* 枠線の共通色設定 */
#define BORDER_COLOR_NORMAL  0xFFFFFF  /* 通常時: White */
#define BORDER_COLOR_PRESSED 0x00FF00  /* 押下時: Green */
#define BORDER_WIDTH 2

/* ================================================================== */
/* Keycode送信ヘルパー                                                 */
/* ================================================================== */

static void send_keycode(uint32_t keycode)
{
    struct zmk_behavior_binding binding = {
        .behavior_dev = DEVICE_DT_NAME(DT_NODELABEL(kp)),
        .param1 = keycode,
        .param2 = 0,
    };
    struct zmk_behavior_binding_event event = {
        .layer = 0,
        .position = 0,
        .timestamp = k_uptime_get(),
#if IS_ENABLED(CONFIG_ZMK_SPLIT)
        .source = 0,
#endif
    };

    zmk_behavior_invoke_binding(&binding, event, true);   /* press */
    k_msleep(5);
    zmk_behavior_invoke_binding(&binding, event, false);  /* release */
}

/* ================================================================== */
/* Widget-private state                                                */
/* ================================================================== */

struct action_btn_bundle {
    lv_obj_t *visual_btn;
    lv_obj_t *hitbox;
};

static struct action_btn_bundle custom_button2_1_bundle;
static struct action_btn_bundle custom_button2_2_bundle;
static struct action_btn_bundle custom_button2_3_bundle;
static struct action_btn_bundle custom_button2_4_bundle;
static struct action_btn_bundle custom_button2_5_bundle;
static struct action_btn_bundle custom_button2_6_bundle;

/* ================================================================== */
/* Visual button helper                                                */
/* ================================================================== */

static lv_obj_t *make_visual_btn(lv_obj_t *parent, const char *text,
                                 lv_color_t bg, lv_align_t align,
                                 lv_coord_t x_off, lv_coord_t y_off)
{
    lv_obj_t *obj = lv_obj_create(parent);
    if (!obj) return NULL;

    lv_obj_set_size(obj, ACTION_BTN_W, ACTION_BTN_H);
    lv_obj_align(obj, align, x_off, y_off);

    lv_obj_clear_flag(obj, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_set_style_bg_color(obj, bg, LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_STATE_DEFAULT);
    lv_obj_set_style_radius(obj, 12, LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(obj, 0, LV_STATE_DEFAULT);

    /* 枠線: 通常時 */
    lv_obj_set_style_border_width(obj, BORDER_WIDTH, LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(obj, LV_OPA_COVER, LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(obj, lv_color_hex(BORDER_COLOR_NORMAL), LV_STATE_DEFAULT);

    /* 枠線: 押下時（手動で LV_STATE_PRESSED を付与したときに適用される） */
    lv_obj_set_style_border_color(obj, lv_color_hex(BORDER_COLOR_PRESSED), LV_STATE_PRESSED);

    lv_obj_t *lbl = lv_label_create(obj);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_color(lbl, lv_color_hex(0xFFFFFF), LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_20, LV_STATE_DEFAULT);
    lv_obj_center(lbl);

    return obj;
}

/* ================================================================== */
/* Interaction state callbacks                                         */
/* ================================================================== */

/*
 * hitbox は各 visual_btn の子オブジェクトとして生成されているため、
 * lv_obj_get_parent() で対応する visual_btn を取得できる。
 * その visual_btn にだけ LV_STATE_PRESSED を付け外しすることで、
 * 押下したボタンの枠線のみ色が変わるようにする。
 */
static void ui_press_start_cb(lv_event_t *e)
{
    lv_obj_t *hitbox = lv_event_get_target(e);
    lv_obj_t *visual_btn = lv_obj_get_parent(hitbox);
    if (visual_btn) {
        lv_obj_add_state(visual_btn, LV_STATE_PRESSED);
    }
    ui_interaction_active = true;
}

static void ui_press_end_cb(lv_event_t *e)
{
    lv_obj_t *hitbox = lv_event_get_target(e);
    lv_obj_t *visual_btn = lv_obj_get_parent(hitbox);
    if (visual_btn) {
        lv_obj_clear_state(visual_btn, LV_STATE_PRESSED);
    }
    ui_interaction_active = false;
}

/* ================================================================== */
/* Action callbacks                                                    */
/* ================================================================== */

static void custom_button2_1_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    if (touch_handler_is_swiping()) { ui_interaction_active = false; return; }
    ui_interaction_active = false;
    send_keycode(LGUI(N1));
}

static void custom_button2_2_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    if (touch_handler_is_swiping()) { ui_interaction_active = false; return; }
    ui_interaction_active = false;
    send_keycode(LGUI(N2));
}

static void custom_button2_3_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    if (touch_handler_is_swiping()) { ui_interaction_active = false; return; }
    ui_interaction_active = false;
    send_keycode(LGUI(N3));
}

static void custom_button2_4_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    if (touch_handler_is_swiping()) { ui_interaction_active = false; return; }
    ui_interaction_active = false;
    send_keycode(LGUI(N4));
}

static void custom_button2_5_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    if (touch_handler_is_swiping()) { ui_interaction_active = false; return; }
    ui_interaction_active = false;
    send_keycode(LGUI(N5));
}

static void custom_button2_6_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    if (touch_handler_is_swiping()) { ui_interaction_active = false; return; }
    ui_interaction_active = false;
    send_keycode(LGUI(N6));
}

/* ================================================================== */
/* Hitbox helper                                                       */
/* ================================================================== */

static lv_obj_t *make_center_hitbox(lv_obj_t *parent_visual_btn,
                                    lv_event_cb_t clicked_cb)
{
    lv_obj_t *hit = lv_obj_create(parent_visual_btn);
    if (!hit) return NULL;

    lv_obj_set_size(hit, ACTION_HIT_W, ACTION_HIT_H);
    lv_obj_center(hit);

    lv_obj_add_flag(hit, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(hit, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(hit, LV_OBJ_FLAG_PRESS_LOCK);

    lv_obj_set_style_bg_opa(hit, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_opa(hit, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_outline_opa(hit, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_radius(hit, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(hit, 0, LV_PART_MAIN);

    lv_obj_add_event_cb(hit, ui_press_start_cb, LV_EVENT_PRESSED,    NULL);
    lv_obj_add_event_cb(hit, ui_press_end_cb,   LV_EVENT_RELEASED,   NULL);
    lv_obj_add_event_cb(hit, ui_press_end_cb,   LV_EVENT_PRESS_LOST, NULL);
    lv_obj_add_event_cb(hit, clicked_cb,        LV_EVENT_CLICKED,    NULL);

    return hit;
}

/* ================================================================== */
/* Widget init                                                         */
/* ================================================================== */

int zmk_widget_custom_buttons2_init(struct zmk_widget_custom_buttons2 *widget,
                                   lv_obj_t *parent)
{
    if (!widget || !parent) return -EINVAL;

    widget->obj = parent;

    widget->title_label = lv_label_create(parent);
    if (!widget->title_label) return -ENOMEM;
    lv_label_set_text(widget->title_label, "Short Cuts-2");
    lv_obj_set_style_text_color(widget->title_label, lv_color_hex(0xFFFFFF), LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(widget->title_label, &lv_font_montserrat_20, LV_STATE_DEFAULT);
    lv_obj_align(widget->title_label, LV_ALIGN_TOP_MID, 0, 14);

    /* ---- BTN-1 ---- */
    custom_button2_1_bundle.visual_btn = make_visual_btn(parent, "BTN-7",
        #if CONFIG_DONGLE_SCREEN_BUTTONS_MONO
            lv_color_hex(0x000000), LV_ALIGN_CENTER, -90, -30);
        #else
            lv_color_hex(0x4AE290), LV_ALIGN_CENTER, -90, -30);
        #endif
    if (!custom_button2_1_bundle.visual_btn) return -ENOMEM;
    custom_button2_1_bundle.hitbox = make_center_hitbox(custom_button2_1_bundle.visual_btn, custom_button2_1_cb);
    if (!custom_button2_1_bundle.hitbox) return -ENOMEM;

    /* ---- BTN-2 ---- */
    custom_button2_2_bundle.visual_btn = make_visual_btn(parent, "BTN-8",
        #if CONFIG_DONGLE_SCREEN_BUTTONS_MONO
            lv_color_hex(0x000000), LV_ALIGN_CENTER, 0, -30);
        #else
            lv_color_hex(0xE24AE2), LV_ALIGN_CENTER, 0, -30);
        #endif
    if (!custom_button2_2_bundle.visual_btn) return -ENOMEM;
    custom_button2_2_bundle.hitbox = make_center_hitbox(custom_button2_2_bundle.visual_btn, custom_button2_2_cb);
    if (!custom_button2_2_bundle.hitbox) return -ENOMEM;

    /* ---- BTN-3 ---- */
    custom_button2_3_bundle.visual_btn = make_visual_btn(parent, "BTN-9",
        #if CONFIG_DONGLE_SCREEN_BUTTONS_MONO
            lv_color_hex(0x000000), LV_ALIGN_CENTER, 90, -30);
        #else
            lv_color_hex(0xE2504A), LV_ALIGN_CENTER, 90, -30);
        #endif
    if (!custom_button2_3_bundle.visual_btn) return -ENOMEM;
    custom_button2_3_bundle.hitbox = make_center_hitbox(custom_button2_3_bundle.visual_btn, custom_button2_3_cb);
    if (!custom_button2_3_bundle.hitbox) return -ENOMEM;

    /* ---- BTN-4 ---- */
    custom_button2_4_bundle.visual_btn = make_visual_btn(parent, "BTN-10",
        #if CONFIG_DONGLE_SCREEN_BUTTONS_MONO
            lv_color_hex(0x000000), LV_ALIGN_CENTER, -90, 50);
        #else
            lv_color_hex(0xE2904A), LV_ALIGN_CENTER, -90, 50);
        #endif
    if (!custom_button2_4_bundle.visual_btn) return -ENOMEM;
    custom_button2_4_bundle.hitbox = make_center_hitbox(custom_button2_4_bundle.visual_btn, custom_button2_4_cb);
    if (!custom_button2_4_bundle.hitbox) return -ENOMEM;

    /* ---- BTN-5 ---- */
    custom_button2_5_bundle.visual_btn = make_visual_btn(parent, "BTN-11",
        #if CONFIG_DONGLE_SCREEN_BUTTONS_MONO
            lv_color_hex(0x000000), LV_ALIGN_CENTER, 0, 50);
        #else
            lv_color_hex(0xDCE24A), LV_ALIGN_CENTER, 0, 50);
        #endif
    if (!custom_button2_5_bundle.visual_btn) return -ENOMEM;
    custom_button2_5_bundle.hitbox = make_center_hitbox(custom_button2_5_bundle.visual_btn, custom_button2_5_cb);
    if (!custom_button2_5_bundle.hitbox) return -ENOMEM;

    /* ---- BTN-6 ---- */
    custom_button2_6_bundle.visual_btn = make_visual_btn(parent, "BTN-12",
        #if CONFIG_DONGLE_SCREEN_BUTTONS_MONO
            lv_color_hex(0x000000), LV_ALIGN_CENTER, 90, 50);
        #else
            lv_color_hex(0x4A90E2), LV_ALIGN_CENTER, 90, 50);
        #endif
    if (!custom_button2_6_bundle.visual_btn) return -ENOMEM;
    custom_button2_6_bundle.hitbox = make_center_hitbox(custom_button2_6_bundle.visual_btn, custom_button2_6_cb);
    if (!custom_button2_6_bundle.hitbox) return -ENOMEM;

    widget->custom_button2_1_btn = custom_button2_1_bundle.visual_btn;
    widget->custom_button2_2_btn = custom_button2_2_bundle.visual_btn;
    widget->custom_button2_3_btn = custom_button2_3_bundle.visual_btn;
    widget->custom_button2_4_btn = custom_button2_4_bundle.visual_btn;
    widget->custom_button2_5_btn = custom_button2_5_bundle.visual_btn;
    widget->custom_button2_6_btn = custom_button2_6_bundle.visual_btn;

    widget->nav_hint = lv_label_create(parent);
    if (!widget->nav_hint) return -ENOMEM;
    lv_label_set_text(widget->nav_hint, "< swipe >");
    lv_obj_set_style_text_color(widget->nav_hint, lv_color_hex(0x444444), LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(widget->nav_hint, &lv_font_montserrat_20, LV_STATE_DEFAULT);
    lv_obj_align(widget->nav_hint, LV_ALIGN_BOTTOM_MID, 0, -10);

    return 0;
}

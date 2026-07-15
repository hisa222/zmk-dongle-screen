/*
 * Copyright (c) 2024 The ZMK Contributors
 * SPDX-License-Identifier: MIT
 *
 * Media Control Widget (LVGL8 / ZMK 3.5)
 * system_settings_widget.c と同じ hitbox 方式を流用。
 */

#include "media_control_widget.h"
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
#if CONFIG_DONGLE_SCREEN_BUTTONS_MONO || CONFIG_DONGLE_SCREEN_BONGO_CAT_ACTIVE
#define BORDER_COLOR_NORMAL  0xFFFFFF  /* 通常時: White */
#else
#define BORDER_COLOR_NORMAL  0x000000  /* 通常時: Black */
#endif
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

static struct action_btn_bundle mute_bundle;
static struct action_btn_bundle vol_down_bundle;
static struct action_btn_bundle vol_up_bundle;
static struct action_btn_bundle bri_down_bundle;
static struct action_btn_bundle prtscn_bundle;
static struct action_btn_bundle bri_up_bundle;

/* ================================================================== */
/* Visual button helper                                                */
/* ================================================================== */

static lv_obj_t *make_visual_btn(lv_obj_t *parent, const char *text,
                                 lv_color_t bg, lv_color_t text_color,
                                 lv_align_t align,
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

    lv_obj_set_style_border_width(obj, BORDER_WIDTH, LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(obj, LV_OPA_COVER, LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(obj, lv_color_hex(BORDER_COLOR_NORMAL), LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(obj, lv_color_hex(BORDER_COLOR_PRESSED), LV_STATE_PRESSED);

    lv_obj_t *lbl = lv_label_create(obj);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_color(lbl, text_color, LV_STATE_DEFAULT);
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

static void mute_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    if (touch_handler_is_swiping()) { ui_interaction_active = false; return; }
    ui_interaction_active = false;
    LOG_INF("Media control: MUTE");
    send_keycode(C_MUTE);
}

static void vol_down_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    if (touch_handler_is_swiping()) { ui_interaction_active = false; return; }
    ui_interaction_active = false;
    LOG_INF("Media control: VOL_DOWN");
    send_keycode(C_VOL_DN);
}

static void vol_up_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    if (touch_handler_is_swiping()) { ui_interaction_active = false; return; }
    ui_interaction_active = false;
    LOG_INF("Media control: VOL_UP");
    send_keycode(C_VOL_UP);
}

static void bri_down_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    if (touch_handler_is_swiping()) { ui_interaction_active = false; return; }
    ui_interaction_active = false;
    LOG_INF("Media control: BRI_DOWN");
    send_keycode(C_BRI_DN);
}

static void prtscn_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    if (touch_handler_is_swiping()) { ui_interaction_active = false; return; }
    ui_interaction_active = false;
    LOG_INF("Media control: PRTSCN");
    send_keycode(PSCRN);
}

static void bri_up_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    if (touch_handler_is_swiping()) { ui_interaction_active = false; return; }
    ui_interaction_active = false;
    LOG_INF("Media control: BRI_UP");
    send_keycode(C_BRI_UP);
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

int zmk_widget_media_control_init(struct zmk_widget_media_control *widget,
                                   lv_obj_t *parent)
{
    if (!widget || !parent) return -EINVAL;

    widget->obj = parent;

    widget->title_label = lv_label_create(parent);
    if (!widget->title_label) return -ENOMEM;
    lv_label_set_text(widget->title_label, "Media Control");
    lv_obj_set_style_text_color(widget->title_label, lv_color_hex(0xFFFFFF), LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(widget->title_label, &lv_font_montserrat_20, LV_STATE_DEFAULT);
    lv_obj_align(widget->title_label, LV_ALIGN_TOP_MID, 0, 14);

    /* ---- Vol Down ---- */
    vol_down_bundle.visual_btn = make_visual_btn(parent, LV_SYMBOL_VOLUME_MID,
    #if CONFIG_DONGLE_SCREEN_BUTTONS_MONO
        lv_color_hex(0x000000), lv_color_hex(0xFFFFFF), LV_ALIGN_CENTER, -90, -30);
    #elif CONFIG_DONGLE_SCREEN_BONGO_CAT_ACTIVE
        lv_color_hex(0x000000), lv_color_hex(0x4A90E2), LV_ALIGN_CENTER, -90, -30);
    #elif !CONFIG_DONGLE_SCREEN_BONGO_CAT_ACTIVE && CONFIG_DONGLE_SCREEN_BONGO_BOO_ACTIVE
        lv_color_hex(0xFFFFFF), lv_color_hex(0x000000), LV_ALIGN_CENTER, -90, -30);
    #elif !CONFIG_DONGLE_SCREEN_BONGO_CAT_ACTIVE && !CONFIG_DONGLE_SCREEN_BONGO_BOO_ACTIVE && CONFIG_DONGLE_SCREEN_BONGO_SPHEAL_ACTIVE
        lv_color_hex(0x7BDEFD), lv_color_hex(0x000000), LV_ALIGN_CENTER, -90, -30);
    #elif !CONFIG_DONGLE_SCREEN_BONGO_CAT_ACTIVE && !CONFIG_DONGLE_SCREEN_BONGO_BOO_ACTIVE && !CONFIG_DONGLE_SCREEN_BONGO_SPHEAL_ACTIVE && CONFIG_DONGLE_SCREEN_BONGO_DOE_ACTIVE
        lv_color_hex(0xFFFFFF), lv_color_hex(0x000000), LV_ALIGN_CENTER, -90, -30);
    #else
        lv_color_hex(0x4A90E2), lv_color_hex(0xFFFFFF), LV_ALIGN_CENTER, -90, -30);
    #endif
    if (!vol_down_bundle.visual_btn) return -ENOMEM;
    vol_down_bundle.hitbox = make_center_hitbox(vol_down_bundle.visual_btn, vol_down_cb);
    if (!vol_down_bundle.hitbox) return -ENOMEM;

    /* ---- Mute ---- */
    mute_bundle.visual_btn = make_visual_btn(parent, LV_SYMBOL_MUTE,
    #if CONFIG_DONGLE_SCREEN_BUTTONS_MONO
        lv_color_hex(0x000000), lv_color_hex(0xFFFFFF), LV_ALIGN_CENTER, 0, -30);
    #elif CONFIG_DONGLE_SCREEN_BONGO_CAT_ACTIVE
        lv_color_hex(0x000000), lv_color_hex(0xE24A4A), LV_ALIGN_CENTER, 0, -30);
    #elif !CONFIG_DONGLE_SCREEN_BONGO_CAT_ACTIVE && CONFIG_DONGLE_SCREEN_BONGO_BOO_ACTIVE
        lv_color_hex(0xFF0000), lv_color_hex(0x000000), LV_ALIGN_CENTER, 0, -30);
    #elif !CONFIG_DONGLE_SCREEN_BONGO_CAT_ACTIVE && !CONFIG_DONGLE_SCREEN_BONGO_BOO_ACTIVE && CONFIG_DONGLE_SCREEN_BONGO_SPHEAL_ACTIVE
        lv_color_hex(0xFF7C80), lv_color_hex(0x000000), LV_ALIGN_CENTER, 0, -30);
    #elif !CONFIG_DONGLE_SCREEN_BONGO_CAT_ACTIVE && !CONFIG_DONGLE_SCREEN_BONGO_BOO_ACTIVE && !CONFIG_DONGLE_SCREEN_BONGO_SPHEAL_ACTIVE && CONFIG_DONGLE_SCREEN_BONGO_DOE_ACTIVE
        lv_color_hex(0xF05C0A), lv_color_hex(0x000000), LV_ALIGN_CENTER, 0, -30);
    #else
        lv_color_hex(0xE24A4A), lv_color_hex(0xFFFFFF), LV_ALIGN_CENTER, 0, -30);
    #endif
    if (!mute_bundle.visual_btn) return -ENOMEM;
    mute_bundle.hitbox = make_center_hitbox(mute_bundle.visual_btn, mute_cb);
    if (!mute_bundle.hitbox) return -ENOMEM;

    /* ---- Vol Up ---- */
    vol_up_bundle.visual_btn = make_visual_btn(parent, LV_SYMBOL_VOLUME_MAX,
    #if CONFIG_DONGLE_SCREEN_BUTTONS_MONO
        lv_color_hex(0x000000), lv_color_hex(0xFFFFFF), LV_ALIGN_CENTER, 90, -30);
    #elif CONFIG_DONGLE_SCREEN_BONGO_CAT_ACTIVE
        lv_color_hex(0x000000), lv_color_hex(0x4A90E2), LV_ALIGN_CENTER, 90, -30);
    #elif !CONFIG_DONGLE_SCREEN_BONGO_CAT_ACTIVE && CONFIG_DONGLE_SCREEN_BONGO_BOO_ACTIVE
        lv_color_hex(0xFFFFFF), lv_color_hex(0x000000), LV_ALIGN_CENTER, 90, -30);
    #elif !CONFIG_DONGLE_SCREEN_BONGO_CAT_ACTIVE && !CONFIG_DONGLE_SCREEN_BONGO_BOO_ACTIVE && CONFIG_DONGLE_SCREEN_BONGO_SPHEAL_ACTIVE
        lv_color_hex(0x7BDEFD), lv_color_hex(0x000000), LV_ALIGN_CENTER, 90, -30);
    #elif !CONFIG_DONGLE_SCREEN_BONGO_CAT_ACTIVE && !CONFIG_DONGLE_SCREEN_BONGO_BOO_ACTIVE && !CONFIG_DONGLE_SCREEN_BONGO_SPHEAL_ACTIVE && CONFIG_DONGLE_SCREEN_BONGO_DOE_ACTIVE
        lv_color_hex(0xFFFFFF), lv_color_hex(0x000000), LV_ALIGN_CENTER, 90, -30);
    #else
        lv_color_hex(0x4A90E2), lv_color_hex(0xFFFFFF), LV_ALIGN_CENTER, 90, -30);
    #endif
    if (!vol_up_bundle.visual_btn) return -ENOMEM;
    vol_up_bundle.hitbox = make_center_hitbox(vol_up_bundle.visual_btn, vol_up_cb);
    if (!vol_up_bundle.hitbox) return -ENOMEM;

    /* ---- Brightness Down ---- */
    bri_down_bundle.visual_btn = make_visual_btn(parent, "BRI-",
    #if CONFIG_DONGLE_SCREEN_BUTTONS_MONO
        lv_color_hex(0x000000), lv_color_hex(0xFFFFFF), LV_ALIGN_CENTER, -90, 50);
    #elif CONFIG_DONGLE_SCREEN_BONGO_CAT_ACTIVE
        lv_color_hex(0x000000), lv_color_hex(0x4A90E2), LV_ALIGN_CENTER, -90, 50);
    #elif !CONFIG_DONGLE_SCREEN_BONGO_CAT_ACTIVE && CONFIG_DONGLE_SCREEN_BONGO_BOO_ACTIVE
        lv_color_hex(0xFFFFFF), lv_color_hex(0x000000), LV_ALIGN_CENTER, -90, 50);
    #elif !CONFIG_DONGLE_SCREEN_BONGO_CAT_ACTIVE && !CONFIG_DONGLE_SCREEN_BONGO_BOO_ACTIVE && CONFIG_DONGLE_SCREEN_BONGO_SPHEAL_ACTIVE
        lv_color_hex(0x7BDEFD), lv_color_hex(0x000000), LV_ALIGN_CENTER, -90, 50);
    #elif !CONFIG_DONGLE_SCREEN_BONGO_CAT_ACTIVE && !CONFIG_DONGLE_SCREEN_BONGO_BOO_ACTIVE && !CONFIG_DONGLE_SCREEN_BONGO_SPHEAL_ACTIVE && CONFIG_DONGLE_SCREEN_BONGO_DOE_ACTIVE
        lv_color_hex(0xFFFFFF), lv_color_hex(0x000000), LV_ALIGN_CENTER, -90, 50);
    #else
        lv_color_hex(0x4A90E2), lv_color_hex(0xFFFFFF), LV_ALIGN_CENTER, -90, 50);
    #endif
    if (!bri_down_bundle.visual_btn) return -ENOMEM;
    bri_down_bundle.hitbox = make_center_hitbox(bri_down_bundle.visual_btn, bri_down_cb);
    if (!bri_down_bundle.hitbox) return -ENOMEM;

    /* ---- Print Screen ---- */
    prtscn_bundle.visual_btn = make_visual_btn(parent, "PRTSC",
    #if CONFIG_DONGLE_SCREEN_BUTTONS_MONO
        lv_color_hex(0x000000), lv_color_hex(0xFFFFFF), LV_ALIGN_CENTER, 0, 50);
    #elif CONFIG_DONGLE_SCREEN_BONGO_CAT_ACTIVE
        lv_color_hex(0x000000), lv_color_hex(0xE2A64A), LV_ALIGN_CENTER, 0, 50);
    #elif !CONFIG_DONGLE_SCREEN_BONGO_CAT_ACTIVE && CONFIG_DONGLE_SCREEN_BONGO_BOO_ACTIVE
        lv_color_hex(0xFF0000), lv_color_hex(0x000000), LV_ALIGN_CENTER, 0, 50);
    #elif !CONFIG_DONGLE_SCREEN_BONGO_CAT_ACTIVE && !CONFIG_DONGLE_SCREEN_BONGO_BOO_ACTIVE && CONFIG_DONGLE_SCREEN_BONGO_SPHEAL_ACTIVE
        lv_color_hex(0xFEEECE), lv_color_hex(0x000000), LV_ALIGN_CENTER, 0, 50);
    #elif !CONFIG_DONGLE_SCREEN_BONGO_CAT_ACTIVE && !CONFIG_DONGLE_SCREEN_BONGO_BOO_ACTIVE && !CONFIG_DONGLE_SCREEN_BONGO_SPHEAL_ACTIVE && CONFIG_DONGLE_SCREEN_BONGO_DOE_ACTIVE
        lv_color_hex(0xFFFF00), lv_color_hex(0x000000), LV_ALIGN_CENTER, 0, 50);
    #else
        lv_color_hex(0xE2A64A), lv_color_hex(0xFFFFFF), LV_ALIGN_CENTER, 0, 50);
    #endif
    if (!prtscn_bundle.visual_btn) return -ENOMEM;
    prtscn_bundle.hitbox = make_center_hitbox(prtscn_bundle.visual_btn, prtscn_cb);
    if (!prtscn_bundle.hitbox) return -ENOMEM;

    /* ---- Brightness Up ---- */
    bri_up_bundle.visual_btn = make_visual_btn(parent, "BRI+",
    #if CONFIG_DONGLE_SCREEN_BUTTONS_MONO
        lv_color_hex(0x000000), lv_color_hex(0xFFFFFF), LV_ALIGN_CENTER, 90, 50);
    #elif CONFIG_DONGLE_SCREEN_BONGO_CAT_ACTIVE
        lv_color_hex(0x000000), lv_color_hex(0x4A90E2), LV_ALIGN_CENTER, 90, 50);
    #elif !CONFIG_DONGLE_SCREEN_BONGO_CAT_ACTIVE && CONFIG_DONGLE_SCREEN_BONGO_BOO_ACTIVE
        lv_color_hex(0xFFFFFF), lv_color_hex(0x000000), LV_ALIGN_CENTER, 90, 50);
    #elif !CONFIG_DONGLE_SCREEN_BONGO_CAT_ACTIVE && !CONFIG_DONGLE_SCREEN_BONGO_BOO_ACTIVE && CONFIG_DONGLE_SCREEN_BONGO_SPHEAL_ACTIVE
        lv_color_hex(0x7BDEFD), lv_color_hex(0x000000), LV_ALIGN_CENTER, 90, 50);
    #elif !CONFIG_DONGLE_SCREEN_BONGO_CAT_ACTIVE && !CONFIG_DONGLE_SCREEN_BONGO_BOO_ACTIVE && !CONFIG_DONGLE_SCREEN_BONGO_SPHEAL_ACTIVE && CONFIG_DONGLE_SCREEN_BONGO_DOE_ACTIVE
        lv_color_hex(0xFFFFFF), lv_color_hex(0x000000), LV_ALIGN_CENTER, 90, 50);
    #else
        lv_color_hex(0x4A90E2), lv_color_hex(0xFFFFFF), LV_ALIGN_CENTER, 90, 50);
    #endif
    if (!bri_up_bundle.visual_btn) return -ENOMEM;
    bri_up_bundle.hitbox = make_center_hitbox(bri_up_bundle.visual_btn, bri_up_cb);
    if (!bri_up_bundle.hitbox) return -ENOMEM;

    widget->vol_down_btn = vol_down_bundle.visual_btn;
    widget->mute_btn     = mute_bundle.visual_btn;
    widget->vol_up_btn   = vol_up_bundle.visual_btn;
    widget->bri_down_btn = bri_down_bundle.visual_btn;
    widget->prtscn_btn   = prtscn_bundle.visual_btn;
    widget->bri_up_btn   = bri_up_bundle.visual_btn;

    widget->nav_hint = lv_label_create(parent);
    if (!widget->nav_hint) return -ENOMEM;
    lv_label_set_text(widget->nav_hint, "< swipe >");
    lv_obj_set_style_text_color(widget->nav_hint, lv_color_hex(0x444444), LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(widget->nav_hint, &lv_font_montserrat_20, LV_STATE_DEFAULT);
    lv_obj_align(widget->nav_hint, LV_ALIGN_BOTTOM_MID, 0, -10);

    return 0;
}

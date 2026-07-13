/*
 * Copyright (c) 2024 The ZMK Contributors
 * SPDX-License-Identifier: MIT
 *
 * Main Screen Buttons Widget (LVGL8 / ZMK 3.5)
 *
 * custom_buttons.c と見た目(サイズ/枠線/ヒットボックス方式)は揃えていますが、
 * 挙動(コールバック/送信キーコード)は完全に独立して実装しています。
 * custom_buttons 側を変更してもこちらには影響せず、その逆も同様です。
 *
 *   ROW1 (CONFIG_DONGLE_SCREEN_MAIN_BUTTONS_ROW1)
 *     | main_btn_1 | | main_btn_2 | | main_btn_3 |
 *
 *   ROW2 (CONFIG_DONGLE_SCREEN_MAIN_BUTTONS_ROW2)
 *     | main_btn_4 | | main_btn_5 | | main_btn_6 |
 */

#include "main_screen_buttons.h"
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

/* custom_buttons と同じサイズを踏襲（値は独立して定義） */
#define MAIN_BTN_W 76
#define MAIN_BTN_H 60
#define MAIN_HIT_W 60
#define MAIN_HIT_H 50

#if CONFIG_DONGLE_SCREEN_BUTTONS_MONO
#define MAIN_BORDER_COLOR_NORMAL  0xFFFFFF  /* 通常時: White */
#else
#define MAIN_BORDER_COLOR_NORMAL  0x000000  /* 通常時: Black */
#endif
#define MAIN_BORDER_COLOR_PRESSED 0x00FF00  /* 押下時: Green */
#define MAIN_BORDER_WIDTH 2

/* ================================================================== */
/* Keycode送信ヘルパー（main_screen_buttons専用。custom_buttons側とは独立） */
/* ================================================================== */

static void main_btn_send_keycode(uint32_t keycode)
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

static void main_btn_key_press(uint32_t keycode)
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
    zmk_behavior_invoke_binding(&binding, event, true);
}

static void main_btn_key_release(uint32_t keycode)
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
    zmk_behavior_invoke_binding(&binding, event, false);
}

/* ================================================================== */
/* Widget-private state                                                */
/* ================================================================== */

struct main_btn_bundle {
    lv_obj_t *visual_btn;
    lv_obj_t *hitbox;
};

static struct main_btn_bundle main_button_1_bundle;
static struct main_btn_bundle main_button_2_bundle;
static struct main_btn_bundle main_button_3_bundle;
static struct main_btn_bundle main_button_4_bundle;
static struct main_btn_bundle main_button_5_bundle;
static struct main_btn_bundle main_button_6_bundle;

/* ================================================================== */
/* Visual button helper                                                */
/* ================================================================== */

static lv_obj_t *make_main_visual_btn(lv_obj_t *parent, const char *text, lv_color_t bg)
{
    lv_obj_t *obj = lv_obj_create(parent);
    if (!obj) {
        LOG_ERR("main_screen_buttons: failed to allocate visual_btn (LVGL memory pool exhausted?)");
        return NULL;
    }

    lv_obj_set_size(obj, MAIN_BTN_W, MAIN_BTN_H);

    /*
     * 初期位置は仮置き（中央に重ねて生成）。
     * 実際の配置は custom_status_screen.c 側で
     * lv_obj_align(widget.main_btn_N, ...) を呼んで編集してください。
     */
    lv_obj_align(obj, LV_ALIGN_CENTER, 0, 0);

    lv_obj_clear_flag(obj, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_set_style_bg_color(obj, bg, LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_STATE_DEFAULT);
    lv_obj_set_style_radius(obj, 12, LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(obj, 0, LV_STATE_DEFAULT);

    lv_obj_set_style_border_width(obj, MAIN_BORDER_WIDTH, LV_STATE_DEFAULT);
    lv_obj_set_style_border_opa(obj, LV_OPA_COVER, LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(obj, lv_color_hex(MAIN_BORDER_COLOR_NORMAL), LV_STATE_DEFAULT);
    lv_obj_set_style_border_color(obj, lv_color_hex(MAIN_BORDER_COLOR_PRESSED), LV_STATE_PRESSED);

    lv_obj_t *lbl = lv_label_create(obj);
    if (!lbl) {
        LOG_ERR("main_screen_buttons: failed to allocate label (LVGL memory pool exhausted?)");
        return obj; /* ボタン本体はできているので枠だけでも表示させる */
    }
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_color(lbl, lv_color_hex(0xFFFFFF), LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_20, LV_STATE_DEFAULT);
    lv_obj_center(lbl);

    return obj;
}

/* ================================================================== */
/* Interaction state callbacks                                         */
/* ================================================================== */

static void main_btn_press_start_cb(lv_event_t *e)
{
    lv_obj_t *hitbox = lv_event_get_target(e);
    lv_obj_t *visual_btn = lv_obj_get_parent(hitbox);
    if (visual_btn) {
        lv_obj_add_state(visual_btn, LV_STATE_PRESSED);
    }
    ui_interaction_active = true;
}

static void main_btn_press_end_cb(lv_event_t *e)
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
/*                                                                      */
/* ここは custom_buttons とは無関係の独自機能です。                     */
/* 送信キーコードや処理内容は自由に書き換えてください。                 */
/* ================================================================== */

static void trigger_unlock(void)
{
    main_btn_send_keycode(ENTER);
    k_msleep(1000);
    main_btn_send_keycode(N0);
    k_msleep(50);
    main_btn_send_keycode(N2);
    k_msleep(50);
    main_btn_send_keycode(N2);
    k_msleep(50);
    main_btn_send_keycode(N2);
    k_msleep(50);
    main_btn_send_keycode(ENTER);
    k_msleep(50);
}

static void trigger_close_window(void)
{
    main_btn_key_press(LALT);
    k_msleep(100);

    main_btn_send_keycode(SPACE);
    k_msleep(100);
    main_btn_send_keycode(N);;
    k_msleep(100);

    main_btn_key_release(LALT);
}

static void trigger_sleep(void)
{
    main_btn_key_press(MOD_LGUI);
    k_msleep(100);
    main_btn_send_keycode(X);
    k_msleep(100);
    main_btn_key_release(MOD_LGUI);
    k_msleep(100);
    main_btn_send_keycode(U);
    k_msleep(100);
    main_btn_send_keycode(S);
}

static void main_button_1_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    if (touch_handler_is_swiping()) { ui_interaction_active = false; return; }
    ui_interaction_active = false;

    trigger_unlock();
}

static void main_button_2_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    if (touch_handler_is_swiping()) { ui_interaction_active = false; return; }
    ui_interaction_active = false;
    
    trigger_close_window();
}

static void main_button_3_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    if (touch_handler_is_swiping()) { ui_interaction_active = false; return; }
    ui_interaction_active = false;
    
    trigger_sleep();
}

static void main_button_4_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    if (touch_handler_is_swiping()) { ui_interaction_active = false; return; }
    ui_interaction_active = false;
    /* TODO: ここに BTN4 の機能を実装してください */
    main_btn_send_keycode(N4);
}

static void main_button_5_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    if (touch_handler_is_swiping()) { ui_interaction_active = false; return; }
    ui_interaction_active = false;
    /* TODO: ここに BTN5 の機能を実装してください */
    main_btn_send_keycode(N5);
}

static void main_button_6_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    if (touch_handler_is_swiping()) { ui_interaction_active = false; return; }
    ui_interaction_active = false;
    /* TODO: ここに BTN6 の機能を実装してください */
    main_btn_send_keycode(N6);
}

/* ================================================================== */
/* Hitbox helper                                                       */
/* ================================================================== */

static lv_obj_t *make_main_center_hitbox(lv_obj_t *parent_visual_btn, lv_event_cb_t clicked_cb)
{
    lv_obj_t *hit = lv_obj_create(parent_visual_btn);
    if (!hit) {
        LOG_ERR("main_screen_buttons: failed to allocate hitbox (LVGL memory pool exhausted?)");
        return NULL;
    }

    lv_obj_set_size(hit, MAIN_HIT_W, MAIN_HIT_H);
    lv_obj_center(hit);

    lv_obj_add_flag(hit, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(hit, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(hit, LV_OBJ_FLAG_PRESS_LOCK);

    lv_obj_set_style_bg_opa(hit, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_opa(hit, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_outline_opa(hit, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_radius(hit, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(hit, 0, LV_PART_MAIN);

    lv_obj_add_event_cb(hit, main_btn_press_start_cb, LV_EVENT_PRESSED,    NULL);
    lv_obj_add_event_cb(hit, main_btn_press_end_cb,   LV_EVENT_RELEASED,   NULL);
    lv_obj_add_event_cb(hit, main_btn_press_end_cb,   LV_EVENT_PRESS_LOST, NULL);
    lv_obj_add_event_cb(hit, clicked_cb,               LV_EVENT_CLICKED,    NULL);

    return hit;
}

/* ================================================================== */
/* Widget init                                                         */
/* ================================================================== */

int zmk_widget_main_screen_buttons_init(struct zmk_widget_main_screen_buttons *widget,
                                        lv_obj_t *parent)
{
    if (!widget || !parent) return -EINVAL;

    widget->obj = parent;

    widget->main_btn_1 = NULL;
    widget->main_btn_2 = NULL;
    widget->main_btn_3 = NULL;
    widget->main_btn_4 = NULL;
    widget->main_btn_5 = NULL;
    widget->main_btn_6 = NULL;

#if CONFIG_DONGLE_SCREEN_MAIN_BUTTONS_ROW1
    /* ---- BTN-1 ---- */
    main_button_1_bundle.visual_btn = make_main_visual_btn(parent, "UnLck", 
        #if CONFIG_DONGLE_SCREEN_BUTTONS_MONO
            lv_color_hex(0x000000));
        #else
            lv_color_hex(0x4AE290));
        #endif
    if (!main_button_1_bundle.visual_btn) return -ENOMEM;
    main_button_1_bundle.hitbox = make_main_center_hitbox(main_button_1_bundle.visual_btn, main_button_1_cb);
    if (!main_button_1_bundle.hitbox) return -ENOMEM;
    widget->main_btn_1 = main_button_1_bundle.visual_btn;

    /* ---- BTN-2 ---- */
    main_button_2_bundle.visual_btn = make_main_visual_btn(parent, "Close", 
        #if CONFIG_DONGLE_SCREEN_BUTTONS_MONO
            lv_color_hex(0x000000));
        #else
            lv_color_hex(0xDCE24A));
        #endif
    if (!main_button_2_bundle.visual_btn) return -ENOMEM;
    main_button_2_bundle.hitbox = make_main_center_hitbox(main_button_2_bundle.visual_btn, main_button_2_cb);
    if (!main_button_2_bundle.hitbox) return -ENOMEM;
    widget->main_btn_2 = main_button_2_bundle.visual_btn;

    /* ---- BTN-3 ---- */
    main_button_3_bundle.visual_btn = make_main_visual_btn(parent, "Sleep", 
        #if CONFIG_DONGLE_SCREEN_BUTTONS_MONO
            lv_color_hex(0x000000));
        #else
            lv_color_hex(0xE2904A));
        #endif
    if (!main_button_3_bundle.visual_btn) return -ENOMEM;
    main_button_3_bundle.hitbox = make_main_center_hitbox(main_button_3_bundle.visual_btn, main_button_3_cb);
    if (!main_button_3_bundle.hitbox) return -ENOMEM;
    widget->main_btn_3 = main_button_3_bundle.visual_btn;
#endif /* CONFIG_DONGLE_SCREEN_MAIN_BUTTONS_ROW1 */

#if CONFIG_DONGLE_SCREEN_MAIN_BUTTONS_ROW2
    /* ---- BTN-4 ---- */
    main_button_4_bundle.visual_btn = make_main_visual_btn(parent, "BTN4", 
        #if CONFIG_DONGLE_SCREEN_BUTTONS_MONO
            lv_color_hex(0x000000));
        #else
            lv_color_hex(0xE2504A));
        #endif
    if (!main_button_4_bundle.visual_btn) return -ENOMEM;
    main_button_4_bundle.hitbox = make_main_center_hitbox(main_button_4_bundle.visual_btn, main_button_4_cb);
    if (!main_button_4_bundle.hitbox) return -ENOMEM;
    widget->main_btn_4 = main_button_4_bundle.visual_btn;

    /* ---- BTN-5 ---- */
    main_button_5_bundle.visual_btn = make_main_visual_btn(parent, "BTN5", 
        #if CONFIG_DONGLE_SCREEN_BUTTONS_MONO
            lv_color_hex(0x000000));
        #else
            lv_color_hex(0xE24AE2));
        #endif
    if (!main_button_5_bundle.visual_btn) return -ENOMEM;
    main_button_5_bundle.hitbox = make_main_center_hitbox(main_button_5_bundle.visual_btn, main_button_5_cb);
    if (!main_button_5_bundle.hitbox) return -ENOMEM;
    widget->main_btn_5 = main_button_5_bundle.visual_btn;

    /* ---- BTN-6 ---- */
    main_button_6_bundle.visual_btn = make_main_visual_btn(parent, "BTN6", 
        #if CONFIG_DONGLE_SCREEN_BUTTONS_MONO
            lv_color_hex(0x000000));
        #else
            lv_color_hex(0x4AE290));
        #endif
    if (!main_button_6_bundle.visual_btn) return -ENOMEM;
    main_button_6_bundle.hitbox = make_main_center_hitbox(main_button_6_bundle.visual_btn, main_button_6_cb);
    if (!main_button_6_bundle.hitbox) return -ENOMEM;
    widget->main_btn_6 = main_button_6_bundle.visual_btn;
#endif /* CONFIG_DONGLE_SCREEN_MAIN_BUTTONS_ROW2 */

    return 0;
}

/*
 * Copyright (c) 2024 The ZMK Contributors
 * SPDX-License-Identifier: MIT
 *
 * Dongle custom status screen
 *
 * 重要:
 * - Prospector 寄せの UI 操作フラグ(ui_interaction_active) は維持
 * - ただし画面管理は dongle_screen 既存方式に戻し、
 *   各 screen を個別に生成して lv_scr_load() で切り替える
 *
 * 理由:
 * - output/layer/battery/wpm/mod/bongo の各 widget は
 *   static な widget state を持ち、ZMK イベント購読と絡むため、
 *   lv_obj_clean() による単一 screen 再構築方式だと不安定になりやすい
 * - 実際に「画面遷移後フリーズ」「メイン画面の表示欠落」が発生しているため、
 *   screen 単位の切替へ戻す
 */

#include "custom_status_screen.h"
#include "touch_handler.h"
#include "events/swipe_gesture_event.h"
#include "display_settings.h"
#include "widgets/brightness_screen.h"
#include "widgets/system_settings_widget.h"
#include "widgets/media_control_widget.h"

#if CONFIG_DONGLE_SCREEN_OUTPUT_ACTIVE
#include "widgets/output_status.h"
static struct zmk_widget_output_status output_status_widget;
#endif

#if CONFIG_DONGLE_SCREEN_LAYER_ACTIVE
#include "widgets/layer_status.h"
static struct zmk_widget_layer_status layer_status_widget;

/* 横スクロールレイヤー表示ウィジェット */
#include "widgets/layer_slider.h"
static struct zmk_widget_layer_slider layer_slider_widget;
#endif

#if CONFIG_DONGLE_SCREEN_BATTERY_ACTIVE
#include "widgets/battery_status.h"
static struct zmk_widget_dongle_battery_status dongle_battery_status_widget;
#endif

#if CONFIG_DONGLE_SCREEN_WPM_ACTIVE
#include "widgets/wpm_status.h"
static struct zmk_widget_wpm_status wpm_status_widget;
#endif

#if CONFIG_DONGLE_SCREEN_MODIFIER_ACTIVE
#include "widgets/mod_status.h"
static struct zmk_widget_mod_status mod_widget;
#endif

#if CONFIG_DONGLE_SCREEN_NAME_ACTIVE
#include "widgets/keyboard_name_status.h"
static struct zmk_widget_keyboard_name_status keyboard_name_status_widget;
#endif

#if CONFIG_DONGLE_SCREEN_BONGO_CAT_ACTIVE
#include "widgets/bongo_cat.h"
static struct zmk_widget_bongo_cat main_bongo_cat_widget;
#endif

#if !CONFIG_DONGLE_SCREEN_BONGO_CAT_ACTIVE && CONFIG_DONGLE_SCREEN_BONGO_BOO_ACTIVE
#include "widgets/bongo_boo.h"
static struct zmk_widget_bongo_boo main_bongo_boo_widget;
#endif

static struct zmk_widget_brightness_screen brightness_widget;
static struct zmk_widget_system_settings system_settings_widget;
static struct zmk_widget_media_control media_control_widget;

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <lvgl.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

/* ================================================================== */
/* Shared interaction state                                           */
/* ================================================================== */

/*
 * brightness_screen / system_settings_widget / touch_handler から参照される。
 * true の間は画面スワイプ遷移を抑止する。
 */
volatile bool ui_interaction_active = false;

/* ================================================================== */
/* Screen management                                                  */
/* ================================================================== */

#define SCREEN_COUNT 4

enum dongle_screen_id {
    SCREEN_MAIN = 0,
    SCREEN_BRIGHTNESS = 1,
    SCREEN_SYSTEM_SETTINGS = 2,
    SCREEN_MEDIA_CONTROL = 3,
};

static lv_obj_t *screens[SCREEN_COUNT];
static int current_screen_index = 0;
static bool lvgl_indev_registered = false;

/* global style */
lv_style_t global_style;

/* ================================================================== */
/* Helpers                                                            */
/* ================================================================== */

static void ensure_lvgl_indev_registered(void)
{
    if (lvgl_indev_registered) {
        return;
    }

    int ret = touch_handler_register_lvgl_indev();
    if (ret == 0) {
        lvgl_indev_registered = true;
        LOG_INF("LVGL indev registered");
    } else {
        LOG_ERR("Failed to register LVGL indev (%d)", ret);
    }
}

static lv_obj_t *make_screen(void)
{
    lv_obj_t *screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_add_style(screen, &global_style, LV_PART_MAIN);
    lv_obj_clear_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    return screen;
}

/* ================================================================== */
/* Main screen                                                        */
/* ================================================================== */

static lv_obj_t *create_main_screen(void)
{
    lv_obj_t *screen = make_screen();

#if CONFIG_DONGLE_SCREEN_BONGO_CAT_ACTIVE || CONFIG_DONGLE_SCREEN_BONGO_BOO_ACTIVE

#if CONFIG_DONGLE_SCREEN_OUTPUT_ACTIVE
    zmk_widget_output_status_init(&output_status_widget, screen);
    lv_obj_align(zmk_widget_output_status_obj(&output_status_widget),
                 LV_ALIGN_TOP_MID, 0, 10);
#endif

#if CONFIG_DONGLE_SCREEN_BATTERY_ACTIVE
    zmk_widget_dongle_battery_status_init(&dongle_battery_status_widget, screen);
    lv_obj_align(zmk_widget_dongle_battery_status_obj(&dongle_battery_status_widget),
                 LV_ALIGN_TOP_MID, 0, 10);
#endif

#if CONFIG_DONGLE_SCREEN_WPM_ACTIVE
    zmk_widget_wpm_status_init(&wpm_status_widget, screen);
    lv_obj_align(zmk_widget_wpm_status_obj(&wpm_status_widget),
                 LV_ALIGN_TOP_LEFT, 20, 20);
#endif

#if CONFIG_DONGLE_SCREEN_LAYER_ACTIVE
//    zmk_widget_layer_status_init(&layer_status_widget, screen);
//    lv_obj_align(zmk_widget_layer_status_obj(&layer_status_widget),
//                 LV_ALIGN_TOP_MID, 0, 50);

    /* 横スクロールレイヤーウィジェット */
    zmk_widget_layer_slider_init(&layer_slider_widget, screen);
    lv_obj_align(zmk_widget_layer_slider_obj(&layer_slider_widget),
                 LV_ALIGN_TOP_MID, 0, 50);
#endif
    
/*
#if CONFIG_DONGLE_SCREEN_MODIFIER_ACTIVE
    zmk_widget_mod_status_init(&mod_widget, screen);
    lv_obj_align(zmk_widget_mod_status_obj(&mod_widget),
                 LV_ALIGN_TOP_MID, 0, 85);
#endif
*/
#if CONFIG_DONGLE_SCREEN_BONGO_CAT_ACTIVE
    zmk_widget_bongo_cat_init(&main_bongo_cat_widget, screen);
    lv_obj_align(zmk_widget_bongo_cat_obj(&main_bongo_cat_widget),
                 LV_ALIGN_BOTTOM_MID, 0, 0);
#endif

#if !CONFIG_DONGLE_SCREEN_BONGO_CAT_ACTIVE && CONFIG_DONGLE_SCREEN_BONGO_BOO_ACTIVE
    zmk_widget_bongo_boo_init(&main_bongo_boo_widget, screen);
    lv_obj_align(zmk_widget_bongo_boo_obj(&main_bongo_boo_widget),
                 LV_ALIGN_BOTTOM_MID, 0, 0);
#endif

#else

#if CONFIG_DONGLE_SCREEN_OUTPUT_ACTIVE
    zmk_widget_output_status_init(&output_status_widget, screen);
    lv_obj_align(zmk_widget_output_status_obj(&output_status_widget),
                 LV_ALIGN_TOP_MID, 0, 10);
#endif

#if CONFIG_DONGLE_SCREEN_BATTERY_ACTIVE
    zmk_widget_dongle_battery_status_init(&dongle_battery_status_widget, screen);
    lv_obj_align(zmk_widget_dongle_battery_status_obj(&dongle_battery_status_widget),
                 LV_ALIGN_TOP_MID, 0, 10);
#endif

#if CONFIG_DONGLE_SCREEN_WPM_ACTIVE
    zmk_widget_wpm_status_init(&wpm_status_widget, screen);
    lv_obj_align(zmk_widget_wpm_status_obj(&wpm_status_widget),
                 LV_ALIGN_TOP_LEFT, 20, 20);
#endif

#if CONFIG_DONGLE_SCREEN_LAYER_ACTIVE
//    zmk_widget_layer_status_init(&layer_status_widget, screen);
//    lv_obj_align(zmk_widget_layer_status_obj(&layer_status_widget),
//                 LV_ALIGN_CENTER, 0, 40);

    /* 横スクロールレイヤーウィジェット */
    zmk_widget_layer_slider_init(&layer_slider_widget, screen);
    lv_obj_align(zmk_widget_layer_slider_obj(&layer_slider_widget),
                 LV_ALIGN_CENTER, 0, 40);
#endif

#if CONFIG_DONGLE_SCREEN_MODIFIER_ACTIVE
    zmk_widget_mod_status_init(&mod_widget, screen);
    lv_obj_align(zmk_widget_mod_status_obj(&mod_widget),
                 LV_ALIGN_CENTER, 0, 90);
#endif

#if CONFIG_DONGLE_SCREEN_NAME_ACTIVE
    /* キーボード名ウィジェット（レイヤー表示の上） */
    zmk_widget_keyboard_name_status_init(&keyboard_name_status_widget, screen);
    lv_obj_align(zmk_widget_keyboard_name_status_obj(&keyboard_name_status_widget),
                 LV_ALIGN_CENTER, 0, -30);   /* layer が y=0 なのでその上 -20px */
#endif

#endif

    return screen;
}

/* ================================================================== */
/* Brightness screen                                                  */
/* ================================================================== */

static lv_obj_t *create_brightness_screen(void)
{
    lv_obj_t *screen = make_screen();
    zmk_widget_brightness_screen_init(&brightness_widget, screen);
    return screen;
}

/* ================================================================== */
/* System settings screen                                             */
/* ================================================================== */

static lv_obj_t *create_system_settings_screen(void)
{
    lv_obj_t *screen = make_screen();
    zmk_widget_system_settings_init(&system_settings_widget, screen);
    return screen;
}

/* ================================================================== */
/* Media control screen                                             */
/* ================================================================== */

static lv_obj_t *create_media_control_screen(void)
{
    lv_obj_t *screen = make_screen();
    zmk_widget_media_control_init(&media_control_widget, screen);
    return screen;
}

/* ================================================================== */
/* Screen transition helpers                                          */
/* ================================================================== */

static void activate_screen(int next)
{
    if (next < 0 || next >= SCREEN_COUNT) {
        return;
    }

    if (next == current_screen_index) {
        return;
    }

    /*
     * brightness / system settings を離れるときは interaction フラグを落とす。
     * （画面切替時に押下状態が残るのを防ぐ）
     */
    ui_interaction_active = false;

    current_screen_index = next;
    lv_scr_load(screens[next]);
}

/* ================================================================== */
/* Swipe event listener                                               */
/* ================================================================== */

/*
 * 画面遷移マップ（MAIN を中心とした十字形ナビゲーション）:
 *
 *   横軸 (LEFT / RIGHT): MAIN <-> MEDIA_CONTROL の2画面ループ。
 *     SYSTEM_SETTINGS / BRIGHTNESS 上で左右スワイプした場合も、
 *     MAIN を経由せず直接 MEDIA_CONTROL へ移動する。
 *
 *   縦軸 (UP):   MAIN -> SYSTEM_SETTINGS -> BRIGHTNESS -> MAIN … の正順ループ
 *   縦軸 (DOWN): MAIN -> BRIGHTNESS -> SYSTEM_SETTINGS -> MAIN … の逆順ループ
 *     MEDIA_CONTROL 上で上下スワイプした場合も、MAIN を経由せず
 *     直接ループの最初のステップ（UPならSYSTEM_SETTINGS、DOWNならBRIGHTNESS）へ移動する。
 */
static int next_screen_for_direction(int current, enum swipe_direction direction)
{
    switch (direction) {
    case SWIPE_DIRECTION_LEFT:
    case SWIPE_DIRECTION_RIGHT:
        /* MEDIA_CONTROL からは MAIN へ、それ以外（MAIN含む）からは
         * MAIN経由せず直接 MEDIA_CONTROL へ。 */
        if (current == SCREEN_MEDIA_CONTROL) {
            return SCREEN_MAIN;
        }
        return SCREEN_MEDIA_CONTROL;

    case SWIPE_DIRECTION_UP:
        switch (current) {
        case SCREEN_MAIN:
            return SCREEN_SYSTEM_SETTINGS;
        case SCREEN_SYSTEM_SETTINGS:
            return SCREEN_BRIGHTNESS;
        case SCREEN_BRIGHTNESS:
            return SCREEN_MAIN;
        default: /* MEDIA_CONTROL: MAIN経由せず正順ループの先頭へ */
            return SCREEN_SYSTEM_SETTINGS;
        }

    case SWIPE_DIRECTION_DOWN:
        switch (current) {
        case SCREEN_MAIN:
            return SCREEN_BRIGHTNESS;
        case SCREEN_BRIGHTNESS:
            return SCREEN_SYSTEM_SETTINGS;
        case SCREEN_SYSTEM_SETTINGS:
            return SCREEN_MAIN;
        default: /* MEDIA_CONTROL: MAIN経由せず逆順ループの先頭へ */
            return SCREEN_BRIGHTNESS;
        }

    case SWIPE_DIRECTION_DOUBLE_TAP:
        return SCREEN_MAIN;

    default:
        return current;
    }
}

static int swipe_gesture_event_handler(const zmk_event_t *eh)
{
    const struct zmk_swipe_gesture_event *ev = as_zmk_swipe_gesture_event(eh);
    if (!ev) {
        return ZMK_EV_EVENT_BUBBLE;
    }

    /*
     * brightness スライダ操作中 / quick actions ボタン押下中は
     * 画面遷移を抑止する。
     */
    if (ui_interaction_active) {
        return ZMK_EV_EVENT_BUBBLE;
    }

    int next = next_screen_for_direction(current_screen_index, ev->direction);
    activate_screen(next);
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(swipe_gesture_screen, swipe_gesture_event_handler);
ZMK_SUBSCRIPTION(swipe_gesture_screen, zmk_swipe_gesture_event);

/* ================================================================== */
/* Entry point                                                        */
/* ================================================================== */

lv_obj_t *zmk_display_status_screen(void)
{
    display_settings_init();

    lv_style_init(&global_style);
    lv_style_set_text_color(&global_style, lv_color_white());
    lv_style_set_text_letter_space(&global_style, 1);
    lv_style_set_text_line_space(&global_style, 1);

    screens[SCREEN_MAIN] = create_main_screen();
    screens[SCREEN_BRIGHTNESS] = create_brightness_screen();
    screens[SCREEN_SYSTEM_SETTINGS] = create_system_settings_screen();
    screens[SCREEN_MEDIA_CONTROL] = create_media_control_screen();
    
    ensure_lvgl_indev_registered();

    current_screen_index = SCREEN_MAIN;
    ui_interaction_active = false;

    return screens[SCREEN_MAIN];
}

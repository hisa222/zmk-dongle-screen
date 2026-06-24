/*
 * Copyright (c) 2024 The ZMK Contributors
 * SPDX-License-Identifier: MIT
 *
 * Dongle screen status UI
 *
 * Prospector の操作感に寄せるため、画面遷移は以下の方針に変更している。
 *
 * 1. スワイプイベント受信時に直接 lv_scr_load() / lv_obj_clean() しない
 *    → イベントハンドラでは pending_swipe に方向を記録するだけ
 *
 * 2. 実際の画面遷移は LVGL timer 側で実行する
 *    → ISR/Zephyr callback 側から LVGL API を触らない
 *
 * 3. brightness スライダや system settings ボタン操作中は
 *    ui_interaction_active=true にしてスワイプ遷移を抑止する
 *
 * 4. 画面は「screen を複数持って lv_scr_load で切替」ではなく、
 *    単一 screen_obj を lv_obj_clean() して再構築する
 *
 * LVGL 8 / ZMK 3.5 向け実装。
 */

#include "custom_status_screen.h"
#include "touch_handler.h"
#include "events/swipe_gesture_event.h"
#include "display_settings.h"
#include "widgets/brightness_screen.h"
#include "widgets/system_settings_widget.h"

#if CONFIG_DONGLE_SCREEN_OUTPUT_ACTIVE
#include "widgets/output_status.h"
static struct zmk_widget_output_status output_status_widget;
#endif

#if CONFIG_DONGLE_SCREEN_LAYER_ACTIVE
#include "widgets/layer_status.h"
static struct zmk_widget_layer_status layer_status_widget;
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

#if CONFIG_DONGLE_SCREEN_BONGO_CAT_ACTIVE
#include "widgets/bongo_cat.h"
static struct zmk_widget_bongo_cat main_bongo_cat_widget;
#else
#include "widgets/bongo_cat.h"
static struct zmk_widget_bongo_cat bongo_screen_widget;
#endif

static struct zmk_widget_brightness_screen brightness_widget;
static struct zmk_widget_system_settings system_settings_widget;

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <lvgl.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

/* ================================================================== */
/* Screen state                                                       */
/* ================================================================== */

enum dongle_screen_id {
    SCREEN_MAIN = 0,
#if !CONFIG_DONGLE_SCREEN_BONGO_CAT_ACTIVE
    SCREEN_BONGO,
#endif
    SCREEN_BRIGHTNESS,
    SCREEN_SYSTEM_SETTINGS,
};

static lv_obj_t *screen_obj;
static lv_timer_t *swipe_timer;
static enum dongle_screen_id current_screen = SCREEN_MAIN;

static volatile enum swipe_direction pending_swipe = SWIPE_DIRECTION_NONE;
static bool transition_in_progress = false;
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

static void prepare_screen(lv_color_t bg)
{
    lv_obj_clean(screen_obj);
    lv_obj_set_style_bg_color(screen_obj, bg, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(screen_obj, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_add_style(screen_obj, &global_style, LV_PART_MAIN);
}

static void hide_current_screen_widgets(void)
{
    switch (current_screen) {
    case SCREEN_BRIGHTNESS:
        zmk_widget_brightness_screen_hide(&brightness_widget);
        break;

    case SCREEN_SYSTEM_SETTINGS:
        zmk_widget_system_settings_hide(&system_settings_widget);
        break;

    default:
        break;
    }
}

/* ================================================================== */
/* Main screen                                                        */
/* ================================================================== */

static void create_main_screen_widgets(void)
{
#if CONFIG_DONGLE_SCREEN_BONGO_CAT_ACTIVE

#if CONFIG_DONGLE_SCREEN_OUTPUT_ACTIVE
    zmk_widget_output_status_init(&output_status_widget, screen_obj);
    lv_obj_align(zmk_widget_output_status_obj(&output_status_widget),
                 LV_ALIGN_TOP_MID, 0, 10);
#endif

#if CONFIG_DONGLE_SCREEN_BATTERY_ACTIVE
    zmk_widget_dongle_battery_status_init(&dongle_battery_status_widget, screen_obj);
    lv_obj_align(zmk_widget_dongle_battery_status_obj(&dongle_battery_status_widget),
                 LV_ALIGN_TOP_MID, 0, 10);
#endif

#if CONFIG_DONGLE_SCREEN_WPM_ACTIVE
    zmk_widget_wpm_status_init(&wpm_status_widget, screen_obj);
    lv_obj_align(zmk_widget_wpm_status_obj(&wpm_status_widget),
                 LV_ALIGN_TOP_LEFT, 20, 20);
#endif

#if CONFIG_DONGLE_SCREEN_LAYER_ACTIVE
    zmk_widget_layer_status_init(&layer_status_widget, screen_obj);
    lv_obj_align(zmk_widget_layer_status_obj(&layer_status_widget),
                 LV_ALIGN_TOP_MID, 0, 50);
#endif

#if CONFIG_DONGLE_SCREEN_MODIFIER_ACTIVE
    zmk_widget_mod_status_init(&mod_widget, screen_obj);
    lv_obj_align(zmk_widget_mod_status_obj(&mod_widget),
                 LV_ALIGN_TOP_MID, 0, 85);
#endif

    zmk_widget_bongo_cat_init(&main_bongo_cat_widget, screen_obj);
    lv_obj_align(zmk_widget_bongo_cat_obj(&main_bongo_cat_widget),
                 LV_ALIGN_BOTTOM_MID, 0, 0);

#else

#if CONFIG_DONGLE_SCREEN_OUTPUT_ACTIVE
    zmk_widget_output_status_init(&output_status_widget, screen_obj);
    lv_obj_align(zmk_widget_output_status_obj(&output_status_widget),
                 LV_ALIGN_TOP_MID, 0, 10);
#endif

#if CONFIG_DONGLE_SCREEN_BATTERY_ACTIVE
    zmk_widget_dongle_battery_status_init(&dongle_battery_status_widget, screen_obj);
    lv_obj_align(zmk_widget_dongle_battery_status_obj(&dongle_battery_status_widget),
                 LV_ALIGN_BOTTOM_MID, 0, 0);
#endif

#if CONFIG_DONGLE_SCREEN_WPM_ACTIVE
    zmk_widget_wpm_status_init(&wpm_status_widget, screen_obj);
    lv_obj_align(zmk_widget_wpm_status_obj(&wpm_status_widget),
                 LV_ALIGN_TOP_LEFT, 20, 20);
#endif

#if CONFIG_DONGLE_SCREEN_LAYER_ACTIVE
    zmk_widget_layer_status_init(&layer_status_widget, screen_obj);
    lv_obj_align(zmk_widget_layer_status_obj(&layer_status_widget),
                 LV_ALIGN_CENTER, 0, 0);
#endif

#if CONFIG_DONGLE_SCREEN_MODIFIER_ACTIVE
    zmk_widget_mod_status_init(&mod_widget, screen_obj);
    lv_obj_align(zmk_widget_mod_status_obj(&mod_widget),
                 LV_ALIGN_CENTER, 0, 35);
#endif

#endif
}

static void show_main_screen(void)
{
    hide_current_screen_widgets();

    prepare_screen(lv_color_hex(0x000000));
    create_main_screen_widgets();
    current_screen = SCREEN_MAIN;
}

/* ================================================================== */
/* Bongo-only screen (only when main screen does NOT contain bongo)   */
/* ================================================================== */

#if !CONFIG_DONGLE_SCREEN_BONGO_CAT_ACTIVE
static void show_bongo_screen(void)
{
    hide_current_screen_widgets();

    prepare_screen(lv_color_hex(0x000000));

    zmk_widget_bongo_cat_init(&bongo_screen_widget, screen_obj);
    lv_obj_align(zmk_widget_bongo_cat_obj(&bongo_screen_widget),
                 LV_ALIGN_CENTER, 0, 0);

    current_screen = SCREEN_BONGO;
}
#endif

/* ================================================================== */
/* Brightness screen                                                  */
/* ================================================================== */

static void show_brightness_screen(void)
{
    if (current_screen == SCREEN_BRIGHTNESS) {
        return;
    }

    hide_current_screen_widgets();

    prepare_screen(lv_color_hex(0x0A0A0A));
    ensure_lvgl_indev_registered();

    zmk_widget_brightness_screen_init(&brightness_widget, screen_obj);
    zmk_widget_brightness_screen_show(&brightness_widget);

    current_screen = SCREEN_BRIGHTNESS;
}

/* ================================================================== */
/* System settings screen                                             */
/* ================================================================== */

static void show_system_settings_screen(void)
{
    if (current_screen == SCREEN_SYSTEM_SETTINGS) {
        return;
    }

    hide_current_screen_widgets();

    prepare_screen(lv_color_hex(0x0A0A0A));
    ensure_lvgl_indev_registered();

    zmk_widget_system_settings_init(&system_settings_widget, screen_obj);
    zmk_widget_system_settings_show(&system_settings_widget);

    current_screen = SCREEN_SYSTEM_SETTINGS;
}

/* ================================================================== */
/* Screen routing                                                     */
/* ================================================================== */

static void route_swipe(enum swipe_direction dir)
{
    switch (current_screen) {
    case SCREEN_MAIN:
        if (dir == SWIPE_DIRECTION_LEFT) {
            show_brightness_screen();
            return;
        }
        if (dir == SWIPE_DIRECTION_RIGHT) {
            show_system_settings_screen();
            return;
        }
#if !CONFIG_DONGLE_SCREEN_BONGO_CAT_ACTIVE
        if (dir == SWIPE_DIRECTION_UP || dir == SWIPE_DIRECTION_DOWN) {
            show_bongo_screen();
            return;
        }
#endif
        break;

#if !CONFIG_DONGLE_SCREEN_BONGO_CAT_ACTIVE
    case SCREEN_BONGO:
        if (dir == SWIPE_DIRECTION_UP || dir == SWIPE_DIRECTION_DOWN ||
            dir == SWIPE_DIRECTION_LEFT || dir == SWIPE_DIRECTION_RIGHT ||
            dir == SWIPE_DIRECTION_DOUBLE_TAP) {
            show_main_screen();
            return;
        }
        break;
#endif

    case SCREEN_BRIGHTNESS:
        if (dir == SWIPE_DIRECTION_RIGHT || dir == SWIPE_DIRECTION_DOUBLE_TAP) {
            show_main_screen();
            return;
        }
        break;

    case SCREEN_SYSTEM_SETTINGS:
        if (dir == SWIPE_DIRECTION_LEFT || dir == SWIPE_DIRECTION_DOUBLE_TAP) {
            show_main_screen();
            return;
        }
        break;

    default:
        break;
    }
}

/* ================================================================== */
/* Swipe event handling                                               */
/* ================================================================== */

static int swipe_gesture_event_handler(const zmk_event_t *eh)
{
    const struct zmk_swipe_gesture_event *ev = as_zmk_swipe_gesture_event(eh);
    if (!ev) {
        return -ENOTSUP;
    }

    /*
     * Prospector と同じ方針:
     * ここでは LVGL API を触らず、方向を pending に積むだけ。
     */
    pending_swipe = ev->direction;
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(swipe_gesture_screen, swipe_gesture_event_handler);
ZMK_SUBSCRIPTION(swipe_gesture_screen, zmk_swipe_gesture_event);

/* ================================================================== */
/* LVGL timer                                                         */
/* ================================================================== */

static void swipe_process_timer_cb(lv_timer_t *timer)
{
    ARG_UNUSED(timer);

    enum swipe_direction dir = pending_swipe;
    if (dir == SWIPE_DIRECTION_NONE) {
        return;
    }

    pending_swipe = SWIPE_DIRECTION_NONE;

    if (ui_interaction_active) {
        LOG_DBG("Swipe ignored: UI interaction active");
        return;
    }

    if (transition_in_progress) {
        LOG_DBG("Swipe ignored: transition already in progress");
        return;
    }

    transition_in_progress = true;
    route_swipe(dir);
    transition_in_progress = false;
}

/* ================================================================== */
/* Public entry                                                       */
/* ================================================================== */

lv_obj_t *zmk_display_status_screen(void)
{
    display_settings_init();

    lv_style_init(&global_style);
    lv_style_set_text_color(&global_style, lv_color_white());
    lv_style_set_text_letter_space(&global_style, 1);
    lv_style_set_text_line_space(&global_style, 1);

    screen_obj = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen_obj, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(screen_obj, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_add_style(screen_obj, &global_style, LV_PART_MAIN);

    ensure_lvgl_indev_registered();

    create_main_screen_widgets();
    current_screen = SCREEN_MAIN;

    if (!swipe_timer) {
        swipe_timer = lv_timer_create(swipe_process_timer_cb, 16, NULL);
    }

    return screen_obj;
}

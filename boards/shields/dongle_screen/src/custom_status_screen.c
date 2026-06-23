/*
 * Copyright (c) 2024 The ZMK Contributors
 * SPDX-License-Identifier: MIT
 *
 * カスタムステータス画面
 *
 * スクリーン構成:
 *   screens[0] = メイン（ステータス表示）
 *   screens[1] = ボンゴキャット
 *   screens[2] = 輝度設定（スライダー）   ← 追加
 *   screens[3] = クイックアクション        ← 追加
 *                (Bootloader / System Reset)
 *
 * スワイプ操作:
 *   左スワイプ → 次の画面（ループ）
 *   右スワイプ → 前の画面（ループ）
 *   ダブルタップ → メイン画面へ戻る
 */

#include "custom_status_screen.h"
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
static struct zmk_widget_bongo_cat bongo_cat_widget;
#endif

static struct zmk_widget_brightness_screen    brightness_widget;
static struct zmk_widget_system_settings      system_settings_widget;

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

/* ================================================================== */
/* スクリーン管理                                                      */
/* ================================================================== */

#define SCREEN_COUNT 4
/*
 * 0: Main
 * 1: BongoCat
 * 2: Brightness
 * 3: QuickActions (Bootloader/Reset)
 */

static lv_obj_t *screens[SCREEN_COUNT];
static int current_screen_index = 0;

/* ================================================================== */
/* スワイプイベントリスナー                                            */
/* ================================================================== */

static int swipe_gesture_event_handler(const zmk_event_t *eh)
{
    const struct zmk_swipe_gesture_event *ev = as_zmk_swipe_gesture_event(eh);
    if (!ev) return -ENOTSUP;

    int next = current_screen_index;

    switch (ev->direction) {
    case SWIPE_DIRECTION_LEFT:
        next = (current_screen_index + 1) % SCREEN_COUNT;
        break;
    case SWIPE_DIRECTION_RIGHT:
        next = (current_screen_index - 1 + SCREEN_COUNT) % SCREEN_COUNT;
        break;
    case SWIPE_DIRECTION_DOUBLE_TAP:
        next = 0;
        break;
    default:
        return ZMK_EV_EVENT_BUBBLE;
    }

    if (next == current_screen_index) return ZMK_EV_EVENT_BUBBLE;

    LOG_INF("Screen: %d -> %d", current_screen_index, next);
    current_screen_index = next;
    lv_scr_load(screens[next]);

    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(swipe_gesture_screen, swipe_gesture_event_handler);
ZMK_SUBSCRIPTION(swipe_gesture_screen, zmk_swipe_gesture_event);

/* ================================================================== */
/* スタイル                                                            */
/* ================================================================== */

lv_style_t global_style;

static void init_screen_base(lv_obj_t *screen)
{
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(screen, 255, LV_PART_MAIN);
    lv_obj_add_style(screen, &global_style, LV_PART_MAIN);
}

/* ================================================================== */
/* screens[0]: メイン                                                  */
/* ================================================================== */

static lv_obj_t *create_main_screen(void)
{
    lv_obj_t *screen = lv_obj_create(NULL);
    init_screen_base(screen);

#if CONFIG_DONGLE_SCREEN_OUTPUT_ACTIVE
    zmk_widget_output_status_init(&output_status_widget, screen);
    lv_obj_align(zmk_widget_output_status_obj(&output_status_widget),
                 LV_ALIGN_TOP_MID, 0, 10);
#endif

#if CONFIG_DONGLE_SCREEN_BATTERY_ACTIVE
    zmk_widget_dongle_battery_status_init(&dongle_battery_status_widget, screen);
    lv_obj_align(zmk_widget_dongle_battery_status_obj(&dongle_battery_status_widget),
                 LV_ALIGN_BOTTOM_MID, 0, 0);
#endif

#if CONFIG_DONGLE_SCREEN_WPM_ACTIVE
    zmk_widget_wpm_status_init(&wpm_status_widget, screen);
    lv_obj_align(zmk_widget_wpm_status_obj(&wpm_status_widget),
                 LV_ALIGN_TOP_LEFT, 20, 20);
#endif

#if CONFIG_DONGLE_SCREEN_LAYER_ACTIVE
    zmk_widget_layer_status_init(&layer_status_widget, screen);
    lv_obj_align(zmk_widget_layer_status_obj(&layer_status_widget),
                 LV_ALIGN_CENTER, 0, 0);
#endif

#if CONFIG_DONGLE_SCREEN_MODIFIER_ACTIVE
    zmk_widget_mod_status_init(&mod_widget, screen);
    lv_obj_align(zmk_widget_mod_status_obj(&mod_widget),
                 LV_ALIGN_CENTER, 0, 35);
#endif

    return screen;
}

/* ================================================================== */
/* screens[1]: ボンゴキャット                                          */
/* ================================================================== */

static lv_obj_t *create_bongo_screen(void)
{
    lv_obj_t *screen = lv_obj_create(NULL);
    init_screen_base(screen);

#if CONFIG_DONGLE_SCREEN_BONGO_CAT_ACTIVE
    zmk_widget_bongo_cat_init(&bongo_cat_widget, screen);
    lv_obj_align(zmk_widget_bongo_cat_obj(&bongo_cat_widget),
                 LV_ALIGN_BOTTOM_MID, 0, 0);
#endif

    return screen;
}

/* ================================================================== */
/* screens[2]: 輝度設定                                               */
/* ================================================================== */

static lv_obj_t *create_brightness_screen(void)
{
    lv_obj_t *screen = lv_obj_create(NULL);
    init_screen_base(screen);

    zmk_widget_brightness_screen_init(&brightness_widget, screen);
    zmk_widget_brightness_screen_show(&brightness_widget);

    return screen;
}

/* ================================================================== */
/* screens[3]: クイックアクション (Bootloader / Reset)                */
/* ================================================================== */

static lv_obj_t *create_system_settings_screen(void)
{
    lv_obj_t *screen = lv_obj_create(NULL);
    init_screen_base(screen);

    zmk_widget_system_settings_init(&system_settings_widget, screen);
    zmk_widget_system_settings_show(&system_settings_widget);

    return screen;
}

/* ================================================================== */
/* エントリーポイント                                                  */
/* ================================================================== */

lv_obj_t *zmk_display_status_screen(void)
{
    /* NVS 輝度ロード（スクリーン生成前に呼ぶ） */
    display_settings_init();

    lv_style_init(&global_style);
    lv_style_set_text_color(&global_style, lv_color_white());
    lv_style_set_text_letter_space(&global_style, 1);
    lv_style_set_text_line_space(&global_style, 1);

    screens[0] = create_main_screen();
    screens[1] = create_bongo_screen();
    screens[2] = create_brightness_screen();
    screens[3] = create_system_settings_screen();

    return screens[0];
}

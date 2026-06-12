/*
 * Copyright (c) 2024 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */
#include "custom_status_screen.h"
#include "swipe_gesture_event.h"

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

#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

/* ── スクリーン管理 ─────────────────────────────────────────── */

#define SCREEN_COUNT 2  /* スクリーンの総数 */

static lv_obj_t *screens[SCREEN_COUNT];
static int current_screen_index = 0;

/* スクリーンをインデックス指定で切り替える */
static void switch_to_screen(int index) {
    if (index < 0 || index >= SCREEN_COUNT) {
        return;
    }
    if (screens[index] == NULL) {
        LOG_WRN("Screen %d is not initialized", index);
        return;
    }
    current_screen_index = index;
    lv_scr_load(screens[index]);
    LOG_DBG("Switched to screen %d", index);
}

/* 次のスクリーンへ（右端で折り返し） */
static void switch_to_next_screen(void) {
    int next = (current_screen_index + 1) % SCREEN_COUNT;
    switch_to_screen(next);
}

/* 前のスクリーンへ（左端で折り返し） */
static void switch_to_prev_screen(void) {
    int prev = (current_screen_index - 1 + SCREEN_COUNT) % SCREEN_COUNT;
    switch_to_screen(prev);
}

/* ── スワイプイベントリスナー ───────────────────────────────── */

static int swipe_gesture_event_handler(const zmk_event_t *eh) {
    const struct zmk_swipe_gesture_event *ev = as_zmk_swipe_gesture_event(eh);
    if (ev == NULL) {
        return -ENOTSUP;
    }

    LOG_DBG("Swipe gesture received: direction=%d", ev->direction);

    switch (ev->direction) {
    case SWIPE_DIRECTION_LEFT:
        /* 左スワイプ → 次のスクリーンへ */
        switch_to_next_screen();
        break;
    case SWIPE_DIRECTION_RIGHT:
        /* 右スワイプ → 前のスクリーンへ */
        switch_to_prev_screen();
        break;
    case SWIPE_DIRECTION_DOUBLE_TAP:
        /* ダブルタップ → 先頭スクリーンへ戻る */
        switch_to_screen(0);
        break;
    default:
        /* UP / DOWN / NONE は現状無視 */
        break;
    }

    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(swipe_gesture_screen, swipe_gesture_event_handler);
ZMK_SUBSCRIPTION(swipe_gesture_screen, zmk_swipe_gesture_event);

/* ── スクリーン生成ヘルパー ─────────────────────────────────── */

lv_style_t global_style;

/* スクリーン 0：メインステータス（元の画面） */
static lv_obj_t *create_main_screen(void) {
    lv_obj_t *screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(screen, 255, LV_PART_MAIN);
    lv_obj_add_style(screen, &global_style, LV_PART_MAIN);

#if CONFIG_DONGLE_SCREEN_OUTPUT_ACTIVE
    zmk_widget_output_status_init(&output_status_widget, screen);
    lv_obj_align(zmk_widget_output_status_obj(&output_status_widget), LV_ALIGN_TOP_MID, 0, 10);
#endif
#if CONFIG_DONGLE_SCREEN_BATTERY_ACTIVE
    zmk_widget_dongle_battery_status_init(&dongle_battery_status_widget, screen);
    lv_obj_align(zmk_widget_dongle_battery_status_obj(&dongle_battery_status_widget),
                 LV_ALIGN_BOTTOM_MID, 0, 0);
#endif
#if CONFIG_DONGLE_SCREEN_WPM_ACTIVE
    zmk_widget_wpm_status_init(&wpm_status_widget, screen);
    lv_obj_align(zmk_widget_wpm_status_obj(&wpm_status_widget), LV_ALIGN_TOP_LEFT, 20, 20);
#endif
#if CONFIG_DONGLE_SCREEN_LAYER_ACTIVE
    zmk_widget_layer_status_init(&layer_status_widget, screen);
    lv_obj_align(zmk_widget_layer_status_obj(&layer_status_widget), LV_ALIGN_CENTER, 0, 0);
#endif
#if CONFIG_DONGLE_SCREEN_MODIFIER_ACTIVE
    zmk_widget_mod_status_init(&mod_widget, screen);
    lv_obj_align(zmk_widget_mod_status_obj(&mod_widget), LV_ALIGN_CENTER, 0, 35);
#endif

    return screen;
}

/* スクリーン 1：サブ画面（必要に応じてカスタマイズ） */
static lv_obj_t *create_sub_screen(void) {
    lv_obj_t *screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(screen, 255, LV_PART_MAIN);
    lv_obj_add_style(screen, &global_style, LV_PART_MAIN);

    /* TODO: サブ画面のウィジェットをここに追加する */
    lv_obj_t *label = lv_label_create(screen);
    lv_label_set_text(label, "Screen 2");
    lv_obj_align(label, LV_ALIGN_CENTER, 0, 0);

    return screen;
}

/* ── エントリーポイント ─────────────────────────────────────── */

lv_obj_t *zmk_display_status_screen(void) {
    lv_style_init(&global_style);
    lv_style_set_text_color(&global_style, lv_color_white());
    lv_style_set_text_letter_space(&global_style, 1);
    lv_style_set_text_line_space(&global_style, 1);

    screens[0] = create_main_screen();
    screens[1] = create_sub_screen();

    /* ZMK は返された画面を最初にロードするので、screen[0] を返す */
    return screens[0];
}

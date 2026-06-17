/*
#include "custom_status_screen.h"
#include "events/swipe_gesture_event.h"

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

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

// ── スクリーン管理 ───────────────────────────────────────────

#define SCREEN_COUNT 2

static lv_obj_t *screens[SCREEN_COUNT];
static int current_screen_index = 0;

// ── k_work によるスレッドセーフなスクリーン切り替え ─────────
// ZMKイベントハンドラはZMKのシステムスレッドで動く。
// LVGLはディスプレイスレッド専用のため、直接 lv_scr_load() を
// 呼ぶと競合する。k_work でディスプレイスレッドのワークキューに
// 投入し、ZMK display タスクが next_screen_index を見て切り替える。
// ───────────────────────────────────────────────────────────

static int next_screen_index = -1;  // -1 = 切り替えなし
static struct k_work screen_switch_work;

// ディスプレイスレッドのワークキュー（ZMKが用意したもの）を使う
extern struct k_work_q k_sys_work_q;

static void screen_switch_work_handler(struct k_work *work) {
    int index = next_screen_index;
    if (index < 0 || index >= SCREEN_COUNT || screens[index] == NULL) {
        LOG_WRN("Invalid screen index: %d", index);
        return;
    }
    current_screen_index = index;
    lv_scr_load(screens[index]);
    LOG_DBG("Screen switched to %d", index);
}

static void request_screen_switch(int index) {
    if (index < 0 || index >= SCREEN_COUNT) {
        return;
    }
    next_screen_index = index;
    k_work_submit(&screen_switch_work);
    LOG_DBG("Screen switch requested: %d", index);
}

static void switch_to_next_screen(void) {
    request_screen_switch((current_screen_index + 1) % SCREEN_COUNT);
}

static void switch_to_prev_screen(void) {
    request_screen_switch((current_screen_index - 1 + SCREEN_COUNT) % SCREEN_COUNT);
}

// ── スワイプイベントリスナー ─────────────────────────────────

static int swipe_gesture_event_handler(const zmk_event_t *eh) {
    const struct zmk_swipe_gesture_event *ev = as_zmk_swipe_gesture_event(eh);
    if (ev == NULL) {
        return -ENOTSUP;
    }
    LOG_DBG("Swipe event: direction=%d", ev->direction);
    switch (ev->direction) {
    case SWIPE_DIRECTION_LEFT:
        switch_to_next_screen();
        break;
    case SWIPE_DIRECTION_RIGHT:
        switch_to_prev_screen();
        break;
    case SWIPE_DIRECTION_DOUBLE_TAP:
        request_screen_switch(0);
        break;
    default:
        break;
    }
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(swipe_gesture_screen, swipe_gesture_event_handler);
ZMK_SUBSCRIPTION(swipe_gesture_screen, zmk_swipe_gesture_event);

// ── スタイル ─────────────────────────────────────────────────

lv_style_t global_style;

static void init_screen_base(lv_obj_t *screen) {
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(screen, 255, LV_PART_MAIN);
    lv_obj_add_style(screen, &global_style, LV_PART_MAIN);
}

// ── スクリーン 0：メインステータス ──────────────────────────

static lv_obj_t *create_main_screen(void) {
    lv_obj_t *screen = lv_obj_create(NULL);
    init_screen_base(screen);

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

// ── スクリーン 1：ボンゴキャット専用 ────────────────────────

static lv_obj_t *create_bongo_screen(void) {
    lv_obj_t *screen = lv_obj_create(NULL);
    init_screen_base(screen);

#if CONFIG_DONGLE_SCREEN_BONGO_CAT_ACTIVE
    zmk_widget_bongo_cat_init(&bongo_cat_widget, screen);
    lv_obj_align(zmk_widget_bongo_cat_obj(&bongo_cat_widget), LV_ALIGN_CENTER, 0, 0);
#endif

    return screen;
}

// ── エントリーポイント ───────────────────────────────────────

lv_obj_t *zmk_display_status_screen(void) {
    k_work_init(&screen_switch_work, screen_switch_work_handler);

    lv_style_init(&global_style);
    // lv_style_set_text_font(&global_style, &lv_font_unscii_8); // ToDo: Font is not recognized
    lv_style_set_text_color(&global_style, lv_color_white());
    lv_style_set_text_letter_space(&global_style, 1);
    lv_style_set_text_line_space(&global_style, 1);

    screens[0] = create_main_screen();
    screens[1] = create_bongo_screen();

    return screens[0];
}
*/

/*
 * Copyright (c) 2024 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include "custom_status_screen.h"
#include "events/swipe_gesture_event.h"

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

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

/* ── スクリーン管理 ─────────────────────────────────────────── */

#define SCREEN_COUNT 2

static lv_obj_t *screens[SCREEN_COUNT];
static volatile int current_screen_index = 0;
static volatile int pending_screen_index = -1;  /* -1 = 切り替えなし */

/* ── lv_timer によるスクリーン切り替え ──────────────────────
 *
 * lv_timer はLVGLのタスクループ内（ディスプレイスレッド）で
 * 呼ばれるため、lv_scr_load() を安全に呼べる。
 * ZMKイベントハンドラは pending_screen_index に書くだけ。
 * ─────────────────────────────────────────────────────────── */

static void screen_switch_timer_cb(lv_timer_t *timer) {
    int idx = pending_screen_index;
    if (idx < 0 || idx == current_screen_index) {
        return;
    }
    if (idx >= SCREEN_COUNT || screens[idx] == NULL) {
        pending_screen_index = -1;
        return;
    }
    pending_screen_index = -1;
    current_screen_index = idx;
    lv_scr_load(screens[idx]);
}

static void request_screen_switch(int index) {
    if (index >= 0 && index < SCREEN_COUNT) {
        pending_screen_index = index;
    }
}

static void switch_to_next_screen(void) {
    request_screen_switch((current_screen_index + 1) % SCREEN_COUNT);
}

static void switch_to_prev_screen(void) {
    request_screen_switch((current_screen_index - 1 + SCREEN_COUNT) % SCREEN_COUNT);
}

/* ── スワイプイベントリスナー ───────────────────────────────── */

static int swipe_gesture_event_handler(const zmk_event_t *eh) {
    const struct zmk_swipe_gesture_event *ev = as_zmk_swipe_gesture_event(eh);
    if (ev == NULL) {
        return -ENOTSUP;
    }
    switch (ev->direction) {
    case SWIPE_DIRECTION_LEFT:
        switch_to_next_screen();
        break;
    case SWIPE_DIRECTION_RIGHT:
        switch_to_prev_screen();
        break;
    case SWIPE_DIRECTION_DOUBLE_TAP:
        request_screen_switch(0);
        break;
    default:
        break;
    }
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(swipe_gesture_screen, swipe_gesture_event_handler);
ZMK_SUBSCRIPTION(swipe_gesture_screen, zmk_swipe_gesture_event);

/* ── スタイル ───────────────────────────────────────────────── */

lv_style_t global_style;

static void init_screen_base(lv_obj_t *screen) {
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(screen, 255, LV_PART_MAIN);
    lv_obj_add_style(screen, &global_style, LV_PART_MAIN);
}

/* ── スクリーン 0：メインステータス ────────────────────────── */

static lv_obj_t *create_main_screen(void) {
    lv_obj_t *screen = lv_obj_create(NULL);
    init_screen_base(screen);

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

/* ── スクリーン 1：ボンゴキャット専用（デバッグ用に赤背景） ── */

static lv_obj_t *create_bongo_screen(void) {
    lv_obj_t *screen = lv_obj_create(NULL);
    /* デバッグ：遷移確認のため赤背景。動作確認後に 0x000000 に戻す */
    lv_obj_set_style_bg_color(screen, lv_color_hex(0xFF0000), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(screen, 255, LV_PART_MAIN);
    lv_obj_add_style(screen, &global_style, LV_PART_MAIN);

#if CONFIG_DONGLE_SCREEN_BONGO_CAT_ACTIVE
    zmk_widget_bongo_cat_init(&bongo_cat_widget, screen);
    lv_obj_align(zmk_widget_bongo_cat_obj(&bongo_cat_widget), LV_ALIGN_CENTER, 0, 0);
#endif

    return screen;
}

/* ── エントリーポイント ─────────────────────────────────────── */

lv_obj_t *zmk_display_status_screen(void) {
    lv_style_init(&global_style);
    // lv_style_set_text_font(&global_style, &lv_font_unscii_8); // ToDo: Font is not recognized
    lv_style_set_text_color(&global_style, lv_color_white());
    lv_style_set_text_letter_space(&global_style, 1);
    lv_style_set_text_line_space(&global_style, 1);

    screens[0] = create_main_screen();
    screens[1] = create_bongo_screen();

    /* 100ms ごとに pending_screen_index を確認してスクリーンを切り替える
     * lv_timer はLVGLタスクループ内で実行されるため lv_scr_load() が安全 */
    lv_timer_create(screen_switch_timer_cb, 100, NULL);

    return screens[0];
}

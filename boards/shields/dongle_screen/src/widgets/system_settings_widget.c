/*
 * Copyright (c) 2024 The ZMK Contributors
 * SPDX-License-Identifier: MIT
 *
 * System Settings Widget (LVGL8 / ZMK 3.5)
 *
 * Prospector (LVGL9/ZMK4) から移植した設計方針:
 *
 * [問題] リセット画面からスワイプしようとするとボタンが誤作動する
 *
 *   原因:
 *     スワイプのタッチ DOWN がボタン上に落ちると LVGL が LV_EVENT_PRESSED を
 *     発火させ、その後 UP で LV_EVENT_CLICKED が発火してリセット/ブートローダーが
 *     実行される。
 *
 *   Prospector の解決策:
 *     ① LV_EVENT_PRESSED 時に ui_interaction_active = true → スワイプをブロック
 *     ② touch_handler.c 側でタッチ DOWN 時点で ui_interaction_active を確認し、
 *        true なら swipe_already_raised = true にしてスワイプを抑止
 *     ③ LV_EVENT_CLICKED 時に touch_handler_is_swiping() でスワイプ中か確認し、
 *        スワイプ中ならボタンアクションを実行しない
 *     ④ LV_EVENT_RELEASED / PRESS_LOST で ui_interaction_active = false に戻す
 *
 * [設計] hitbox 方式
 *   見た目の大きなボタン (visual_btn) と実際の当たり判定 (hitbox) を分離。
 *   visual_btn は LV_OBJ_FLAG_CLICKABLE を持たず、押下状態変化を起こさない。
 *   hitbox は visual_btn の中央に配置した小さな透明オブジェクト。
 *   これによりボタン端へのスワイプ開始でクリックが発生しにくくなる。
 *
 * [枠線フィードバック]
 *   hitbox の PRESSED/RELEASED/PRESS_LOST 時に、対応する visual_btn 自身へ
 *   手動で LV_STATE_PRESSED を付け外しすることで、押下したボタンだけ
 *   枠線色が変わるようにする（visual_btn は CLICKABLE を持たないため
 *   LVGL 標準の自動状態遷移は発生しない）。
 *
 * [LVGL8 対応]
 *   ui_interaction_active は custom_status_screen.c で定義済みの
 *   volatile bool を extern 参照。
 *   touch_handler_is_swiping() は touch_handler.h で宣言済み。
 */

#include "system_settings_widget.h"
#include "../custom_status_screen.h"
#include "../touch_handler.h"

#include <zephyr/logging/log.h>
#include <zephyr/sys/reboot.h>
#include <errno.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

/* ================================================================== */
/* Tunables                                                           */
/* ================================================================== */

/* 見た目のボタンサイズ */
#define ACTION_BTN_W 220
#define ACTION_BTN_H 60

/*
 * 実際の当たり判定サイズ。
 * ボタン中央の矩形に絞ることでスワイプ開始点がボタン端に掛かっても
 * クリックが発生しにくくなる。
 */
#define ACTION_HIT_W 60
#define ACTION_HIT_H 60

/* 枠線の共通色設定 */
#if CONFIG_DONGLE_SCREEN_BUTTONS_MONO
#define BORDER_COLOR_NORMAL  0xFFFFFF  /* 通常時: White */
#else
#define BORDER_COLOR_NORMAL  0x000000  /* 通常時: Black */
#endif
#define BORDER_COLOR_PRESSED 0x00FF00  /* 押下時: Green */
#define BORDER_WIDTH 2

/* ================================================================== */
/* Widget-private state                                               */
/* ================================================================== */

struct action_btn_bundle {
    lv_obj_t *visual_btn;   /* 見た目だけの大きいボタン */
    lv_obj_t *hitbox;       /* 中央の狭いクリック領域 */
};

static struct action_btn_bundle boot_bundle;
static struct action_btn_bundle reset_bundle;

/* ================================================================== */
/* Visual button helper                                               */
/* ================================================================== */

static lv_obj_t *make_visual_btn(lv_obj_t *parent,
                                 const char *text,
                                 lv_color_t bg,
                                 lv_align_t align,
                                 lv_coord_t x_off,
                                 lv_coord_t y_off)
{
    lv_obj_t *obj = lv_obj_create(parent);
    if (!obj) {
        return NULL;
    }

    lv_obj_set_size(obj, ACTION_BTN_W, ACTION_BTN_H);
    lv_obj_align(obj, align, x_off, y_off);

    /*
     * 見た目だけ: clickable は付けない。
     * タッチイベントはすべて hitbox で受け取る。
     */
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
/* Interaction state callbacks                                        */
/* ================================================================== */

/*
 * hitbox PRESSED: ui_interaction_active = true でスワイプをブロック。
 * touch_handler.c の touch_input_callback() は INPUT_BTN_TOUCH の touch_started
 * 時点で ui_interaction_active を確認し、true なら swipe_already_raised=true に
 * することでスワイプを抑止する。
 *
 * 併せて、対応する visual_btn（hitbox の親）にのみ LV_STATE_PRESSED を付与し、
 * 押下したボタンだけ枠線色が変わるようにする。
 */
static void ui_press_start_cb(lv_event_t *e)
{
    lv_obj_t *hitbox = lv_event_get_target(e);
    lv_obj_t *visual_btn = lv_obj_get_parent(hitbox);
    if (visual_btn) {
        lv_obj_add_state(visual_btn, LV_STATE_PRESSED);
    }

    ui_interaction_active = true;
    LOG_DBG("ui_interaction_active = true (button pressed)");
}

/*
 * hitbox RELEASED / PRESS_LOST: ui_interaction_active = false に戻す。
 * PRESS_LOST はスワイプ等でフォーカスが外れた場合に発生する。
 * 併せて対応する visual_btn の LV_STATE_PRESSED を解除する。
 */
static void ui_press_end_cb(lv_event_t *e)
{
    lv_obj_t *hitbox = lv_event_get_target(e);
    lv_obj_t *visual_btn = lv_obj_get_parent(hitbox);
    if (visual_btn) {
        lv_obj_clear_state(visual_btn, LV_STATE_PRESSED);
    }

    ui_interaction_active = false;
    LOG_DBG("ui_interaction_active = false (button released/lost)");
}

/* ================================================================== */
/* Action callbacks                                                   */
/* ================================================================== */

static void bootloader_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }

    /*
     * スワイプ中はボタンアクションを実行しない。
     * touch_handler_is_swiping() は swipe_state.in_progress を返す。
     * スワイプで PRESS_LOST が来るより先に CLICKED が来た場合の保険。
     */
    if (touch_handler_is_swiping()) {
        LOG_DBG("Bootloader ignored: swipe in progress");
        ui_interaction_active = false;
        return;
    }

    ui_interaction_active = false;
    LOG_INF("Enter UF2 bootloader (sys_reboot 0x57)");
    sys_reboot(0x57);
}

static void reset_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }

    if (touch_handler_is_swiping()) {
        LOG_DBG("Reset ignored: swipe in progress");
        ui_interaction_active = false;
        return;
    }

    ui_interaction_active = false;
    LOG_INF("System warm reset");
    sys_reboot(SYS_REBOOT_WARM);
}

/* ================================================================== */
/* Hitbox helper                                                      */
/* ================================================================== */

/*
 * visual_btn の子として中央に小さい透明クリック領域を配置する。
 *
 * LV_OBJ_FLAG_PRESS_LOCK を clear しておくことで、hitbox 外にタッチが
 * 移動してもイベントが hitbox にロックされず、親に抜けていく。
 * これによりスワイプ中に CLICK が発生しにくくなる。
 */
static lv_obj_t *make_center_hitbox(lv_obj_t *parent_visual_btn,
                                    lv_event_cb_t clicked_cb)
{
    lv_obj_t *hit = lv_obj_create(parent_visual_btn);
    if (!hit) {
        return NULL;
    }

    lv_obj_set_size(hit, ACTION_HIT_W, ACTION_HIT_H);
    lv_obj_center(hit);

    lv_obj_add_flag(hit, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(hit, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(hit, LV_OBJ_FLAG_PRESS_LOCK);

    /* 完全透明 */
    lv_obj_set_style_bg_opa(hit, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_opa(hit, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_outline_opa(hit, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_radius(hit, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(hit, 0, LV_PART_MAIN);

    /*
     * PRESSED: ui_interaction_active = true, visual_btn に LV_STATE_PRESSED 付与
     * RELEASED / PRESS_LOST: ui_interaction_active = false, LV_STATE_PRESSED 解除
     * CLICKED: 実アクション (スワイプ中はスキップ)
     *
     * 登録順序: PRESSED → RELEASED/PRESS_LOST → CLICKED
     * LVGL はイベントを登録順に呼び出す。
     * CLICKED は RELEASED の後で発火するので、RELEASED で
     * ui_interaction_active=false にしてから CLICKED を処理させる。
     */
    lv_obj_add_event_cb(hit, ui_press_start_cb, LV_EVENT_PRESSED,    NULL);
    lv_obj_add_event_cb(hit, ui_press_end_cb,   LV_EVENT_RELEASED,   NULL);
    lv_obj_add_event_cb(hit, ui_press_end_cb,   LV_EVENT_PRESS_LOST, NULL);
    lv_obj_add_event_cb(hit, clicked_cb,         LV_EVENT_CLICKED,    NULL);

    return hit;
}

/* ================================================================== */
/* Widget init                                                        */
/* ================================================================== */

int zmk_widget_system_settings_init(struct zmk_widget_system_settings *widget,
                                    lv_obj_t *parent)
{
    if (!widget || !parent) {
        return -EINVAL;
    }

    widget->obj = parent;

    /* ---- タイトル ---- */
    widget->title_label = lv_label_create(parent);
    if (!widget->title_label) { return -ENOMEM; }
    lv_label_set_text(widget->title_label, "Quick Actions");
    lv_obj_set_style_text_color(widget->title_label,
                                lv_color_hex(0xFFFFFF), LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(widget->title_label,
                               &lv_font_montserrat_20, LV_STATE_DEFAULT);
    lv_obj_align(widget->title_label, LV_ALIGN_TOP_MID, 0, 14);

    /* ---- Bootloader visual button ---- */
    boot_bundle.visual_btn = make_visual_btn(
        parent,
        "Enter Bootloader",
        #if CONFIG_DONGLE_SCREEN_BUTTONS_MONO
        lv_color_hex(0x000000),
        #else
        lv_color_hex(0x4A90E2),
        #endif
        LV_ALIGN_CENTER, 0, -52);
    if (!boot_bundle.visual_btn) {
        return -ENOMEM;
    }

    boot_bundle.hitbox = make_center_hitbox(boot_bundle.visual_btn, bootloader_cb);
    if (!boot_bundle.hitbox) {
        return -ENOMEM;
    }

    /* ---- Reset visual button ---- */
    reset_bundle.visual_btn = make_visual_btn(
        parent,
        "System Reset",
        #if CONFIG_DONGLE_SCREEN_BUTTONS_MONO
        lv_color_hex(0x000000),
        #else
        lv_color_hex(0xE24A4A),
        #endif
        LV_ALIGN_CENTER, 0, 52);
    if (!reset_bundle.visual_btn) {
        return -ENOMEM;
    }

    reset_bundle.hitbox = make_center_hitbox(reset_bundle.visual_btn, reset_cb);
    if (!reset_bundle.hitbox) {
        return -ENOMEM;
    }

    /* API 互換のため widget フィールドへも格納 */
    widget->bootloader_btn = boot_bundle.visual_btn;
    widget->reset_btn      = reset_bundle.visual_btn;

    /* ---- ナビゲーションヒント ---- */
    widget->nav_hint = lv_label_create(parent);
    if (!widget->nav_hint) { return -ENOMEM; }
    lv_label_set_text(widget->nav_hint, "< swipe >");
    lv_obj_set_style_text_color(widget->nav_hint,
                                lv_color_hex(0x444444), LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(widget->nav_hint,
                               &lv_font_montserrat_20, LV_STATE_DEFAULT);
    lv_obj_align(widget->nav_hint, LV_ALIGN_BOTTOM_MID, 0, -10);

    return 0;
}

/* ================================================================== */
/* Show / Hide                                                        */
/* ================================================================== */

void zmk_widget_system_settings_show(struct zmk_widget_system_settings *widget)
{
    ARG_UNUSED(widget);
    /*
     * 画面表示時に interaction フラグをリセットする。
     * 前の画面でフラグが立ったままになっているケースへの保険。
     */
    ui_interaction_active = false;
    LOG_DBG("System settings shown, ui_interaction_active reset");
}

void zmk_widget_system_settings_hide(struct zmk_widget_system_settings *widget)
{
    /*
     * 画面離脱時に interaction フラグをリセットする。
     * スワイプ遷移でボタン押下中に画面が切り替わった場合の保険。
     */
    ui_interaction_active = false;

    if (!widget) {
        return;
    }

    /* ポインタをクリア (screen が lv_scr_load で切り替わっても安全) */
    widget->title_label    = NULL;
    widget->bootloader_btn = NULL;
    widget->reset_btn      = NULL;
    widget->nav_hint       = NULL;
    widget->obj            = NULL;

    boot_bundle.visual_btn  = NULL;
    boot_bundle.hitbox      = NULL;
    reset_bundle.visual_btn = NULL;
    reset_bundle.hitbox     = NULL;

    LOG_DBG("System settings hidden, ui_interaction_active reset");
}

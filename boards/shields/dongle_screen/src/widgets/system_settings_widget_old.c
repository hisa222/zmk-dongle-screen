/*
 * Copyright (c) 2024 The ZMK Contributors
 * SPDX-License-Identifier: MIT
 *
 * System Settings Widget - Bootloader / System Reset
 *
 * 設計方針:
 *   parent はスクリーン自体。中間コンテナを作らない。
 *   全 LVGL オブジェクトを直接 parent に配置する。
 *
 * [修正1] PRESS_LOCK を削除。
 *
 *   問題:
 *     LV_OBJ_FLAG_PRESS_LOCK が設定されていると、タッチがボタン上で開始した後、
 *     指がボタン外に移動してもそのボタンが入力を保持し続ける。
 *     スワイプ動作でタッチ UP した際に CLICKED / SHORT_CLICKED が発火することがある。
 *
 *   解決:
 *     PRESS_LOCK を外す。スワイプ中に指がボタン外へ出ると PRESS_LOST が発生し、
 *     その後 CLICKED / SHORT_CLICKED は発火しなくなる。
 *
 * [修正2] touch_handler_is_swiping() でボタン誤発火をガード。
 *
 *   問題:
 *     PRESS_LOCK を外してもスワイプ開始位置がボタン上の場合、
 *     ごく短いスワイプでは PRESS_LOST が発生しないケースがある。
 *     また Reset ボタンの Y 範囲が広いため、Bootloader 画面から
 *     スワイプしてきた指の UP 座標が Reset ボタン上に入り誤発火する。
 *
 *   解決:
 *     コールバック内で touch_handler_is_swiping() を参照し、
 *     スワイプ中 (in_progress=true) であれば即座に return する。
 *     touch_handler.c 側で swipe_state.in_progress が false になる
 *     (= 完全に指が離れた後) でなければボタンは反応しない。
 *
 * [修正3] ボタン配置の Y 座標を調整して2ボタン間の隙間を確保。
 *
 *   旧: Bootloader(-44), Reset(+36) → 間隔 80px、近すぎてタップ誤認識
 *   新: Bootloader(-52), Reset(+52) → 間隔 104px、タップ領域が明確に分離
 */

#include "system_settings_widget.h"
#include "../touch_handler.h"

#include <zephyr/logging/log.h>
#include <zephyr/sys/reboot.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

/* ================================================================== */
/* ボタン作成ヘルパー                                                  */
/* ================================================================== */

static lv_obj_t *make_btn(lv_obj_t *parent,
                           const char *text,
                           lv_color_t bg,
                           lv_color_t bg_pressed,
                           lv_align_t align,
                           lv_coord_t x_off,
                           lv_coord_t y_off)
{
    lv_obj_t *obj = lv_obj_create(parent);
    if (!obj) return NULL;

    lv_obj_set_size(obj, 220, 60);
    lv_obj_align(obj, align, x_off, y_off);

    lv_obj_add_flag(obj, LV_OBJ_FLAG_CLICKABLE);
    /*
     * [修正1] LV_OBJ_FLAG_PRESS_LOCK を設定しない。
     * PRESS_LOCK があるとスワイプ中もボタンが入力を保持し CLICKED が発火する。
     * PRESS_LOCK なしではスワイプ中に指がボタン外へ出ると PRESS_LOST になり
     * CLICKED は発火しない。
     */
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_set_style_bg_color(obj, bg, LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(obj, bg_pressed, LV_STATE_PRESSED);
    lv_obj_set_style_radius(obj, 12, LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(obj, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(obj, 0, LV_STATE_DEFAULT);

    lv_obj_t *lbl = lv_label_create(obj);
    lv_label_set_text(lbl, text);
    lv_obj_set_style_text_color(lbl, lv_color_hex(0xFFFFFF), LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_20, LV_STATE_DEFAULT);
    lv_obj_clear_flag(lbl, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_center(lbl);

    return obj;
}

/* ================================================================== */
/* イベントハンドラ                                                    */
/*                                                                     */
/* [修正2] touch_handler_is_swiping() でスワイプ中の誤発火をガード。  */
/*                                                                     */
/* スワイプ時のイベント発火順 (PRESS_LOCK なし):                       */
/*   PRESSED → PRESSING → PRESS_LOST → RELEASED                      */
/*   → CLICKED / SHORT_CLICKED は基本発火しない                       */
/*                                                                     */
/* ただしスワイプ開始点がボタン上で移動量が小さい場合、               */
/* PRESS_LOST が発生しないケースへの保険として                        */
/* touch_handler_is_swiping() によるガードを追加する。               */
/* ================================================================== */

static void bootloader_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code != LV_EVENT_SHORT_CLICKED) return;

    /*
     * [修正2] スワイプ進行中なら無視。
     * touch_handler_is_swiping() は swipe_state.in_progress を返す。
     * タッチ DOWN 〜 UP の間は true のため、スワイプ動作中はここで弾かれる。
     */
    if (touch_handler_is_swiping()) {
        LOG_DBG("Bootloader button ignored - swipe in progress");
        return;
    }

    LOG_INF("Enter UF2 bootloader");
    sys_reboot(0x57);
}

static void reset_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code != LV_EVENT_SHORT_CLICKED) return;

    /* [修正2] 同上 */
    if (touch_handler_is_swiping()) {
        LOG_DBG("Reset button ignored - swipe in progress");
        return;
    }

    LOG_INF("System warm reset");
    sys_reboot(SYS_REBOOT_WARM);
}

/* ================================================================== */
/* Widget init                                                         */
/* ================================================================== */

int zmk_widget_system_settings_init(struct zmk_widget_system_settings *widget,
                                    lv_obj_t *parent)
{
    if (!parent) return -EINVAL;

    widget->obj = parent;

    /* ---- タイトル ---- */
    widget->title_label = lv_label_create(parent);
    lv_label_set_text(widget->title_label, "Quick Actions");
    lv_obj_set_style_text_color(widget->title_label,
                                lv_color_hex(0xFFFFFF), LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(widget->title_label,
                               &lv_font_montserrat_20, LV_STATE_DEFAULT);
    lv_obj_align(widget->title_label, LV_ALIGN_TOP_MID, 0, 14);

    /*
     * [修正3] ボタン Y 座標を調整。
     *
     * 旧: Bootloader(-44), Reset(+36)
     *   → ボタン間の隙間: 36-(-44)-64 = 16px (狭すぎ)
     *
     * 新: Bootloader(-52), Reset(+52)
     *   → ボタン間の隙間: 52-(-52)-60 = 44px (タップ領域が明確に分離)
     */

    /* ---- Bootloader ボタン ---- */
    widget->bootloader_btn = make_btn(
        parent, "Enter Bootloader",
        lv_color_hex(0x4A90E2), lv_color_hex(0x357ABD),
        LV_ALIGN_CENTER, 0, -52);
    if (!widget->bootloader_btn) return -ENOMEM;

    lv_obj_add_event_cb(widget->bootloader_btn,
                        bootloader_cb, LV_EVENT_SHORT_CLICKED, NULL);

    /* ---- System Reset ボタン ---- */
    widget->reset_btn = make_btn(
        parent, "System Reset",
        lv_color_hex(0xE24A4A), lv_color_hex(0xC93A3A),
        LV_ALIGN_CENTER, 0, 52);
    if (!widget->reset_btn) return -ENOMEM;

    lv_obj_add_event_cb(widget->reset_btn,
                        reset_cb, LV_EVENT_SHORT_CLICKED, NULL);

    /* ---- ナビゲーションヒント ---- */
    widget->nav_hint = lv_label_create(parent);
    lv_label_set_text(widget->nav_hint, "< swipe >");
    lv_obj_set_style_text_color(widget->nav_hint,
                                lv_color_hex(0x444444), LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(widget->nav_hint,
                               &lv_font_montserrat_20, LV_STATE_DEFAULT);
    lv_obj_align(widget->nav_hint, LV_ALIGN_BOTTOM_MID, 0, -10);

    return 0;
}

void zmk_widget_system_settings_show(struct zmk_widget_system_settings *widget)
{
    /* lv_scr_load() で表示するため何もしない */
}

void zmk_widget_system_settings_hide(struct zmk_widget_system_settings *widget)
{
    /* lv_scr_load() で切り替えるため何もしない */
}

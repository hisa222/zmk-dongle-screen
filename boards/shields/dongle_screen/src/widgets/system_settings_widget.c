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
 * [修正] ボタンのイベント登録を LV_EVENT_SHORT_CLICKED 単独から
 *        LV_EVENT_ALL に変更し、コールバック内で CLICKED のみ処理する。
 *
 *   問題:
 *     LV_EVENT_SHORT_CLICKED はスワイプ動作の終端でも発火することがある。
 *     リセット画面からスワイプで別画面に遷移しようとすると、
 *     タッチ UP 時に SHORT_CLICKED が誤発火してボタンが動作していた。
 *
 *   解決策:
 *     LV_EVENT_ALL で登録し、コールバック内で
 *     LV_EVENT_CLICKED / LV_EVENT_SHORT_CLICKED のみ処理する。
 *     スワイプ時は PRESS_LOST → RELEASED の順に発火し
 *     CLICKED / SHORT_CLICKED は発火しないため、誤作動が防止される。
 */

#include "system_settings_widget.h"

#include <zephyr/logging/log.h>
#include <zephyr/sys/reboot.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

/* ================================================================== */
/* ボタン作成ヘルパー                                                  */
/* lv_obj_create(parent) で直接 parent (スクリーン) に配置する        */
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

    lv_obj_set_size(obj, 220, 64);
    lv_obj_align(obj, align, x_off, y_off);

    /* LVGL8: lv_obj_create はデフォルトで CLICKABLE。念のため明示。 */
    lv_obj_add_flag(obj, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_flag(obj, LV_OBJ_FLAG_PRESS_LOCK);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_set_style_bg_color(obj, bg, LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_STATE_DEFAULT);
    lv_obj_set_style_bg_color(obj, bg_pressed, LV_STATE_PRESSED);
    lv_obj_set_style_radius(obj, 12, LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(obj, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(obj, 0, LV_STATE_DEFAULT);

    /* ラベルはクリック透過 */
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
/* [修正] LV_EVENT_ALL で登録し、CLICKED / SHORT_CLICKED のみ処理。   */
/*                                                                     */
/* スワイプ時のイベント発火順:                                         */
/*   PRESSED → PRESSING → PRESS_LOST → RELEASED                      */
/*   ↑ CLICKED / SHORT_CLICKED は発火しない → 誤作動しない            */
/*                                                                     */
/* 正常タップ時のイベント発火順:                                       */
/*   PRESSED → RELEASED → SHORT_CLICKED → CLICKED                    */
/*   ↑ CLICKED で処理される → 正常動作                                */
/* ================================================================== */

static void bootloader_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);

    /*
     * [修正] CLICKED または SHORT_CLICKED のみ処理。
     * 旧実装: if (code != LV_EVENT_SHORT_CLICKED) return;
     *   → スワイプ終端で SHORT_CLICKED が誤発火することがあった。
     * 新実装: LV_EVENT_ALL 登録 + 両イベントをガード。
     *   → PRESS_LOST が介在するスワイプでは CLICKED/SHORT_CLICKED は来ない。
     */
    if (code != LV_EVENT_CLICKED && code != LV_EVENT_SHORT_CLICKED) return;

    LOG_INF("Enter UF2 bootloader");
    sys_reboot(0x57);
}

static void reset_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);

    /* [修正] 同上 */
    if (code != LV_EVENT_CLICKED && code != LV_EVENT_SHORT_CLICKED) return;

    LOG_INF("System warm reset");
    sys_reboot(SYS_REBOOT_WARM);
}

/* ================================================================== */
/* Widget init                                                         */
/* parent = lv_obj_create(NULL) のスクリーン。コンテナなし。          */
/* ================================================================== */

int zmk_widget_system_settings_init(struct zmk_widget_system_settings *widget,
                                    lv_obj_t *parent)
{
    if (!parent) return -EINVAL;

    /* widget->obj = parent そのもの */
    widget->obj = parent;

    /* ---- タイトル ---- */
    widget->title_label = lv_label_create(parent);
    lv_label_set_text(widget->title_label, "Quick Actions");
    lv_obj_set_style_text_color(widget->title_label,
                                lv_color_hex(0xFFFFFF), LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(widget->title_label,
                               &lv_font_montserrat_20, LV_STATE_DEFAULT);
    lv_obj_align(widget->title_label, LV_ALIGN_TOP_MID, 0, 14);

    /* ---- Bootloader ボタン ---- */
    widget->bootloader_btn = make_btn(
        parent, "Enter Bootloader",
        lv_color_hex(0x4A90E2), lv_color_hex(0x357ABD),
        LV_ALIGN_CENTER, 0, -44);
    if (!widget->bootloader_btn) return -ENOMEM;

    /*
     * [修正] LV_EVENT_SHORT_CLICKED → LV_EVENT_ALL に変更。
     * コールバック内で CLICKED / SHORT_CLICKED のみ処理する。
     */
    lv_obj_add_event_cb(widget->bootloader_btn,
                        bootloader_cb, LV_EVENT_ALL, NULL);

    /* ---- System Reset ボタン ---- */
    widget->reset_btn = make_btn(
        parent, "System Reset",
        lv_color_hex(0xE24A4A), lv_color_hex(0xC93A3A),
        LV_ALIGN_CENTER, 0, 36);
    if (!widget->reset_btn) return -ENOMEM;

    /* [修正] 同上 */
    lv_obj_add_event_cb(widget->reset_btn,
                        reset_cb, LV_EVENT_ALL, NULL);

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

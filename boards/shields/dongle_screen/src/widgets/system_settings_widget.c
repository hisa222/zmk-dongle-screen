/*
 * Copyright (c) 2024 The ZMK Contributors
 * SPDX-License-Identifier: MIT
 *
 * System Settings Widget - Bootloader / System Reset
 *
 * 設計方針:
 *   parent はスクリーン自体。中間コンテナを作らない。
 *   全 LVGL オブジェクトを直接 parent に配置する。
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
/* ================================================================== */

static void bootloader_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
    LOG_INF("Enter UF2 bootloader");
    sys_reboot(0x57);
}

static void reset_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) return;
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
    lv_obj_add_event_cb(widget->bootloader_btn, bootloader_cb,
                        LV_EVENT_CLICKED, NULL);

    /* ---- System Reset ボタン ---- */
    widget->reset_btn = make_btn(
        parent, "System Reset",
        lv_color_hex(0xE24A4A), lv_color_hex(0xC93A3A),
        LV_ALIGN_CENTER, 0, 36);
    if (!widget->reset_btn) return -ENOMEM;
    lv_obj_add_event_cb(widget->reset_btn, reset_cb,
                        LV_EVENT_CLICKED, NULL);

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

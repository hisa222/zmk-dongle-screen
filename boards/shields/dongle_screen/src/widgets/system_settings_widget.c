/*
 * Copyright (c) 2024 The ZMK Contributors
 * SPDX-License-Identifier: MIT
 *
 * System Settings Widget
 *
 * 方針:
 * - 見た目のボタンは大きいまま
 * - 実際のクリック領域はボタン中央の狭い矩形だけにする
 * - クリック領域の外側は何も反応しない
 * - スワイプ中の誤発火を防ぐ
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

/*
 * 見た目のボタンサイズ
 */
#define ACTION_BTN_W 220
#define ACTION_BTN_H 60

/*
 * 実際に反応する当たり判定サイズ
 * ご要望どおり、中央の正方形に近いサイズへ絞る
 */
#define ACTION_HIT_W 60
#define ACTION_HIT_H 60

/* ================================================================== */
/* Widget-private state                                               */
/* ================================================================== */

struct action_btn_bundle {
    lv_obj_t *visual_btn;   /* 見た目だけの大きいボタン */
    lv_obj_t *hitbox;       /* 中央の狭いクリック領域 */
};

/*
 * この widget でだけ使う静的 bundle。
 * screen は1回だけ生成される前提なのでこれで十分。
 */
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
     * 見た目だけにしたいので clickable は付けない
     */
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_set_style_bg_color(obj, bg, LV_STATE_DEFAULT);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, LV_STATE_DEFAULT);
    lv_obj_set_style_radius(obj, 12, LV_STATE_DEFAULT);
    lv_obj_set_style_border_width(obj, 0, LV_STATE_DEFAULT);
    lv_obj_set_style_pad_all(obj, 0, LV_STATE_DEFAULT);

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

static void ui_press_start_cb(lv_event_t *e)
{
    ARG_UNUSED(e);
    ui_interaction_active = true;
}

static void ui_press_end_cb(lv_event_t *e)
{
    ARG_UNUSED(e);
    ui_interaction_active = false;
}

/* ================================================================== */
/* Action callbacks                                                   */
/* ================================================================== */

static void bootloader_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }

    if (touch_handler_is_swiping()) {
        LOG_DBG("Bootloader ignored: swipe in progress");
        return;
    }

    ui_interaction_active = false;
    LOG_INF("Enter UF2 bootloader");
    sys_reboot(0x57);
}

static void reset_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }

    if (touch_handler_is_swiping()) {
        LOG_DBG("Reset ignored: swipe in progress");
        return;
    }

    ui_interaction_active = false;
    LOG_INF("System warm reset");
    sys_reboot(SYS_REBOOT_WARM);
}

/* ================================================================== */
/* Hitbox helper                                                      */
/* ================================================================== */

static lv_obj_t *make_center_hitbox(lv_obj_t *parent_visual_btn,
                                    lv_event_cb_t clicked_cb)
{
    /*
     * visual_btn の子として、中央に小さいクリック領域を置く
     */
    lv_obj_t *hit = lv_obj_create(parent_visual_btn);
    if (!hit) {
        return NULL;
    }

    lv_obj_set_size(hit, ACTION_HIT_W, ACTION_HIT_H);
    lv_obj_center(hit);

    lv_obj_add_flag(hit, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(hit, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_clear_flag(hit, LV_OBJ_FLAG_PRESS_LOCK);

    /*
     * 見た目は完全透明
     */
    lv_obj_set_style_bg_opa(hit, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_opa(hit, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_outline_opa(hit, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_radius(hit, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(hit, 0, LV_PART_MAIN);

    /*
     * interaction 管理
     */
    lv_obj_add_event_cb(hit, ui_press_start_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(hit, ui_press_end_cb,   LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(hit, ui_press_end_cb,   LV_EVENT_PRESS_LOST, NULL);

    /*
     * 実アクション
     */
    lv_obj_add_event_cb(hit, clicked_cb, LV_EVENT_CLICKED, NULL);

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

    widget->title_label = lv_label_create(parent);
    lv_label_set_text(widget->title_label, "Quick Actions");
    lv_obj_set_style_text_color(widget->title_label,
                                lv_color_hex(0xFFFFFF), LV_STATE_DEFAULT);
    lv_obj_set_style_text_font(widget->title_label,
                               &lv_font_montserrat_20, LV_STATE_DEFAULT);
    lv_obj_align(widget->title_label, LV_ALIGN_TOP_MID, 0, 14);

    /*
     * Bootloader visual button
     */
    boot_bundle.visual_btn = make_visual_btn(
        parent,
        "Enter Bootloader",
        lv_color_hex(0x4A90E2),
        LV_ALIGN_CENTER, 0, -52);
    if (!boot_bundle.visual_btn) {
        return -ENOMEM;
    }

    boot_bundle.hitbox = make_center_hitbox(boot_bundle.visual_btn, bootloader_cb);
    if (!boot_bundle.hitbox) {
        return -ENOMEM;
    }

    /*
     * Reset visual button
     */
    reset_bundle.visual_btn = make_visual_btn(
        parent,
        "System Reset",
        lv_color_hex(0xE24A4A),
        LV_ALIGN_CENTER, 0, 52);
    if (!reset_bundle.visual_btn) {
        return -ENOMEM;
    }

    reset_bundle.hitbox = make_center_hitbox(reset_bundle.visual_btn, reset_cb);
    if (!reset_bundle.hitbox) {
        return -ENOMEM;
    }

    /*
     * 既存 API 互換のため、widget 内には visual_btn を入れておく
     */
    widget->bootloader_btn = boot_bundle.visual_btn;
    widget->reset_btn = reset_bundle.visual_btn;

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
    ARG_UNUSED(widget);
    ui_interaction_active = false;
}

void zmk_widget_system_settings_hide(struct zmk_widget_system_settings *widget)
{
    ui_interaction_active = false;

    if (!widget) {
        return;
    }

    widget->title_label = NULL;
    widget->bootloader_btn = NULL;
    widget->reset_btn = NULL;
    widget->nav_hint = NULL;
    widget->obj = NULL;

    boot_bundle.visual_btn = NULL;
    boot_bundle.hitbox = NULL;
    reset_bundle.visual_btn = NULL;
    reset_bundle.hitbox = NULL;
}

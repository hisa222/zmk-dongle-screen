/*
 * Copyright (c) 2024 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/kernel.h>
#include <zephyr/bluetooth/services/bas.h>

#include <zephyr/logging/log.h>
LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#include <zmk/display.h>
#include <zmk/event_manager.h>
#include <zmk/events/wpm_state_changed.h>
#include <zmk/wpm.h>

#include "bongo_spheal.h"

#define SRC(array) (const void **)array, sizeof(array) / sizeof(lv_img_dsc_t *)

static sys_slist_t widgets = SYS_SLIST_STATIC_INIT(&widgets);
static int64_t last_anim_update_time = 0;
#define ANIM_UPDATE_INTERVAL_MS 200  // Throttle: max 5 animation checks per second

LV_IMG_DECLARE(bongo_spheal_none);
LV_IMG_DECLARE(bongo_spheal_left1);
LV_IMG_DECLARE(bongo_spheal_left2);
LV_IMG_DECLARE(bongo_spheal_left3);
LV_IMG_DECLARE(bongo_spheal_right1);
LV_IMG_DECLARE(bongo_spheal_right2);
LV_IMG_DECLARE(bongo_spheal_right3);
LV_IMG_DECLARE(bongo_spheal_both1);
LV_IMG_DECLARE(bongo_spheal_both1_open);
LV_IMG_DECLARE(bongo_spheal_both2);

#define ANIMATION_SPEED_IDLE 10000
const lv_img_dsc_t *idle_imgs_s[] = {
    &bongo_spheal_both1_open,
    &bongo_spheal_both1_open,
    &bongo_spheal_both1_open,
    &bongo_spheal_both1,
    &bongo_spheal_both1_open,
    &bongo_spheal_both1_open,
    &bongo_spheal_none,
    &bongo_spheal_none,
};

#define ANIMATION_SPEED_SLOW 2000
const lv_img_dsc_t *slow_imgs_s[] = {
    &bongo_spheal_left1,
    &bongo_spheal_both1,
    &bongo_spheal_both1,
    &bongo_spheal_right1,
    &bongo_spheal_both1,
    &bongo_spheal_both1,
    &bongo_spheal_both1_open,
    &bongo_spheal_both1_open,
    &bongo_spheal_left1,
    &bongo_spheal_both1_open,
    &bongo_spheal_both1_open,
    &bongo_spheal_right1,
    &bongo_spheal_both1_open,
    &bongo_spheal_both1_open,
    &bongo_spheal_both1,
    &bongo_spheal_both1,
};

#define ANIMATION_SPEED_MID 500
const lv_img_dsc_t *mid_imgs_s[] = {
    &bongo_spheal_left2,
    &bongo_spheal_left3,
    &bongo_spheal_both1,
    &bongo_spheal_right2,
    &bongo_spheal_right3,
    &bongo_spheal_both1,
};

#define ANIMATION_SPEED_FAST 200
const lv_img_dsc_t *fast_imgs_s[] = {
    &bongo_spheal_both2,
    &bongo_spheal_both1,
    &bongo_spheal_both1_open,
    &bongo_spheal_both2,
    &bongo_spheal_both1,
    &bongo_spheal_both1_open,
};

struct bongo_spheal_wpm_status_state {
    uint8_t wpm;
};

enum anim_state {
    anim_state_none,
    anim_state_idle,
    anim_state_slow,
    anim_state_mid,
    anim_state_fast
} current_anim_state_s;

static void set_animation(lv_obj_t *animing, struct bongo_spheal_wpm_status_state state) {
    // Throttle animation state changes to prevent display thread flooding
    int64_t now = k_uptime_get();
    if ((now - last_anim_update_time) < ANIM_UPDATE_INTERVAL_MS) {
        return;
    }
    last_anim_update_time = now;

    if (state.wpm < 5) {
        if (current_anim_state_s != anim_state_idle) {
            lv_animimg_set_src(animing, SRC(idle_imgs_s));
            lv_animimg_set_duration(animing, ANIMATION_SPEED_IDLE);
            lv_animimg_set_repeat_count(animing, LV_ANIM_REPEAT_INFINITE);
            lv_animimg_start(animing);
            current_anim_state_s = anim_state_idle;
        }
    } else if (state.wpm < 30) {
        if (current_anim_state_s != anim_state_slow) {
            lv_animimg_set_src(animing, SRC(slow_imgs_s));
            lv_animimg_set_duration(animing, ANIMATION_SPEED_SLOW);
            lv_animimg_set_repeat_count(animing, LV_ANIM_REPEAT_INFINITE);
            lv_animimg_start(animing);
            current_anim_state_s = anim_state_slow;
        }
    } else if (state.wpm < 70) {
        if (current_anim_state_s != anim_state_mid) {
            lv_animimg_set_src(animing, SRC(mid_imgs_s));
            lv_animimg_set_duration(animing, ANIMATION_SPEED_MID);
            lv_animimg_set_repeat_count(animing, LV_ANIM_REPEAT_INFINITE);
            lv_animimg_start(animing);
            current_anim_state_s = anim_state_mid;
        }
    } else {
        if (current_anim_state_s != anim_state_fast) {
            lv_animimg_set_src(animing, SRC(fast_imgs_s));
            lv_animimg_set_duration(animing, ANIMATION_SPEED_FAST);
            lv_animimg_set_repeat_count(animing, LV_ANIM_REPEAT_INFINITE);
            lv_animimg_start(animing);
            current_anim_state_s = anim_state_fast;
        }
    }
}

struct bongo_spheal_wpm_status_state bongo_spheal_wpm_status_get_state(const zmk_event_t *eh) {
    struct zmk_wpm_state_changed *ev = as_zmk_wpm_state_changed(eh);
    // Add NULL check to prevent crash if event is NULL
    return (struct bongo_spheal_wpm_status_state) { .wpm = ev ? ev->state : 0 };
};

void bongo_spheal_wpm_status_update_cb(struct bongo_spheal_wpm_status_state state) {
    struct zmk_widget_bongo_spheal *widget;
    SYS_SLIST_FOR_EACH_CONTAINER(&widgets, widget, node) { set_animation(widget->obj, state); }
}

ZMK_DISPLAY_WIDGET_LISTENER(widget_bongo_spheal, struct bongo_spheal_wpm_status_state,
                            bongo_spheal_wpm_status_update_cb, bongo_spheal_wpm_status_get_state)

ZMK_SUBSCRIPTION(widget_bongo_spheal, zmk_wpm_state_changed);

int zmk_widget_bongo_spheal_init(struct zmk_widget_bongo_spheal *widget, lv_obj_t *parent) {
widget->obj = lv_animimg_create(parent);

lv_animimg_set_src(widget->obj, SRC(idle_imgs_s));
// lv_img_set_zoom(widget->obj, 768);
lv_img_set_zoom(widget->obj, 435); // 256 * n
lv_obj_set_size(widget->obj, 90 * 1.7, 47 * 1.7);
lv_obj_center(widget->obj);

lv_obj_set_style_bg_color(widget->obj, lv_color_hex(0xFF0000), 0);
lv_obj_set_style_bg_opa(widget->obj, LV_OPA_COVER, 0);
    
lv_animimg_set_duration(widget->obj, ANIMATION_SPEED_IDLE);
lv_animimg_set_repeat_count(widget->obj, LV_ANIM_REPEAT_INFINITE);
lv_animimg_start(widget->obj);

sys_slist_append(&widgets, &widget->node);
widget_bongo_spheal_init();
    
    return 0;
}

lv_obj_t *zmk_widget_bongo_spheal_obj(struct zmk_widget_bongo_spheal *widget) {
    return widget->obj;
}

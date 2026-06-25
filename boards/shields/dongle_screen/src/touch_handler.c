/*
 * Copyright (c) 2024 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include "touch_handler.h"
#include "events/swipe_gesture_event.h"

/*
 * brightness_screen.c がこの weak 関数を override する。
 * Prospector と同様に引数なし版を使用する。
 * スライダー操作中 + brightness screen 表示中かどうかを返す。
 */
__attribute__((weak)) bool display_settings_is_interacting(void) {
    return false;
}

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/input/input.h>
#include <zephyr/dt-bindings/input/input-event-codes.h>
#include <zephyr/logging/log.h>
#include <lvgl.h>
#include <zmk/event_manager.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#define TOUCH_NODE DT_NODELABEL(touch_sensor)

#if !DT_NODE_EXISTS(TOUCH_NODE)
#error "Touch sensor device tree node not found"
#endif

#define SWIPE_THRESHOLD       30
#define SWIPE_COOLDOWN_MS     400
#define DOUBLE_TAP_THRESHOLD  10
#define DOUBLE_TAP_INTERVAL_MS 350

static struct touch_event_data last_event = {0};
static touch_event_callback_t registered_callback = NULL;
static bool touch_active = false;
static bool prev_touch_active = false;

volatile bool ui_interaction_active = false;

static lv_indev_t *lvgl_indev = NULL;

static uint16_t current_x = 0;
static uint16_t current_y = 0;
static bool x_updated = false;
static bool y_updated = false;

static struct {
    int16_t start_x;
    int16_t start_y;
    int64_t start_time;
    bool in_progress;
} swipe_state = {0};

static int64_t last_swipe_time = 0;
static int64_t last_tap_time   = 0;
static bool swipe_already_raised = false;

bool touch_handler_is_swiping(void) {
    return swipe_state.in_progress;
}

static void raise_swipe_event(enum swipe_direction direction)
{
    const char *dir_name[] = {"UP", "DOWN", "LEFT", "RIGHT"};

    int64_t now = k_uptime_get();
    if ((now - last_swipe_time) < SWIPE_COOLDOWN_MS) {
        LOG_DBG("Swipe blocked - cooldown active");
        return;
    }

    /*
     * Prospector と同様: ui_interaction_active か display_settings_is_interacting()
     * のどちらかが true ならスワイプをブロック。
     * ただし brightness slider が縦スワイプで drag_cancelled になった際は
     * display_settings_is_interacting() が false を返し、かつ
     * ui_interaction_active も false になるため、スワイプは通過できる。
     */
    if (ui_interaction_active) {
        LOG_DBG("Swipe blocked - ui_interaction_active");
        return;
    }

    if (display_settings_is_interacting()) {
        LOG_DBG("Swipe blocked - display_settings_is_interacting");
        return;
    }

    last_swipe_time = now;
    LOG_INF("Raising ZMK swipe event: %s", dir_name[direction]);

    raise_zmk_swipe_gesture_event(
        (struct zmk_swipe_gesture_event){ .direction = direction }
    );
}

static void touch_input_callback(struct input_event *evt, void *user_data)
{
    ARG_UNUSED(user_data);

    switch (evt->code) {
    case INPUT_KEY_DOWN:
        if (evt->value == 1 && !ui_interaction_active) {
            swipe_already_raised = true;
            raise_swipe_event(SWIPE_DIRECTION_DOWN);
        }
        break;

    case INPUT_KEY_UP:
        if (evt->value == 1 && !ui_interaction_active) {
            swipe_already_raised = true;
            raise_swipe_event(SWIPE_DIRECTION_UP);
        }
        break;

    case INPUT_KEY_LEFT:
        if (evt->value == 1 && !ui_interaction_active) {
            swipe_already_raised = true;
            raise_swipe_event(SWIPE_DIRECTION_LEFT);
        }
        break;

    case INPUT_KEY_RIGHT:
        if (evt->value == 1 && !ui_interaction_active) {
            swipe_already_raised = true;
            raise_swipe_event(SWIPE_DIRECTION_RIGHT);
        }
        break;

    case INPUT_ABS_X:
        current_x = (uint16_t)evt->value;
        x_updated = true;
        break;

    case INPUT_ABS_Y:
        current_y = (uint16_t)evt->value;
        y_updated = true;
        break;

    case INPUT_BTN_TOUCH:
        touch_active = (evt->value != 0);

        last_event.x         = current_x;
        last_event.y         = current_y;
        last_event.touched   = touch_active;
        last_event.timestamp = k_uptime_get_32();

        {
            bool touch_started = touch_active && !prev_touch_active;

            if (touch_started) {
                swipe_state.start_x    = current_x;
                swipe_state.start_y    = current_y;
                swipe_state.start_time = k_uptime_get();
                swipe_state.in_progress = true;
                swipe_already_raised    = false;

                /*
                 * タッチ開始時点で ui_interaction_active が true
                 * (= スライダー操作中) ならば、この touch 中はスワイプを起こさない
                 */
                if (ui_interaction_active || display_settings_is_interacting()) {
                    swipe_already_raised = true;
                }

                x_updated = false;
                y_updated = false;
            } else if (!touch_active) {
                /* Touch UP */
                if (!swipe_already_raised && swipe_state.in_progress && !ui_interaction_active) {
                    int16_t raw_dx = current_x - swipe_state.start_x;
                    int16_t raw_dy = current_y - swipe_state.start_y;

                    /*
                     * ROTATED_270 用変換:
                     * Touch Y → Display X
                     * Touch X → Display Y (inverted)
                     */
                    int16_t dx = raw_dy;
                    int16_t dy = -raw_dx;

                    int16_t abs_dx = (dx < 0) ? -dx : dx;
                    int16_t abs_dy = (dy < 0) ? -dy : dy;

                    if (abs_dy > abs_dx && abs_dy > SWIPE_THRESHOLD) {
                        raise_swipe_event(dy > 0 ? SWIPE_DIRECTION_DOWN : SWIPE_DIRECTION_UP);
                    } else if (abs_dx > abs_dy && abs_dx > SWIPE_THRESHOLD) {
                        raise_swipe_event(dx > 0 ? SWIPE_DIRECTION_RIGHT : SWIPE_DIRECTION_LEFT);
                    } else if (abs_dx <= DOUBLE_TAP_THRESHOLD && abs_dy <= DOUBLE_TAP_THRESHOLD) {
                        int64_t tap_now = k_uptime_get();
                        if ((tap_now - last_tap_time) < DOUBLE_TAP_INTERVAL_MS && last_tap_time > 0) {
                            last_tap_time = 0;
                            raise_swipe_event(SWIPE_DIRECTION_DOUBLE_TAP);
                        } else {
                            last_tap_time = tap_now;
                        }
                    }
                }

                swipe_state.in_progress  = false;
                swipe_already_raised     = false;

                x_updated = false;
                y_updated = false;
            }
        }

        if (registered_callback) {
            registered_callback(&last_event);
        }

        prev_touch_active = touch_active;
        break;

    default:
        break;
    }
}

/* Zephyr 3.5: 2-arg macro */
INPUT_CALLBACK_DEFINE(DEVICE_DT_GET(TOUCH_NODE), touch_input_callback);

/*
 * LVGL8 input device read callback
 *
 * physical touch: 240 x 280
 * logical display: 280 x 240 (ROTATED_270)
 */
static void lvgl_input_read(lv_indev_t *indev, lv_indev_data_t *data)
{
    ARG_UNUSED(indev);

    int32_t logical_x = current_y;
    int32_t logical_y = 239 - current_x;

    if (logical_x < 0)   logical_x = 0;
    if (logical_x > 279) logical_x = 279;
    if (logical_y < 0)   logical_y = 0;
    if (logical_y > 239) logical_y = 239;

    data->point.x = logical_x;
    data->point.y = logical_y;
    data->state   = touch_active ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

int touch_handler_init(void)
{
    const struct device *touch_dev = DEVICE_DT_GET(TOUCH_NODE);

    if (!device_is_ready(touch_dev)) {
        LOG_ERR("Touch sensor device not ready");
        return -ENODEV;
    }

    LOG_INF("Touch handler initialized");
    return 0;
}

int touch_handler_register_lvgl_indev(void)
{
    if (lvgl_indev) {
        return 0;
    }

    /* LVGL8 API: lv_indev_drv_init + lv_indev_drv_register */
    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type    = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = lvgl_input_read;

    lvgl_indev = lv_indev_drv_register(&indev_drv);
    if (!lvgl_indev) {
        LOG_ERR("Failed to register LVGL input device");
        return -ENOMEM;
    }

    LOG_INF("LVGL input device registered");
    return 0;
}

int touch_handler_register_callback(touch_event_callback_t callback)
{
    if (!callback) {
        return -EINVAL;
    }

    registered_callback = callback;
    return 0;
}

int touch_handler_get_last_event(struct touch_event_data *event)
{
    if (!event) {
        return -EINVAL;
    }

    if (last_event.timestamp == 0) {
        return -ENODATA;
    }

    *event = last_event;
    return 0;
}

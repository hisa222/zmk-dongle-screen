// touch_handler.h
#pragma once
#include <lvgl.h>

// status_screen.c で定義、touch_handler.c から参照する
extern lv_obj_t *dongle_main_screen;

#ifdef CONFIG_DONGLE_SCREEN_TOUCH_ENABLED
void touch_handler_init(void);
#else
static inline void touch_handler_init(void) {}
#endif

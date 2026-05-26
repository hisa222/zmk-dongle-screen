// touch_handler.h
#pragma once

#ifdef CONFIG_DONGLE_SCREEN_TOUCH_ENABLED
void touch_handler_init(void);
#else
static inline void touch_handler_init(void) {}
#endif

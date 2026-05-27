# Prospector Scanner CMakeLists.txt
# Step-by-step restoration from display_test

if(CONFIG_SHIELD_PROSPECTOR_SCANNER)
    # Backlight initialization (always needed)
    target_sources(app PRIVATE src_t-ogura/backlight_init.c)

    # CUSTOM screen support (from display_test)
    target_sources_ifdef(CONFIG_ZMK_DISPLAY_STATUS_SCREEN_CUSTOM app PRIVATE src_t-ogura/custom_status_screen.c)
    target_sources_ifdef(CONFIG_ZMK_DISPLAY_STATUS_SCREEN_CUSTOM app PRIVATE src_t-ogura/hello_widget.c)

    # Swipe gesture event (ZMK event system - thread-safe)
    target_sources_ifdef(CONFIG_ZMK_DISPLAY_STATUS_SCREEN_CUSTOM app PRIVATE src/events/swipe_gesture_event.c)

    # Touch handler (when touch is enabled)
    target_sources_ifdef(CONFIG_PROSPECTOR_TOUCH_ENABLED app PRIVATE src_t-ogura/touch_handler.c)

    # Scanner stub for message handling (required by custom_status_screen.c)
    target_sources_ifdef(CONFIG_PROSPECTOR_MODE_SCANNER app PRIVATE src_t-ogura/scanner_stub.c)

    # Display settings persistence (NVS)
    target_sources_ifdef(CONFIG_ZMK_DISPLAY_STATUS_SCREEN_CUSTOM app PRIVATE src_t-ogura/display_settings.c)

    # Brightness control with APDS9960 sensor for auto brightness
    target_sources_ifdef(CONFIG_ZMK_DISPLAY_STATUS_SCREEN_CUSTOM app PRIVATE src_t-ogura/brightness_control.c)

    # System settings widget (Bootloader, Reset, Channel selector)
    target_sources_ifdef(CONFIG_ZMK_DISPLAY_STATUS_SCREEN_CUSTOM app PRIVATE src_t-ogura/system_settings_widget.c)

    # NerdFont for modifier icons
    target_sources_ifdef(CONFIG_ZMK_DISPLAY_STATUS_SCREEN_CUSTOM app PRIVATE src/fonts/NerdFonts_Regular_40.c)

    # Prospector Display layouts (Carrefinho-inspired)
    target_sources_ifdef(CONFIG_ZMK_DISPLAY_STATUS_SCREEN_CUSTOM app PRIVATE src_t-ogura/prospector_layouts.c)
    target_sources_ifdef(CONFIG_ZMK_DISPLAY_STATUS_SCREEN_CUSTOM app PRIVATE src_t-ogura/field_layout.c)
    target_sources_ifdef(CONFIG_ZMK_DISPLAY_STATUS_SCREEN_CUSTOM app PRIVATE src_t-ogura/operator_layout.c)
    target_sources_ifdef(CONFIG_ZMK_DISPLAY_STATUS_SCREEN_CUSTOM app PRIVATE src_t-ogura/radii_layout.c)

    # Carrefinho custom fonts
    target_sources_ifdef(CONFIG_ZMK_DISPLAY_STATUS_SCREEN_CUSTOM app PRIVATE
        src/fonts_carrefinho/DINish_Condensed_Semibold_20.c
        src/fonts_carrefinho/DINish_Condensed_Semibold_22.c
        src/fonts_carrefinho/DINish_Expanded_Light_36.c
        src/fonts_carrefinho/DINish_Medium_24.c
        src/fonts_carrefinho/FG_Medium_20.c
        src/fonts_carrefinho/FG_Medium_21.c
        src/fonts_carrefinho/FG_Medium_24.c
        src/fonts_carrefinho/FG_Medium_26.c
        src/fonts_carrefinho/FR_Medium_32.c
        src/fonts_carrefinho/FR_Regular_30.c
        src/fonts_carrefinho/FR_Regular_36.c
        src/fonts_carrefinho/FR_Regular_48.c
        src/fonts_carrefinho/FR_Thin_48.c
        src/fonts_carrefinho/PPF_NarrowThin_64.c
        src/fonts_carrefinho/Symbols_Bold_26.c
        src/fonts_carrefinho/Symbols_Medium_28.c
        src/fonts_carrefinho/Symbols_Regular_28.c
        src/fonts_carrefinho/Symbols_Semibold_28.c
        src/fonts_carrefinho/Symbols_Semibold_32.c
    )

    # Include path for local headers
    target_include_directories(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
endif()

# ORIGINAL FULL BUILD - TO BE RESTORED INCREMENTALLY
# if(CONFIG_SHIELD_PROSPECTOR_SCANNER)
#   zephyr_library()
#   ... (original code preserved in comments)
# endif()


#_________________________________________________________________________________
#if(CONFIG_SHIELD_DONGLE_SCREEN)
  #zephyr_library()
  #zephyr_library_sources(${ZEPHYR_BASE}/misc/empty_file.c)
  #zephyr_library_include_directories(${ZEPHYR_LVGL_MODULE_DIR})
  #zephyr_library_include_directories(${ZEPHYR_BASE}/lib/gui/lvgl/)
  #zephyr_library_include_directories(${ZEPHYR_BASE}/drivers)
  #zephyr_library_include_directories(${CMAKE_SOURCE_DIR}/include)
  #zephyr_library_include_directories(${ZEPHYR_CURRENT_MODULE_DIR}/include)
  #zephyr_library_include_directories(${ZEPHYR_CURRENT_CMAKE_DIR}/include)
  #zephyr_library_include_directories(include)
  #zephyr_library_sources(src/brightness.c)
  #zephyr_library_sources(src/custom_status_screen.c)
  #zephyr_library_sources(src/screen_rotate_init.c)
  #zephyr_library_sources(src/widgets/output_status.c)
  #zephyr_library_sources(src/widgets/battery_status.c)
  #zephyr_library_sources(src/widgets/layer_status.c)
  #zephyr_library_sources(src/widgets/wpm_status.c)
  #zephyr_library_sources(src/widgets/mod_status.c)
  #file(GLOB font_sources src/fonts/*.c)
  #zephyr_library_sources(${font_sources})
  
  #if(CONFIG_DONGLE_SCREEN_TOUCH_ENABLED)
    #target_sources(app PRIVATE touch_handler.c)
  #endif()
#endif()
#_________________________________________________________________________________

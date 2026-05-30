/**
 * lv_conf.h (sim) — shim picked up by LVGL on the native build.
 *
 * LVGL resolves its config via LV_CONF_INCLUDE_SIMPLE → #include "lv_conf.h".
 * On the native build the include path lists sim/ BEFORE the project root, so
 * THIS file wins over the device root lv_conf.h. It simply forwards to the
 * simulator conf, which itself inherits the device conf and overrides only the
 * host-specific bits. The device (esp32dev) build never sees this file — its
 * include path only contains the project root, so it uses the real lv_conf.h.
 */
#pragma once
#include "lv_conf_sim.h"

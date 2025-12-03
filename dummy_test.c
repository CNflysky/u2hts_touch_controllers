/*
  Copyright (C) CNflysky.
  U2HTS stands for "USB to HID TouchScreen".

  dummy_test.c: generate random coordinates report to host.
  WARNING: THIS DRIVER IS INTENDED FOR TOUCH REPORT RATE MEASUREMENT,
  IT GENERATES LOT OF RANDOM TOUCHS MAY CAUSE UNEXPECTED BEHAVIOUR ON YOUR HOST!
  To monitor input device report rate:
    - Build the "getevent" tool (originally from Android, you can find many
  Linux ports on GitHub):
    - Run `./getevent -r`.

  To use this driver:
    - Enable polling mode.
    - set controller_name to "dummy".
  See `CMakeLists.txt` for build.
  
  This file is licensed under GPL V3.
  All rights reserved.
*/
#include <stdlib.h>

#include "u2hts_core.h"
static bool dummy_setup(U2HTS_BUS_TYPES bus_type) {
  U2HTS_UNUSED(bus_type);
  srand(u2hts_get_scan_time());
  return true;
}

static inline uint8_t random_tp_count() { return rand() % 10; }
static inline void random_tp(u2hts_tp *tp) {
  tp->contact = true;
  tp->x = rand() % 4096;
  tp->y = rand() % 4096;
  tp->height = rand() % 256;
  tp->width = rand() % 256;
  tp->pressure = rand() % 256;
}

static void dummy_coord_fetch(const u2hts_config *cfg,
                              u2hts_hid_report *report) {
  report->tp_count = random_tp_count();
  for (uint8_t i = 0; i < report->tp_count; i++) {
    report->tp[i].id = i;
    random_tp(&report->tp[i]);
    u2hts_apply_config_to_tp(cfg, &report->tp[i]);
  }
}

static u2hts_touch_controller_config dummy_get_config() {
  u2hts_touch_controller_config config = {
      .max_tps = 10, .x_max = 4096, .y_max = 4096};
  return config;
}

static u2hts_touch_controller_operations dummy_ops = {
    .setup = &dummy_setup,
    .fetch = &dummy_coord_fetch,
    .get_config = &dummy_get_config};

static u2hts_touch_controller dummy = {.name = "dummy",
                                       .i2c_addr = 0x00,
                                       .i2c_speed = 100 * 1000,  // 100 KHz
                                       .irq_type = 0xFF,
                                       .operations = &dummy_ops};
U2HTS_TOUCH_CONTROLLER(dummy);
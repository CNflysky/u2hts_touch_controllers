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
static bool dummy_setup(U2HTS_BUS_TYPES bus_type,
                        const char* custom_controller_config) {
  U2HTS_UNUSED(bus_type);
  int32_t rand_seed = 0;
  u2hts_get_custom_config_i32("dummy.rand_seed");
  if (rand_seed < 0) rand_seed = u2hts_get_timestamp();
  srand(rand_seed);
  return true;
}

static inline uint8_t random_tp_count() { return rand() % 10; }
static inline void random_tp(u2hts_tp* tp) {
  tp->contact = true;
  tp->x = rand() % 4096;
  tp->y = rand() % 4096;
  tp->height = rand() % 256;
  tp->width = rand() % 256;
  tp->pressure = rand() % 256;
}

static bool dummy_coord_fetch(const u2hts_config* cfg,
                              u2hts_hid_report* report) {
  report->tp_count = random_tp_count();
  for (uint8_t i = 0; i < report->tp_count; i++) {
    report->tp[i].id = i;
    random_tp(&report->tp[i]);
    u2hts_transform_touch_data(cfg, &report->tp[i]);
  }
  return true;
}

static void dummy_get_config(u2hts_touch_controller_config* cfg) {
  cfg->max_tps = 10;
  cfg->x_max = 4095;
  cfg->y_max = 4095;
}

static u2hts_touch_controller_operations dummy_ops = {
    .setup = &dummy_setup,
    .fetch = &dummy_coord_fetch,
    .get_config = &dummy_get_config};

static u2hts_touch_controller dummy = {.name = "dummy",
                                       .irq_type = 0xFF,
                                       .report_mode = UTC_REPORT_MODE_CONTINOUS,
                                       .operations = &dummy_ops};
U2HTS_TOUCH_CONTROLLER(dummy);
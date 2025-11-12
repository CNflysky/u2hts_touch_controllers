/*
  Copyright (C) CNflysky.
  U2HTS stands for "USB to HID TouchScreen".
  gt9xx.c: touch driver for gt9xx touch controllers.
  This file is licensed under GPL V3.
  All rights reserved.
*/

#include "u2hts_core.h"

static bool gt9xx_setup();
static void gt9xx_coord_fetch(const u2hts_config* cfg,
                              u2hts_hid_report* report);
static u2hts_touch_controller_config gt9xx_get_config();

static u2hts_touch_controller_operations gt9xx_ops = {
    .setup = &gt9xx_setup,
    .fetch = &gt9xx_coord_fetch,
    .get_config = &gt9xx_get_config};

static u2hts_touch_controller gt9xx = {.name = "gt9xx",
                                       .i2c_addr = 0x5d,
                                       .alt_i2c_addr = 0x14,
                                       .irq_flag = U2HTS_IRQ_TYPE_FALLING,
                                       .operations = &gt9xx_ops};

U2HTS_TOUCH_CONTROLLER(gt9xx);

#define GT9XX_GT1X_CONFIG_START_REG 0x8050
#define GT9XX_GT9X_CONFIG_START_REG 0x8047
#define GT9XX_PRODUCT_INFO_START_REG 0x8140
#define GT9XX_STATUS_REG 0x814E
#define GT9XX_TP_DATA_START_REG 0x814F

static char gt9xx_product_id[5] = {0};

typedef struct __packed {
  uint8_t track_id;
  uint16_t x_coord;
  uint16_t y_coord;
  uint8_t point_size_w;
  uint8_t point_size_h;
  uint8_t reserved;
} gt9xx_tp_data;

typedef struct __packed {
  // too many config entries, for now we only concern about these 6 items...
  uint8_t cfgver;
  uint16_t x_max;
  uint16_t y_max;
  uint8_t max_tps;
} gt9xx_config;

inline static void gt9xx_i2c_read(uint16_t reg, void* data, size_t data_size) {
  u2hts_i2c_mem_read(gt9xx.i2c_addr, reg, sizeof(reg), data, data_size);
}

inline static void gt9xx_i2c_write(uint16_t reg, void* data, size_t data_size) {
  u2hts_i2c_mem_write(gt9xx.i2c_addr, reg, sizeof(reg), data, data_size);
}

inline static uint8_t gt9xx_read_byte(uint16_t reg) {
  uint8_t var = 0;
  gt9xx_i2c_read(reg, &var, sizeof(var));
  return var;
}

inline static void gt9xx_write_byte(uint16_t reg, uint8_t data) {
  gt9xx_i2c_write(reg, &data, sizeof(data));
}

static u2hts_touch_controller_config gt9xx_get_config() {
  gt9xx_config cfg = {0};
  uint16_t config_addr = 0x00;
  if (!strcmp(gt9xx_product_id, "5688"))
    config_addr = GT9XX_GT1X_CONFIG_START_REG;
  else if (!strcmp(gt9xx_product_id, "9271"))
    config_addr = GT9XX_GT9X_CONFIG_START_REG;
  else
    config_addr = GT9XX_GT9X_CONFIG_START_REG;
  gt9xx_i2c_read(config_addr, &cfg, sizeof(cfg));
  u2hts_touch_controller_config u2hts_tc_cfg = {
      .max_tps = cfg.max_tps, .x_max = cfg.x_max, .y_max = cfg.y_max};
  return u2hts_tc_cfg;
}

inline static void gt9xx_clear_irq() { gt9xx_write_byte(GT9XX_STATUS_REG, 0); }

static void gt9xx_coord_fetch(const u2hts_config* cfg,
                              u2hts_hid_report* report) {
  uint8_t tp_count = gt9xx_read_byte(GT9XX_STATUS_REG) & 0xF;
  gt9xx_clear_irq();
  if (tp_count == 0) return;
  tp_count = (tp_count < cfg->max_tps) ? tp_count : cfg->max_tps;
  report->tp_count = tp_count;
  gt9xx_tp_data tp_data[tp_count];
  gt9xx_i2c_read(GT9XX_TP_DATA_START_REG, tp_data, sizeof(tp_data));
  for (uint8_t i = 0; i < tp_count; i++) {
    report->tp[i].id = tp_data[i].track_id & 0xF;
    report->tp[i].contact = true;
    report->tp[i].x = tp_data[i].x_coord;
    report->tp[i].y = tp_data[i].y_coord;
    report->tp[i].width = tp_data[i].point_size_w;
    report->tp[i].height = tp_data[i].point_size_h;
    u2hts_apply_config_to_tp(cfg, &report->tp[i]);
  }
}

static bool gt9xx_setup() {
  u2hts_tprst_set(false);
  u2hts_delay_ms(20);
  u2hts_tpint_set(false);
  u2hts_delay_ms(100);
  u2hts_tprst_set(true);
  u2hts_delay_ms(5);

  // i2c addr should be 0x5d now.
  if (!u2hts_i2c_detect_slave(gt9xx.i2c_addr)) {
    if (u2hts_i2c_detect_slave(gt9xx.alt_i2c_addr))
      gt9xx.i2c_addr = gt9xx.alt_i2c_addr;
    else
      return false;
  }

  gt9xx_i2c_read(GT9XX_PRODUCT_INFO_START_REG, gt9xx_product_id,
                 sizeof(gt9xx_product_id));
  U2HTS_LOG_INFO("gt9xx i2c addr: 0x%x, product ID: %s", gt9xx.i2c_addr,
                 gt9xx_product_id);

  u2hts_delay_ms(100);
  gt9xx_clear_irq();
  return true;
}
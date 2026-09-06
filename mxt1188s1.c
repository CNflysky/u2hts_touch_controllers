/*
  U2HTS stands for "USB to HID TouchScreen".
  mxt1188s1.c: touch driver for Atmel maxTouch mxt1188s1 touch controllers.
  This file is licensed under GPL V3.
  All rights reserved.
*/

#include "u2hts_core.h"
#include <string.h>

static bool mxt1188s1_setup(U2HTS_BUS_TYPES bus_type);
static bool mxt1188s1_service(void);
static void mxt1188s1_get_config(u2hts_touch_controller_config* cfg);

static u2hts_touch_controller_operations mxt1188s1_ops = {
    .setup = &mxt1188s1_setup,
    .fetch = &mxt1188s1_service,
    .get_config = &mxt1188s1_get_config
};

static u2hts_touch_controller mxt1188s1 = {
    .name = "mxt1188s1",
    .irq_type = IRQ_TYPE_LEVEL_LOW,
    .report_mode = UTC_REPORT_MODE_EVENT,
    .i2c_config =
        {
            .primary_addr = 0x4a,
            .alt_addrs = (uint8_t[]){0x4b, 0},
            .speed_hz = 400 * 1000,
        },
    .operations = &mxt1188s1_ops
};

U2HTS_TOUCH_CONTROLLER(mxt1188s1);

#define MXT1188S1_I2C_ADDR mxt1188s1.i2c_config.primary_addr
#define MXT1188S1_FAMILY_ID 0xA2
#define MXT1188S1_VARIANT_ID 0x01

#define MXT1188S1_T9_STATUS_DETECT   (1 << 7)
#define MXT1188S1_T9_STATUS_PRESS    (1 << 6)
#define MXT1188S1_T9_STATUS_RELEASE  (1 << 5)
#define MXT1188S1_T9_STATUS_MOVE     (1 << 4)
#define MXT1188S1_T9_STATUS_VECTOR   (1 << 3)
#define MXT1188S1_T9_STATUS_AMP      (1 << 2)
#define MXT1188S1_T9_STATUS_SUPPRESS (1 << 1)
#define MXT1188S1_T9_STATUS_UNGRIP   (1 << 0)

inline static bool mxt1188s1_read(uint16_t addr, void* buf, size_t len) {
  uint8_t addr_buf[2] = { (uint8_t)(addr & 0xFF), (uint8_t)((addr >> 8) & 0xFF) };

  if (!buf || len == 0) {
    U2HTS_LOG_ERROR("%s invalid parameters, buf = %p, len = %zu", __func__, buf, len);
    return false;
  }

  // Send 16-bit address with re-start (stop = false)
  if (!u2hts_i2c_write(MXT1188S1_I2C_ADDR, addr_buf, sizeof(addr_buf), false)) {
    U2HTS_LOG_ERROR("%s write address error, addr = 0x%04x", __func__, addr);
    return false;
  }

  // Read data following restart
  if (!u2hts_i2c_read(MXT1188S1_I2C_ADDR, buf, len)) {
    U2HTS_LOG_ERROR("%s read error, addr = 0x%04x", __func__, addr);
    return false;
  }

  return true;
}

inline static bool mxt1188s1_write(uint16_t addr, const void* buf, size_t len) {
  uint8_t tx_buf[2 + len];
  tx_buf[0] = (uint8_t)(addr & 0xFF);
  tx_buf[1] = (uint8_t)((addr >> 8) & 0xFF);

  if (!buf || len == 0) {
    U2HTS_LOG_ERROR("%s invalid parameters, buf = %p, len = %zu", __func__, buf, len);
    return false;
  }

  memcpy(tx_buf + 2, buf, len);

  if (!u2hts_i2c_write(MXT1188S1_I2C_ADDR, tx_buf, 2 + len, true)) {
    U2HTS_LOG_ERROR("%s write error, addr = 0x%04x", __func__, addr);
    return false;
  }

  return true;
}

// Controller Information Block header (7 bytes)
typedef struct __packed {
  uint8_t family_id;
  uint8_t variant_id;
  uint8_t version;
  uint8_t build;
  uint8_t matrix_x_size;
  uint8_t matrix_y_size;
  uint8_t num_objects;
} mxt1188s1_information_block_t;

// Controller object table element (6 bytes)
typedef struct __packed {
  uint8_t  type;
  uint16_t start_address; // 16-bit little-endian
  uint8_t  size;          // size - 1
  uint8_t  instances;     // instances - 1
  uint8_t  num_report_ids;
} mxt1188s1_object_table_element_t;

// TOUCH_MULTITOUCHSCREEN_T9 message data (7 bytes payload following report_id)
typedef struct __packed {
  uint8_t status;       // Byte 1: Status bitmask
  uint8_t xposmsb;      // Byte 2: X position MSB
  uint8_t yposmsb;      // Byte 3: Y position MSB
  uint8_t xyposlsb;     // Byte 4: X LSB (bits 7:4), Y LSB (bits 3:0)
  uint8_t tcharea;      // Byte 5: Size of touch
  uint8_t tchamplitude; // Byte 6: Touch amplitude / pressure
  uint8_t tchvector;    // Byte 7: Touch vector
} mxt1188s1_report_t9_t;

// Driver internal state structure
typedef struct {
  uint16_t t9_address;
  uint8_t  t9_size;
  uint8_t  t9_instances;
  uint8_t  t9_report_id_start;
  uint8_t  t9_report_id_end;

  uint16_t t5_address;
  uint8_t  t5_size;

  uint16_t t44_address;
  uint8_t  t44_size;
} mxt1188s1_driver_info_t;

static mxt1188s1_driver_info_t mxt1188s1_driver;

static bool mxt1188s1_setup(U2HTS_BUS_TYPES bus_type) {
  U2HTS_UNUSED(bus_type);
  memset(&mxt1188s1_driver, 0, sizeof(mxt1188s1_driver));

  U2HTS_LOG_INFO("mXT1188S - Configuration for I2C (addr=0x%x, speed=%dkHz, bus=i2c%d, scl/sda=%d/%d)",
                 MXT1188S1_I2C_ADDR, mxt1188s1.i2c_config.speed_hz / 1000,
                PICO_DEFAULT_I2C, PICO_DEFAULT_I2C_SCL_PIN,
                PICO_DEFAULT_I2C_SDA_PIN);

  // Switch INT pin to input with pull-up so CHG (open-drain) can operate
  u2hts_tpint_set_mode(false /* input */, true /* pull-up */);

  // Hardware reset sequence.
  // mXT1188S1 signals boot completion by asserting CHG (INT pin) low.
  u2hts_tprst_set(false);
  u2hts_delay_ms(10);   // RST hold time
  u2hts_tprst_set(true);

  u2hts_delay_ms(300);
  U2HTS_LOG_INFO("mXT1188S - Chip has been reset using RST line.");

  // // Switch INT pin to input with pull-up so we can poll CHG (active-low open-drain)
  // u2hts_tpint_set_mode(false /* input */, true /* pull-up */);

  // // Wait for CHG (INT) to assert low, indicating device is ready.
  // // Datasheet specifies up to ~300ms boot time; use 500ms timeout.
  // {
  //   uint32_t timeout_ms = 500;
  //   while (u2hts_tpint_get() && timeout_ms--) {
  //     u2hts_delay_ms(1);
  //   }
  //   if (timeout_ms == 0) {
  //     U2HTS_LOG_WARN("mXT1188S1 CHG did not assert after reset, continuing anyway");
  //   }
  // }
  // u2hts_delay_ms(25);  // settling time after CHG asserts

  // Read Information block header from address 0x0000
  mxt1188s1_information_block_t info_block;
  if (!mxt1188s1_read(0, &info_block, sizeof(info_block))) {
    U2HTS_LOG_ERROR("%s failed to read information block header", __func__);
    return false;
  }

  uint8_t ver_major = (info_block.version >> 4) & 0x0F;
  uint8_t ver_minor = info_block.version & 0x0F;

  U2HTS_LOG_INFO("mXT1188S - Detected Family=0x%02X, Variant=0x%02X, Firmware Ver=%d.%d, Build=%d, Matrix X=%d, Y=%d, Objects=%d",
                 info_block.family_id, info_block.variant_id, ver_major, ver_minor,
                 info_block.build, info_block.matrix_x_size, info_block.matrix_y_size, info_block.num_objects);

  if (info_block.family_id != MXT1188S1_FAMILY_ID) {
    U2HTS_LOG_WARN("Unexpected Family ID 0x%02X (expected 0x%02X for mXT1188S)", info_block.family_id, MXT1188S1_FAMILY_ID);
  }

  if (info_block.variant_id != MXT1188S1_VARIANT_ID) {
    U2HTS_LOG_WARN("Unexpected Variant ID 0x%02X (expected 0x%02X for mXT1188S)", info_block.variant_id, MXT1188S1_VARIANT_ID);
  }

  // Read object table to locate T44, T9, T5 objects needed for processing touch reports.
  mxt1188s1_object_table_element_t object_table;
  uint8_t report_id_start = 1;
  for (uint8_t i = 0; i < info_block.num_objects; i++) {
    uint16_t addr = (uint16_t)(sizeof(mxt1188s1_information_block_t) + i * sizeof(mxt1188s1_object_table_element_t));
    if (!mxt1188s1_read(addr, &object_table, sizeof(object_table))) {
      U2HTS_LOG_ERROR("%s read error, addr = 0x%04x", __func__, addr);
      return false;
    }

    if (object_table.type == 9) {
      mxt1188s1_driver.t9_report_id_start = report_id_start;
      mxt1188s1_driver.t9_report_id_end = report_id_start + object_table.num_report_ids - 1;
      mxt1188s1_driver.t9_address = object_table.start_address;
      mxt1188s1_driver.t9_size = object_table.size + 1;
      mxt1188s1_driver.t9_instances = object_table.instances + 1;
      U2HTS_LOG_INFO("Found T9 object at 0x%04x, size = %d, instances = %d, report_ids = %d..%d",
                     mxt1188s1_driver.t9_address, mxt1188s1_driver.t9_size, mxt1188s1_driver.t9_instances,
                     mxt1188s1_driver.t9_report_id_start, mxt1188s1_driver.t9_report_id_end);
    } else if (object_table.type == 44) {
      mxt1188s1_driver.t44_address = object_table.start_address;
      mxt1188s1_driver.t44_size = object_table.size + 1;
      U2HTS_LOG_INFO("Found T44 (Message Count) object at 0x%04x, size = %d",
                     mxt1188s1_driver.t44_address, mxt1188s1_driver.t44_size);
    } else if (object_table.type == 5) {
      mxt1188s1_driver.t5_address = object_table.start_address;
      mxt1188s1_driver.t5_size = object_table.size + 1;
      U2HTS_LOG_INFO("Found T5 (Message Processor) object at 0x%04x, size = %d",
                     mxt1188s1_driver.t5_address, mxt1188s1_driver.t5_size);
    }

    report_id_start += object_table.num_report_ids * (object_table.instances + 1);
  }

  if (mxt1188s1_driver.t9_address == 0) {
    U2HTS_LOG_ERROR("%s T9 object not found", __func__);
    return false;
  }

  if (mxt1188s1_driver.t44_address == 0) {
    U2HTS_LOG_ERROR("%s T44 object not found", __func__);
    return false;
  }

  if (mxt1188s1_driver.t5_address == 0) {
    U2HTS_LOG_ERROR("%s T5 object not found", __func__);
    return false;
  }

  if (mxt1188s1_driver.t44_size != 1) {
    U2HTS_LOG_ERROR("%s T44 object size is not 1", __func__);
    return false;
  }

  U2HTS_LOG_INFO("mXT1188S1 - Finished configuration.");
  return true;
}

static void mxt1188s1_get_config(u2hts_touch_controller_config* cfg) {
  cfg->max_tps = U2HTS_MAX_TPS;
  cfg->x_max = U2HTS_LOGICAL_MAX;
  cfg->y_max = U2HTS_LOGICAL_MAX;
}

static int mxt1188s1_read_message_pending_count(void) {
  uint8_t count;
  if (!mxt1188s1_read(mxt1188s1_driver.t44_address, &count, 1)) {
    U2HTS_LOG_ERROR("%s failed to read message count", __func__);
    return -1;
  }
  return (int)count;
}

static bool mxt1188s1_service(void) {
  // Track which touch id is active.
  int8_t tp_slot[U2HTS_MAX_TPS];
  memset(tp_slot, -1, sizeof(tp_slot));
  int8_t next_slot = 0;

  while (true) {
    int message_count = mxt1188s1_read_message_pending_count();

    if (message_count < 0) {
      U2HTS_LOG_ERROR("%s aborting service loop", __func__);
      return false;
    }

    if (message_count == 0) {
      break;
    }

    // Read message from T5
    uint8_t buf[mxt1188s1_driver.t5_size];
    if (!mxt1188s1_read(mxt1188s1_driver.t5_address, buf, sizeof(buf))) {
      U2HTS_LOG_ERROR("%s failed to read message", __func__);
      return false;
    }

    uint8_t report_id = buf[0];
    if (report_id >= mxt1188s1_driver.t9_report_id_start && report_id <= mxt1188s1_driver.t9_report_id_end) {
      // Received T9 report, update slot state
      mxt1188s1_report_t9_t *report = (mxt1188s1_report_t9_t *)(buf + 1);
      
      uint8_t touch_id = report_id - mxt1188s1_driver.t9_report_id_start;
      int8_t slot = tp_slot[touch_id];
      if (slot == -1) {
        slot = next_slot++;
        if (slot >= U2HTS_MAX_TPS) {
          U2HTS_LOG_WARN("Too many touch points, dropping touch %d", touch_id);
          continue;
        }
        tp_slot[touch_id] = slot;
      }

      bool fDetect  = (report->status & MXT1188S1_T9_STATUS_DETECT);
      bool fPress   = (report->status & MXT1188S1_T9_STATUS_PRESS);
      bool fRelease = (report->status & MXT1188S1_T9_STATUS_RELEASE);
      bool fMove    = (report->status & MXT1188S1_T9_STATUS_MOVE);
      U2HTS_UNUSED(fPress);
      U2HTS_UNUSED(fRelease);
      U2HTS_UNUSED(fMove);

      uint16_t x = ((uint16_t)report->xposmsb << 4) | ((report->xyposlsb >> 4) & 0x0F);
      uint16_t y = ((uint16_t)report->yposmsb << 4) | (report->xyposlsb & 0x0F);
            
      u2hts_set_tp(slot, fDetect, touch_id, x, y, report->tcharea, report->tcharea, report->tchamplitude);      
    }
  }

  uint8_t tp_count = 0;
  for (int8_t i = 0; i < U2HTS_MAX_TPS; i++) {
    if (tp_slot[i] != -1) {
      tp_count++;
    }
  }

  if (tp_count == 0) return false;

  u2hts_set_tp_count(tp_count);
  return true;
}

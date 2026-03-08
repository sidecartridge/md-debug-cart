/**
 * File: constants.h
 * Author: Diego Parrilla Santamaría
 * Date: November 2024, February 2026
 * Copyright: 2025-2026 - GOODDATA LABS SL
 * Description: Constants used in the placeholder file
 */

#ifndef CONSTANTS_H
#define CONSTANTS_H

#include "hardware/vreg.h"

// SELECT signal
#define SELECT_GPIO 5  // GPIO signal for SELECT

// GPIO constants for the read address from the bus
#define READ_ADDR_GPIO_BASE 6     // Start of the GPIOs for the address
#define READ_ADDR_PIN_COUNT 16    // Number of GPIOs for the address
#define READ_SIGNAL_GPIO_BASE 27  // GPIO signal for READ
#define READ_SIGNAL_PIN_COUNT 1   // Number of GPIOs for the READ signal
#define ROM3_GPIO 26              // GPIO signal for ROM3 accesses

// FLASH and RAM sections constants.
#define ROM_BANKS 2  // Number of ROM banks to emulate
#define ROM_SIZE_BYTES 0x10000                   // 64KBytes

// Frequency constants.
#define RP2040_CLOCK_FREQ_KHZ 225000  // Clock frequency in KHz (225MHz).

// Voltage constants.
#define RP2040_VOLTAGE VREG_VOLTAGE_1_10  // Voltage in 1.10 Volts.
#define VOLTAGE_VALUES                                                 \
  (const char *[]){"NOT VALID", "NOT VALID", "NOT VALID", "NOT VALID", \
                   "NOT VALID", "NOT VALID", "0.85v",     "0.90v",     \
                   "0.95v",     "1.00v",     "1.05v",     "1.10v",     \
                   "1.15v",     "1.20v",     "1.25v",     "1.30v",     \
                   "NOT VALID", "NOT VALID", "NOT VALID", "NOT VALID", \
                   "NOT VALID"}

// This is the APP KEY that will be used to identify the current app
// It mmust be a unique UUID4 for each app, and must be the one used in the
// app.json file as descriptor of the app
#ifndef CURRENT_APP_UUID_KEY
#define CURRENT_APP_UUID_KEY "PLACEHOLDER"
#endif

// NOLINTBEGIN(readability-identifier-naming)
extern unsigned int __flash_binary_start;
extern unsigned int _booster_app_flash_start;
extern unsigned int _config_flash_start;
extern unsigned int _global_lookup_flash_start;
extern unsigned int _global_config_flash_start;
extern unsigned int __rom_in_ram_start__;
// NOLINTEND(readability-identifier-naming)

#endif  // CONSTANTS_H

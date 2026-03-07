/**
 * File: emul.c
 * Author: Diego Parrilla Santamaría
 * Date: February 2025, February 2026
 * Copyright: 2025-2026 - GOODDATA LABS
 * Description: Debug cart emulation – minimal menu + address-bus capture
 */

#include "emul.h"

#include <stdint.h>
#include <stdio.h>

// included in the C file to avoid multiple definitions
#include "target_firmware.h"  // Include the target firmware binary

#include "aconfig.h"
#include "constants.h"
#include "debug.h"
#include "display.h"
#include "hardware/dma.h"
#include "hardware/pio.h"
#include "memfunc.h"
#include "pico/stdlib.h"
#include "reset.h"
#include "romemul.h"
#include "select.h"
#include "term.h"

#define SLEEP_LOOP_MS 100
#define MENU_DISPLAY_MS (SLEEP_LOOP_MS * 10)  // ~1 s to let the Atari show the menu

enum {
  APP_MODE_SETUP = 255  // Setup
};

// ---------------------------------------------------------------------------
// Debug-cart capture configuration
// Use pio1 to avoid conflicts with the ROM-emulator programs on pio0.
// ---------------------------------------------------------------------------
#define DEBUG_PIO pio1
#define DEBUG_NUM_PIO_INSTR 3u

// Ring buffer: 32 KiB, must be power-of-two aligned
#define DEBUG_RING_BITS 15u
#define DEBUG_RING_SIZE (1ul << DEBUG_RING_BITS)

// Maximum DMA transfer count (effectively infinite loop via ring)
#define DEBUG_DMA_SIZE (0xFFFFFFFFul)

static uint8_t debugBuffer[DEBUG_RING_SIZE]
    __attribute__((aligned(DEBUG_RING_SIZE)));
static int debugDmaChannel = -1;
static int debugSm = -1;

// ---------------------------------------------------------------------------
// SELECT button flags – set on core1, acted upon on core0
// ---------------------------------------------------------------------------
static volatile bool selectBoosterRequested = false;
static volatile bool selectResetRequested = false;

static void onShortSelect(void) { selectBoosterRequested = true; }
static void onLongSelect(void) { selectResetRequested = true; }

// ---------------------------------------------------------------------------
// Display helpers
// ---------------------------------------------------------------------------
static void showTitle(void) {
  term_printString("\x1B"
                   "E"
                   "Debug Cart Mode - " RELEASE_VERSION "\n");
}

static void menu(void) {
  showTitle();
  term_printString("\n\n");
  term_printString("Booting with debug cart enabled.\n\n");
  term_printString("SHORT SELECT: Jump to Booster\n");
  term_printString("LONG SELECT:  Reset device\n");
  display_refresh();
}

static void preinit(void) {
  term_init();
  term_clearScreen();
  showTitle();
  term_printString("\n\n");
  term_printString("Starting debug cart mode...\n");
  display_refresh();
}

void failure(const char *message) {
  term_init();
  term_clearScreen();
  showTitle();
  term_printString("\n\n");
  term_printString(message);
  display_refresh();
}

// ---------------------------------------------------------------------------
// Debug-cart hardware / PIO / DMA initialisation
// (Based on https://github.com/czietz/atari-debug-cart/blob/master/debug.c)
// ---------------------------------------------------------------------------

static void debugcart_init_hardware(void) {
  // Drive READ signal low to enable the external address-bus buffer
  gpio_init(READ_SIGNAL_GPIO_BASE);
  gpio_set_dir(READ_SIGNAL_GPIO_BASE, GPIO_OUT);
  gpio_put(READ_SIGNAL_GPIO_BASE, false);

  // ROM3 signal: input with pull-up, connected to the chosen PIO block
  pio_gpio_init(DEBUG_PIO, ROM3_GPIO);
  gpio_set_dir(ROM3_GPIO, GPIO_IN);
  gpio_set_pulls(ROM3_GPIO, true, false);
  gpio_pull_up(ROM3_GPIO);
}

static void debugcart_init_pio(void) {
  // Build the 3-instruction PIO program on-the-fly
  uint16_t pio_asm[DEBUG_NUM_PIO_INSTR];

  // 1. Wait for ROM3 high; add small delay to avoid glitches
  pio_asm[0] = pio_encode_wait_gpio(1, ROM3_GPIO) | pio_encode_delay(4);
  // 2. Wait for ROM3 low  (= ROM3 access by the Atari)
  pio_asm[1] = pio_encode_wait_gpio(0, ROM3_GPIO);
  // 3. Sample 8 address bits A1-A8 (the debug byte)
  pio_asm[2] = pio_encode_in(pio_pins, 8);

  struct pio_program pio_prog = {
      .instructions = &pio_asm[0],
      .length = DEBUG_NUM_PIO_INSTR,
      .origin = -1,
  };

  uint offset = pio_add_program(DEBUG_PIO, &pio_prog);
  debugSm = pio_claim_unused_sm(DEBUG_PIO, true);

  pio_sm_config c = pio_get_default_sm_config();
  // Address pin base: READ_ADDR_GPIO_BASE is A1 on the SidecarTridge board
  sm_config_set_in_pins(&c, READ_ADDR_GPIO_BASE);
  sm_config_set_wrap(&c, offset, offset + DEBUG_NUM_PIO_INSTR - 1);
  sm_config_set_clkdiv(&c, 1);
  // Right-shift, autopush after 8 bits; data lands in MSB of the FIFO word
  sm_config_set_in_shift(&c, true, true, 8);
  sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_RX);
  pio_sm_init(DEBUG_PIO, debugSm, offset, &c);
}

static void debugcart_init_dma(void) {
  // Stop & flush the SM before wiring up DMA
  pio_sm_set_enabled(DEBUG_PIO, debugSm, false);
  pio_sm_clear_fifos(DEBUG_PIO, debugSm);
  pio_sm_restart(DEBUG_PIO, debugSm);

  debugDmaChannel = dma_claim_unused_channel(true);

  dma_channel_config c = dma_channel_get_default_config(debugDmaChannel);
  channel_config_set_read_increment(&c, false);
  channel_config_set_write_increment(&c, true);
  channel_config_set_transfer_data_size(&c, DMA_SIZE_8);
  channel_config_set_ring(&c, true, DEBUG_RING_BITS);  // ring on write side
  channel_config_set_dreq(&c, pio_get_dreq(DEBUG_PIO, debugSm, false));

  // Source: MSB byte of the PIO RX FIFO.
  // With right-shift auto-push, the 8-bit sample occupies bits 31:24 of the
  // 32-bit FIFO word.  Adding +3 to the byte address reaches that MSB byte.
  dma_channel_configure(debugDmaChannel, &c,
                        debugBuffer,
                        (uint8_t *)(&DEBUG_PIO->rxf[debugSm]) + 3,  // MSB byte
                        DEBUG_DMA_SIZE,
                        true /* start immediately */);

  // Start the SM – it will wait for the first ROM3 falling edge
  pio_sm_set_enabled(DEBUG_PIO, debugSm, true);
}

// ---------------------------------------------------------------------------
// Main entry point
// ---------------------------------------------------------------------------
void emul_start(void) {
  // 1. Copy the terminal firmware to RAM so the Atari can display our message
  COPY_FIRMWARE_TO_RAM((uint16_t *)target_firmware, target_firmware_length);

  // 2. Initialise the ROM-emulator PIO / DMA (needed for the terminal display)
  init_romemul(NULL, term_dma_irq_handler_lookup, false);

  // 3. Set up the display subsystem
  display_setupU8g2();

  // 4. Show the "please wait" splash, then the minimal debug-cart menu
  preinit();
  menu();

  // 5. Configure and arm the SELECT button watcher on core1
  select_configure();
  select_coreWaitPush(onShortSelect, onLongSelect);

// 6. Blink on
#ifdef BLINK_H
  blink_on();
#endif

  // 7. Let the Atari display the message for a moment before we switch modes
  sleep_ms(MENU_DISPLAY_MS);

  // 8. Disable the ROM-emulator DMA/IRQ/PIO cleanly before starting debug-cart
  deinit_romemul();

  // 9. Bring up the debug-cart capture pipeline
  debugcart_init_hardware();
  debugcart_init_pio();
  debugcart_init_dma();

  // 10. Initialise USB-serial output so captured bytes reach the host
  stdio_init_all();

  // 11. Debug-capture loop
  DPRINTF("Debug cart active – capturing ROM3 address-bus data.\n");
  uint32_t readidx = 0;

  while (1) {
    // Handle SELECT-button requests (flags written by core1 callbacks)
    if (selectBoosterRequested) {
      select_coreWaitPushDisable();
      settings_put_integer(aconfig_getContext(), ACONFIG_PARAM_MODE,
                           APP_MODE_SETUP);
      settings_save(aconfig_getContext(), true);
      reset_jump_to_booster();
    }
    if (selectResetRequested) {
      select_coreWaitPushDisable();
      reset_device();
    }

    // Forward any newly captured bytes to the USB-serial host
    uint32_t writeidx =
        (DEBUG_DMA_SIZE - dma_hw->ch[debugDmaChannel].transfer_count) %
        DEBUG_RING_SIZE;

    if (writeidx > readidx) {
      fwrite(&debugBuffer[readidx], 1, writeidx - readidx, stdout);
      fflush(stdout);
      readidx = writeidx;
    } else if (writeidx < readidx) {
      fwrite(&debugBuffer[readidx], 1, DEBUG_RING_SIZE - readidx, stdout);
      fwrite(&debugBuffer[0], 1, writeidx, stdout);
      fflush(stdout);
      readidx = writeidx;
    }

    sleep_ms(SLEEP_LOOP_MS);
  }
}


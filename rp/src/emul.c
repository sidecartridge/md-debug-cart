/**
 * File: emul.c
 * Author: Diego Parrilla Santamaría
 * Date: February 2025, February 2026
 * Copyright: 2025-2026 - GOODDATA LABS
 * Description: Debug cart emulation - minimal menu + address-bus capture
 * Debug-cart hardware / PIO / DMA initialisation is based on:
 * https://github.com/czietz/atari-debug-cart
 */

#include "emul.h"

#include <stdint.h>
#include <stdio.h>

#include "aconfig.h"
#include "blink.h"
#include "constants.h"
#include "debug.h"
#include "hardware/dma.h"
#include "hardware/pio.h"
#include "pico/stdlib.h"
#include "reset.h"

#define DMA_ACTIVITY_BLINK_MS 20
#define SELECT_DEBOUNCE_DELAY 20
#define SELECT_LONG_RESET 10000

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
// SELECT button state machine - polled and acted upon on core0
// ---------------------------------------------------------------------------
static volatile bool selectBoosterRequested = false;
static volatile bool selectResetRequested = false;
static bool selectRawState = false;
static bool selectStableState = false;
static bool selectDebounceActive = false;
static absolute_time_t selectDebounceDeadline;
static absolute_time_t selectPressStart;
static bool dmaActivityBlinkActive = false;
static absolute_time_t dmaActivityBlinkDeadline;

static void emit_dma_activity_blink(void) {
  blink_on();
  dmaActivityBlinkActive = true;
  dmaActivityBlinkDeadline = make_timeout_time_ms(DMA_ACTIVITY_BLINK_MS);
}

static void service_dma_activity_blink(void) {
  if (dmaActivityBlinkActive &&
      absolute_time_diff_us(get_absolute_time(), dmaActivityBlinkDeadline) <=
          0) {
    blink_off();
    dmaActivityBlinkActive = false;
  }
}

static bool select_detect_push(void) { return gpio_get(SELECT_GPIO) != 0; }

static void select_configure_local(void) {
  gpio_init(SELECT_GPIO);
  gpio_set_dir(SELECT_GPIO, GPIO_IN);
  gpio_set_pulls(SELECT_GPIO, false, true);
  gpio_pull_down(SELECT_GPIO);
}

static void select_poll(void) {
  bool rawState = select_detect_push();

  if (rawState != selectRawState) {
    selectRawState = rawState;
    selectDebounceActive = true;
    selectDebounceDeadline = make_timeout_time_ms(SELECT_DEBOUNCE_DELAY);
    return;
  }

  if (!selectDebounceActive ||
      absolute_time_diff_us(get_absolute_time(), selectDebounceDeadline) > 0) {
    return;
  }

  selectDebounceActive = false;
  if (selectStableState == rawState) {
    return;
  }

  selectStableState = rawState;
  if (selectStableState) {
    DPRINTF("SELECT button pushed\n");
    selectPressStart = get_absolute_time();
    return;
  }

  uint32_t pressDuration = to_ms_since_boot(get_absolute_time()) -
                           to_ms_since_boot(selectPressStart);
  DPRINTF("SELECT button released after %lu ms\n",
          (unsigned long)pressDuration);
  if (pressDuration >= SELECT_LONG_RESET) {
    DPRINTF("Long press detected. Requesting reset\n");
    selectResetRequested = true;
  } else {
    DPRINTF("Short press detected. Requesting booster jump\n");
    selectBoosterRequested = true;
  }
}

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
  // GPIO6 is A0 on this board, so start at A1 for debug-byte capture.
  sm_config_set_in_pins(&c, READ_ADDR_GPIO_BASE + 1);
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
  dma_channel_configure(debugDmaChannel, &c, debugBuffer,
                        (uint8_t *)(&DEBUG_PIO->rxf[debugSm]) + 3,  // MSB byte
                        DEBUG_DMA_SIZE, true /* start immediately */);

  // Start the SM - it will wait for the first ROM3 falling edge
  pio_sm_set_enabled(DEBUG_PIO, debugSm, true);
}

// ---------------------------------------------------------------------------
// Main entry point
// ---------------------------------------------------------------------------
void emul_start(void) {
  stdio_init_all();
  DPRINTF("Debug Cart Mode - %s\n", RELEASE_VERSION);
  DPRINTF("Starting debug cart capture\n");
  DPRINTF("SHORT SELECT: Jump to Booster\n");
  DPRINTF("LONG SELECT: Reset device\n");

  // Configure the SELECT button for same-core polling.
  select_configure_local();
  selectRawState = select_detect_push();
  selectStableState = selectRawState;
  selectDebounceActive = false;

  blink_off();

  // Bring up the debug-cart capture pipeline
  debugcart_init_hardware();
  debugcart_init_pio();
  debugcart_init_dma();

  DPRINTF("Debug cart active - capturing ROM3 address-bus data.\n");
  uint32_t readidx = 0;

  while (1) {
    select_poll();

    // Handle SELECT-button requests
    if (selectBoosterRequested) {
      reset_jump_to_booster();
    }
    if (selectResetRequested) {
      reset_device();
    }

    // Forward any newly captured bytes to the USB-serial host
    uint32_t writeidx =
        (DEBUG_DMA_SIZE - dma_hw->ch[debugDmaChannel].transfer_count) %
        DEBUG_RING_SIZE;

    if (writeidx > readidx) {
      emit_dma_activity_blink();
      fwrite(&debugBuffer[readidx], 1, writeidx - readidx, stdout);
      fflush(stdout);
      readidx = writeidx;
    } else if (writeidx < readidx) {
      emit_dma_activity_blink();
      fwrite(&debugBuffer[readidx], 1, DEBUG_RING_SIZE - readidx, stdout);
      fwrite(&debugBuffer[0], 1, writeidx, stdout);
      fflush(stdout);
      readidx = writeidx;
    }

    service_dma_activity_blink();
    tight_loop_contents();
  }
}

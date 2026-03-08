#include "reset.h"

void reset_device() {
  DPRINTF("Resetting the device\n");

  save_and_disable_interrupts();
  // watchdog_enable(RESET_WATCHDOG_TIMEOUT, 0);
  watchdog_reboot(0, 0, RESET_WATCHDOG_TIMEOUT);
  // 20 ms timeout, for example, then the chip will reset
  while (1) {
    // Wait for the reset
    DPRINTF("Waiting for the device to reset\n");
  }
  DPRINTF("You should never reach this point\n");
}

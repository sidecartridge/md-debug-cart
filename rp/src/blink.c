#include "blink.h"

#ifdef CYW43_WL_GPIO_LED_PIN
static bool cyw43LedReady = false;

static void blink_initWifiLed(void) {
  if (!cyw43LedReady && cyw43_arch_init() == 0) {
    cyw43LedReady = true;
  }
}
#endif

void blink_on() {
#if defined(CYW43_WL_GPIO_LED_PIN)
  blink_initWifiLed();
  if (cyw43LedReady) {
    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 1);
  }
#else
  gpio_put(PICO_DEFAULT_LED_PIN, 1);
#endif
}

void blink_off() {
#if defined(CYW43_WL_GPIO_LED_PIN)
  blink_initWifiLed();
  if (cyw43LedReady) {
    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 0);
  }
#else
  gpio_put(PICO_DEFAULT_LED_PIN, 0);
#endif
}

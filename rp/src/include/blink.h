/**
 * File: blink.h
 * Author: Diego Parrilla Santamaría
 * Date: November 2024, February 2026
 * Copyright: 2024-2026 - GOODDATA LABS SL
 * Description: Header file for the blinking functions
 */

#ifndef BLINK_H
#define BLINK_H

#include "constants.h"
#include "debug.h"
#include "hardware/clocks.h"
#include "pico/stdlib.h"

#ifdef CYW43_WL_GPIO_LED_PIN
#include "pico/cyw43_arch.h"
#endif

#define CHARACTER_GAP_MS 700

/**
 * @brief   Blinks an LED to represent a given character in Morse code.
 *
 * @param   chr  The character to blink in Morse code.
 *
 * @details This function searches for the provided character in the
 *          `morseAlphabet` structure array to get its Morse code
 * representation. If found, it then blinks an LED in the pattern of dots and
 * dashes corresponding to the Morse code of the character. The LED blinks are
 *          separated by time intervals defined by constants such as
 * DOT_DURATION_MS, DASH_DURATION_MS, SYMBOL_GAP_MS, and CHARACTER_GAP_MS.
 *
 * @return  void
 */
/**
 * @brief Turns off the LED.
 *
 * This function turns off the LED by setting the appropriate GPIO pin to low.
 * It checks if the CYW43_WL_GPIO_LED_PIN is defined and uses it if available.
 * Otherwise, it defaults to using the PICO_DEFAULT_LED_PIN.
 */
void blink_off();

/**
 * @brief Turns on the LED.
 *
 * This function turns on the LED by setting the appropriate GPIO pin high.
 * It checks if the `CYW43_WL_GPIO_LED_PIN` is defined and uses it to set the
 * GPIO pin. If not defined, it defaults to using `PICO_DEFAULT_LED_PIN`.
 */
void blink_on();

#endif  // BLINK_H

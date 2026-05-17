#pragma once
/**
 * Minimal Arduino/AVR stub for native (desktop) compilation.
 * Replaces AVR PROGMEM macros and Arduino types with host equivalents.
 */

#include <string>

// PROGMEM / pgm_read — on desktop, data is already in RAM
#define PROGMEM
#define pgm_read_byte(addr) (*reinterpret_cast<const uint8_t*>(addr))

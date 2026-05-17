#pragma once
/**
 * Minimal Arduino/AVR stub for native (desktop) compilation.
 * Replaces AVR PROGMEM macros and Arduino types with host equivalents.
 */

#include <stdint.h>
#include <string>

// Arduino primitive types
using byte    = uint8_t;
using boolean = bool;

// PROGMEM / pgm_read — on desktop, data is already in RAM
#define PROGMEM
#define pgm_read_byte(addr) (*reinterpret_cast<const uint8_t*>(addr))

// Arduino String — thin wrapper around std::string
class String : public std::string {
public:
    String() = default;
    explicit String(const char* s) : std::string(s) {}
    String& operator+=(char c) { push_back(c); return *this; }
};


#include <Arduino.h>
#include <TinyGPSPlus.h>
#include <TimeZoneInfo.h>
#include <Brussels.h>
#include <Time.h>
#include <util/atomic.h>

TinyGPSPlus gps;
TimeZoneInfo timeZoneInfo;
// h10, h1, m10, m1, s10, s1
volatile uint8_t displayDigits[6] = {0, 0, 0, 0, 0, 0};
// h10, h1, m10, m1, s10, s1
constexpr uint8_t TUBE_PINS[6] = {
    PIN_PD5,
    PIN_PD3,
    PIN_PD0,
    PIN_PC2,
    PIN_PA7,
    PIN_PA5
};
// 0,1, ... , 9
constexpr uint8_t DIGIT_PINS[10] = {
    PIN_PA4,
    PIN_PD7,
    PIN_PD6,
    PIN_PD4,
    PIN_PD1,
    PIN_PC3,
    PIN_PC1,
    PIN_PC0,
    PIN_PA6,
    PIN_PA3
};
constexpr uint8_t COLON_PIN = PIN_PD2;

// Bitmasks for all output pins per port
// PORT_A: PA3(DIGIT9), PA4(DIGIT0), PA5(TUBE5), PA6(DIGIT8), PA7(TUBE4)
constexpr uint8_t PORT_A_OUTPUT_MASK = PIN3_bm | PIN4_bm | PIN5_bm | PIN6_bm | PIN7_bm;
// PORT_C: PC0(DIGIT7), PC1(DIGIT6), PC2(TUBE3), PC3(DIGIT5)
constexpr uint8_t PORT_C_OUTPUT_MASK = PIN0_bm | PIN1_bm | PIN2_bm | PIN3_bm;
// PORT_D: PD0(TUBE2), PD1(DIGIT4), PD3(TUBE1), PD4(DIGIT3), PD5(TUBE0), PD6(DIGIT2), PD7(DIGIT1)
constexpr uint8_t PORT_D_OUTPUT_MASK = PIN0_bm | PIN1_bm | PIN3_bm | PIN4_bm | PIN5_bm | PIN6_bm | PIN7_bm;

volatile bool ppsReceived = false;

volatile uint8_t activeTube = 0;
volatile bool blankPhase    = true;

constexpr uint16_t SLOT_FRAME_MS   = 30;
constexpr uint8_t SLOT_CYCLES      = 3;
constexpr uint32_t SLOT_INTERVAL_S = 3600UL;
uint32_t ppsSecondCounter          = 0;
uint32_t lastSlotRunSecond         = 0;

void runSlotMachine() {
    for (uint8_t cycle = 0; cycle < SLOT_CYCLES; cycle++) {
        for (uint8_t digit = 0; digit < 10; digit++) {
            for (uint8_t tube = 0; tube < 6; tube++) { // NOLINT(*-loop-convert)
                displayDigits[tube] = digit;
            }
            delay(SLOT_FRAME_MS);
        }
    }
}

void blankAllDigits() {
    // Clear all output pins in one write per port — atomic and instantaneous
    PORTA.OUTCLR = PORT_A_OUTPUT_MASK;
    PORTC.OUTCLR = PORT_C_OUTPUT_MASK;
    PORTD.OUTCLR = PORT_D_OUTPUT_MASK;
}

ISR(TCB0_INT_vect) {
    blankAllDigits();

    if (!blankPhase) {
        const uint8_t value = displayDigits[activeTube];
        digitalWrite(TUBE_PINS[activeTube], HIGH); // anode first
        digitalWrite(DIGIT_PINS[value], HIGH); // then cathode

        activeTube++;
        if (activeTube >= 6) {
            activeTube = 0;
        }
    }

    blankPhase = !blankPhase;

    TCB0.INTFLAGS = TCB_CAPT_bm;
}

ISR(PORTA_PORT_vect) {
    if (PORTA.INTFLAGS & PIN2_bm) {
        ppsReceived    = true;
        PORTA.INTFLAGS = PIN2_bm;
    }
}

void processGps() {
    while (Serial.available()) {
        const int c = Serial.read();
        gps.encode(static_cast<char>(c));
    }

    if (gps.date.isValid() && gps.time.isValid() && gps.time.isUpdated()) {
#ifdef DEBUG
        Serial2.print(F("GPS date: "));
        Serial2.print(gps.date.year());
        Serial2.print(F("-"));
        Serial2.print(gps.date.month());
        Serial2.print(F("-"));
        Serial2.println(gps.date.day());
        Serial2.print(F("GPS time: "));
        Serial2.print(gps.time.hour());
        Serial2.print(F(":"));
        Serial2.print(gps.time.minute());
        Serial2.print(F(":"));
        Serial2.print(gps.time.second());
        Serial2.print(F("."));
        Serial2.println(gps.time.centisecond());
#endif
        tm utc       = {};
        utc.tm_year  = static_cast<int>(gps.date.year()) - 1900;
        utc.tm_mon   = static_cast<int8_t>(gps.date.month() - 1u);
        utc.tm_mday  = static_cast<int8_t>(gps.date.day());
        utc.tm_hour  = static_cast<int8_t>(gps.time.hour());
        utc.tm_min   = static_cast<int8_t>(gps.time.minute());
        utc.tm_sec   = static_cast<int8_t>(gps.time.second());
        utc.tm_isdst = 0;

        const time_t utcTime = mktime(&utc);
        if (utcTime == static_cast<time_t>(-1)) {
            return; // mktime failed
        }

        const auto utcTimestamp = static_cast<int32_t>(utcTime);
        // breaks in 11 years.  That is the end of Burssels.h
        const auto localTime    = static_cast<time_t>(timeZoneInfo.utc2local(utcTimestamp));
        tm local                = {};
        gmtime_r(&localTime, &local);
        ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
            displayDigits[0] = local.tm_hour / 10;
            displayDigits[1] = local.tm_hour % 10;
            displayDigits[2] = local.tm_min / 10;
            displayDigits[3] = local.tm_min % 10;
            displayDigits[4] = local.tm_sec / 10;
            displayDigits[5] = local.tm_sec % 10;
        }
    }
}

void initGps() {
    timeZoneInfo.setLocation_P(Brussels);
    Serial2.println(F("Initializing GPS module with custom configuration..."));
    delay(500);

    // Disable GSV (Satellite list)
    Serial.println("$PUBX,40,GSV,0,0,0,0,0,0*59");
    // Disable GSA (DOP/Active Satellites)
    Serial.println("$PUBX,40,GSA,0,0,0,0,0,0*4E");
    // Disable VTG (Track/Speed)
    Serial.println("$PUBX,40,VTG,0,0,0,0,0,0*5E");
    // Disable GLL (Geographic Position)
    Serial.println("$PUBX,40,GLL,0,0,0,0,0,0*5C");

    Serial.flush();

    Serial2.println(F("GPS initialized with custom configuration"));
}

void initPins() {
    for (const auto pin : DIGIT_PINS) {
        pinMode(pin, OUTPUT);
        digitalWrite(pin, LOW);
    }
    for (const auto pin : TUBE_PINS) {
        pinMode(pin, OUTPUT);
        digitalWrite(pin, LOW);
    }
    pinMode(COLON_PIN, OUTPUT);
    digitalWrite(COLON_PIN, LOW);
}

void initTCB0Mux() {
    TCB0.CTRLA    = 0;
    TCB0.CTRLB    = TCB_CNTMODE_INT_gc;
    TCB0.CCMP     = 7999; // if I want it brighter lower this value.  Ie 4999
    TCB0.INTFLAGS = TCB_CAPT_bm;
    TCB0.INTCTRL  = TCB_CAPT_bm;
    TCB0.CTRLA    = TCB_CLKSEL_CLKDIV2_gc | TCB_ENABLE_bm;
}

void initPPSInterrupt() {
    pinMode(PIN_PA2, INPUT);

    // Set PA2 to trigger an interrupt on rising edge (going high)
    PORTA.PIN2CTRL = PORT_ISC_RISING_gc;
}

void setup() {
    Serial.begin(9600);
    Serial2.begin(115200);

    initPins();
    initTCB0Mux();
    initPPSInterrupt();

    initGps();
}

void loop() {
    processGps();

    //This runs a 'blocking' routine to cycle through all digits to prevent cathode poisoning.
    //The 'blocking' comes from the use of delay in the runSlotMachine method.
    //Since this is 'blocking', flush the Serial buffer because it might have overflown.
    if (ppsSecondCounter > 0 && (ppsSecondCounter - lastSlotRunSecond) >= SLOT_INTERVAL_S) {
        runSlotMachine();
        lastSlotRunSecond = ppsSecondCounter;
        while (Serial.available()) Serial.read(); // flush stale bytes
    }

    if (ppsReceived) {
        ppsSecondCounter++;
        digitalWriteFast(COLON_PIN, !digitalReadFast(COLON_PIN));
        ppsReceived = false;
    }
}

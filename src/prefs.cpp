#include <Arduino.h>
#include "UTCOffsets.h"
#include "prefs.h"

#if defined(ESP32)
#include <Preferences.h> // native ESP32 / ESP8266 shymanskyy
#else
#include <EEPROMPreferences.h> // provides `Preferences` on AVR
#endif

Preferences prefs; // same code on every platform

extern bool mode_12hour;
extern uint8_t utcOffsetIndex;
extern uint8_t display_brightness;
extern bool flipped_display;

void loadPreferences()
{ // load stored values
  if (prefs.begin("prefs", true))
  {
    mode_12hour = prefs.getBool("m12", DEFAULT_M12_MODE);    // default = 24 hour mode
    utcOffsetIndex = prefs.getUShort("uo", DEFAULT_OFFSET);    // default = -360 minutes
    display_brightness = prefs.getUShort("db", DEFAULT_BRIGHTNESS); // default = 5
    flipped_display = prefs.getBool("fd",DEFAULT_FLIPPED);
  }
  else
  {
    Serial.println("Invalid EEPROM found. Saving defaults.");

    prefs.begin("prefs");
    prefs.getBool("m12", DEFAULT_M12_MODE);    // default = 24 hour mode
    prefs.getUShort("uo", DEFAULT_OFFSET);    // default = -360 minutes
    prefs.getUShort("db", DEFAULT_BRIGHTNESS); // default = 5
    prefs.getBool("fd",DEFAULT_FLIPPED);
  }

  Serial.print("Preference values: ");
  Serial.print("Display mode - ");
  Serial.println(mode_12hour ? "12 hour" : "24 hour");
  Serial.print("UTC offset - ");
  Serial.println(utcOffsetsMinutes[utcOffsetIndex]);
  Serial.print("Display brightness - ");
  Serial.println(display_brightness);
  Serial.print("Flipped display - ");
  Serial.println(flipped_display ? "true" : "false");
  
  prefs.end();
}

void savePrefs(const char *name, bool m12, uint8_t uo, uint8_t db, bool fd)
{
  prefs.begin("prefs");
  prefs.putBool("m12", m12);
  prefs.putUShort("uo", uo);
  prefs.putUShort("db", db);
  prefs.putBool("fd",fd);
  prefs.end();
}
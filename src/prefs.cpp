#include <Arduino.h>
#include "UTCOffsets.h"

#if defined(ESP32)
#include <Preferences.h> // native ESP32 / ESP8266 shymanskyy
#else
#include <EEPROMPreferences.h> // provides `Preferences` on AVR
#endif

Preferences prefs; // same code on every platform

extern bool mode_12hour;
extern uint8_t utcOffsetIndex;
extern uint8_t displayBrightness;

void loadPreferences()
{ // load stored values
  if (prefs.begin("prefs", true))
  {
    mode_12hour = prefs.getBool("m12", false);    // default = 24 hour mode
    utcOffsetIndex = prefs.getUShort("uo", 7);    // default = -360 minutes
    displayBrightness = prefs.getUShort("db", 5); // default = 5
  }
  else
  {
    Serial.println("Invalid EEPROM found. Saving defaults.");

    prefs.begin("prefs");
    prefs.putBool("m12", false);
    prefs.putUShort("uo", 7);
    prefs.putUShort("db", 5);
  }

  Serial.print("Preference values: ");
  Serial.print("Display mode - ");
  Serial.println(mode_12hour ? "12 hour" : "24 hour");
  Serial.print("UTC offset - ");
  Serial.println(utcOffsetsMinutes[utcOffsetIndex]);
  Serial.print("Display brightness - ");
  Serial.println(displayBrightness);
  prefs.end();
}

void savePrefs(const char *name, bool m12, uint8_t uo, uint8_t db)
{
  prefs.begin("prefs");
  prefs.putBool("m12", m12);
  prefs.putUShort("uo", uo);
  prefs.putUShort("db", db);
  prefs.end();
}
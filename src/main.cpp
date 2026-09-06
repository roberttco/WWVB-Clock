/*
  WWVB RECEIVER DECODER ---- Reads the 1 pulse per second time code from WWVB
  Arduino UNO - - - L. Thomas - March 2024

  Input 6 senses the NOT output signal of the WWVB Receiver from Universal-Solder.
  Output 7 controls the PDN signal to the WWVB Receiver (enable/disable)
  Input 2 receives the 60Hz power line signal and functions as an interrupt
  A positive going edge on the NOT output signal kicks off decoding of the signal
    each second.  Following this event, the NOT output signal is examined 21 AC cycles
    later (350ms) to see if the signal is still HIGH (a 1) or if it is LOW (a 0).
    If it was a 1 the signal is checked again after a total of 42 AC cycles (700ms)
    to find out if it is still high and actually a Position Marker.
  The green LED is turned on whenever the output signal is HIGH.
  Uses 60Hz signal (pin 2, INT0) as interrupt for timing.
  Displays "P" when an 800ms Position Marker is detected (6 per minute).
  Displays "F" when 2 consecutive Position Markers are detected (1 per minute).
  Display a "0" or "1" for BCD data detection each second.
  Displays a "-" while waiting for a decoded result.
*/

#include "soc/gpio_struct.h"
#include <Time.h>
#include <TM1637TinyDisplay6.h>
#include "UTCOffsets.h"
#include <AceButton.h>
using namespace ace_button;

#include "prefs.h"

#if defined(ESP32)
#include <ESP32Time.h>
ESP32Time *rtc = nullptr;
#endif

// button variables
AceButton button;

#define CLK 9
#define DIO 7
#define BUTTON_PIN 5
#define Signal 3
#define PDN 10

#define MAX_BRIGHTNESS 7
#define LED_PIN 8

#define MIN_GOOD_FRAMES 2
#define MAX_BAD_FRAMES 5

uint8_t receivedBitCount = 0;
uint8_t frame_bit_index = 0;

// variables modified in ISR
volatile bool Showval = false; // true if decoding is complete.  Used to eliminate repeat
volatile unsigned long rise_time;
volatile unsigned long fall_time;
volatile bool updateOutput = false;

// frame variables
bool populating_frame = false; // true if a valid frame was detected
bool forcedResync = false;

enum BitTypes
{
  WAITING,
  ZERO,
  ONE,
  MARKER,
  FRAME,
  INTERFIELD_SPACE,
  UNKNOWN
} bitType,
    lastBitType;

// configuration
enum OperationMode
{
  CLOCK,
  CONFIG_BEGIN,
  CONFIG_1224,
  CONFIG_OFFSET,
  CONFIG_BRIGHTNESS
} mode;
uint8_t utcOffsetIndex = DEFAULT_UTCOFFSET;
uint8_t displayBrightness = MAX_BRIGHTNESS; // brightest
bool mode_12hour = false;

uint8_t bitvalue = 255; // value of the bit
uint8_t markerCounter = 0;
uint8_t bitsSinceLastMarker = 0;
uint32_t goodFrameCount = 0;
uint32_t framesSinceLastGoodFrame = 0;
unsigned long bitlength;

// field variables
uint16_t fieldValue = 0;                     // running value of the field as it is getting decoded
uint16_t fieldvalues[] = {0, 0, 0, 0, 0, 0}; // value for the decoded fields
int fieldIndex = 0;                          // which field is currently being decoded
const char *fieldNames[] = {"Minutes", "Hours", "DOY High", "DOYL/DUT+-", "DUT1/Year High", "Year Low/LY/DST"};
uint16_t hours = 0, minutes = 0, doy = 0, year = 0;
struct tm resolvedTime;

// Display variables
TM1637TinyDisplay6 display(CLK, DIO);
char displayBuffer[7];
unsigned long lastDisplayUpdate = 0;
uint8_t displaySeconds = 0;

// The event handler for the button.
void handleButtonEvent(AceButton * /* button */, uint8_t eventType,
                       uint8_t buttonState)
{

  // Print out a message for all events.
  // Serial.print(F("handleEvent(): eventType: "));
  // Serial.print(AceButton::eventName(eventType));
  // Serial.print(F("; buttonState: "));
  // Serial.println(buttonState);

  switch (eventType)
  {
  case AceButton::kEventLongPressed:
    display.clear();
    Serial.print("Mode switching from ");
    Serial.print(mode);
    Serial.print(" to ");
    switch (mode)
    {
    case CLOCK:
      Serial.println("CONFIG_1224");
      mode = CONFIG_1224;
      break;
    case CONFIG_1224:
      Serial.println("CONFIG_OFFSET");
      mode = CONFIG_OFFSET;
      break;
    case CONFIG_OFFSET:
      Serial.println("CONFIG_BRIGHTNESS");
      mode = CONFIG_BRIGHTNESS;
      break;
    case CONFIG_BRIGHTNESS:
      Serial.println("CLOCK");
      {
        savePrefs("prefs", mode_12hour, utcOffsetIndex, displayBrightness);
        receivedBitCount = 100;
        mode = CLOCK;
        break;
      }
    default:
      break;
    }
    break;
  case AceButton::kEventPressed:
  case AceButton::kEventReleased:
    break;
  case AceButton::kEventClicked:
    switch (mode)
    {
    case CONFIG_1224:
      if (mode_12hour)
      {
        mode_12hour = false;
      }
      else
      {
        mode_12hour = true;
      }
      break;
    case CONFIG_OFFSET:
      utcOffsetIndex++;
      if (utcOffsetIndex > NUM_OFFSETS)
      {
        utcOffsetIndex = 0;
      }
      break;
    case CONFIG_BRIGHTNESS:
      displayBrightness++;
      if (displayBrightness > MAX_BRIGHTNESS)
      {
        displayBrightness = 0;
      }
      break;
    default:
      break;
    }
    break;
  default:
    break;
  }
}

int8_t hourToHourMode(int8_t hour, bool mode)
{
  if (!mode)
    return hour;
  else
    return (hour > 12 ? hour - 12 : hour);
}

void parseFields()
{
  // minutes
  minutes = (((fieldvalues[0] & 0xF0) >> 4) * 10) + (fieldvalues[0] & 0x0F);
  // hours
  hours = (((fieldvalues[1] & 0xF0) >> 4) * 10) + (fieldvalues[1] & 0x0F);
  // day of year
  doy = (((fieldvalues[2] & 0xF0) >> 4) * 100) + ((fieldvalues[2] & 0x0F) * 10) + ((fieldvalues[3] & 0xF0) >> 4);
  // year
  year = ((fieldvalues[4] & 0x0F) * 10) + ((fieldvalues[5] & 0xF0) >> 4);

  struct tm t;

  time_t now = time(NULL);
  gmtime_r(&now, &t);

  t.tm_min = minutes + 1; // The frame describes the previous minute
  t.tm_hour = hours;
  t.tm_sec = 1;
  t.tm_mday = 1;
  t.tm_mon = 0;
  t.tm_mday = 0;
  t.tm_yday = 0;
  t.tm_year = 100 + year; // +100 == start at 2000

  t.tm_isdst = ((fieldvalues[5] & 0x3) == 3) ? 1 : 0;

  // t is now Jan 1 of the year @ hours:minutes past midnight.

  time_t ref = mktime(&t);
  time_t day = ref + (doy * 86400) + (utcOffsetsMinutes[utcOffsetIndex] * 60); // tm uses 0-based months

  // day is now t + doy days

  gmtime_r(&day, &resolvedTime);

  char buf[80];
  strftime(buf, 80, "Frame time: %c", &resolvedTime);
  Serial.println(buf);

#if defined(ESP32)
  rtc->setTimeStruct(resolvedTime);
#endif
}

void isr_routine()
{
  int signalValue = digitalRead(Signal);
  digitalWrite(LED_PIN, signalValue == HIGH ? LOW : HIGH);
  if (signalValue == HIGH)
  {
    rise_time = millis();
    updateOutput = true;
  }
  else
  {
    fall_time = millis();
    Showval = true;
  }
}

void setup()
{
  Serial.begin(115200);
  display.begin();
  delay(1000);

  display.setBrightness(displayBrightness);
  display.showString("boot");
  delay(2000);
  
  pinMode(Signal, INPUT); // Sets the WWVB NOT signal as an input (also S4)
  pinMode(PDN, OUTPUT);   // Sets the WWVB PDN control as an output
  pinMode(LED_PIN, OUTPUT);

  digitalWrite(PDN, HIGH); // Initialize the PDN signal as HIGH (Receiver off)
  delay(1000);             // Delay a bit
  digitalWrite(PDN, LOW);  // Now let the WWVB receiver operate - PDN is LOW
  delay(1000);             // Delay a bit


  // start with the defaults and override them with the preferences
  mode_12hour = false;
  utcOffsetIndex = 7;
  displayBrightness = 5;

  loadPreferences();

  time_t now = time(NULL);
  gmtime_r(&now, &resolvedTime);

#if !defined(ARDUINO_ARCH_AVR)
  rtc = new ESP32Time(0);
#endif

  // Button uses an external 10k resistor.
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  // We use the AceButton::init() method here instead of using the constructor
  // to show an alternative. Using init() allows the configuration of the
  // hardware pin and the button to be placed closer to each other.
  button.init(BUTTON_PIN);

  // Configure the ButtonConfig with the event handler, and enable the LongPress
  // and RepeatPress events which are turned off by default.
  ButtonConfig *buttonConfig = button.getButtonConfig();
  buttonConfig->setEventHandler(handleButtonEvent);
  buttonConfig->setFeature(ButtonConfig::kFeatureClick);
  buttonConfig->setFeature(ButtonConfig::kFeatureLongPress);

  display.clear();
  
  // DATA pin signal change edge detection. (Mandatory)
  attachInterrupt(digitalPinToInterrupt(Signal), isr_routine, CHANGE);


  Serial.println();
  Serial.println("Establishing frame synchronization");

  mode = CLOCK;
}

// Now that the setup has been done, the main loop is started
// **** MAIN LOOP *********************************************************************
char debugbuf[80];

void loop()
{
  button.check();

  switch (mode)
  {
  case CLOCK:
  {
    if (Showval)
    {
      receivedBitCount += 1;
      displaySeconds++;

      bitsSinceLastMarker++;

      bitvalue = 0;

      if (forcedResync || receivedBitCount > 61 || (populating_frame && markerCounter > 6))
      {
        // power cycle the receiver
        digitalWrite(PDN, HIGH); // Initialize the PDN signal as HIGH (Receiver off)
        delay(1000);             // Delay a bit
        digitalWrite(PDN, LOW);  // Now let the WWVB receiver operate - PDN is LOW
        delay(1000);             // Delay a bit

        // TODO: it might just be simpler to restart the uC
        Serial.println();
        Serial.println("Resynchronizing");
        display.clear();
        fieldIndex = 0;
        fieldValue = 0;
        bitType = UNKNOWN;
        populating_frame = false;
        frame_bit_index = 0;
        receivedBitCount = 0;
        markerCounter = 0;
        goodFrameCount = 0;
        displaySeconds = 0;
        lastDisplayUpdate = 0;
        forcedResync = false;
        bitsSinceLastMarker = 0;
      }
      else
      {
        bitlength = fall_time - rise_time;

        // Serial.println();
        // Serial.print(bitlength);
        // Serial.print(": ");

        if (populating_frame)
        {
          frame_bit_index++;
        }

        if (frame_bit_index == 4 || frame_bit_index == 14 || frame_bit_index == 24 || frame_bit_index == 34 || frame_bit_index == 44 || frame_bit_index == 54)
        {
          bitType = INTERFIELD_SPACE;
        }
        else if (bitlength < 200)
        {
          bitType = ZERO;
          if (populating_frame)
          {
            fieldValue <<= 1;
          }
        }
        else if (bitlength < 500)
        {
          bitType = ONE;
          bitvalue = 1;
          if (populating_frame)
          {
            fieldValue <<= 1;
            fieldValue += 1;
          }
        }
        else if (bitlength < 800)
        {
          // if were trying to populate a frame and there is weirdness in getting markers, then
          // start over.
          if (populating_frame && bitsSinceLastMarker < 9)
          {
            Serial.println();
            Serial.println("Invalid marker spacing during frame population.");
            forcedResync = true;
          }

          if (lastBitType == MARKER) // frame
          {
            bitType = FRAME;

            // Serial.println();
            // Serial.print("Marker counter = ");
            // Serial.println(markerCounter);
            // Serial.print("Frame bit index = ");
            // Serial.println(frame_bit_index);

            populating_frame = true;

            // if the previous frame was good then assume this one will be also
            if (frame_bit_index == 60 && markerCounter == 6)
            {
              goodFrameCount++;
              framesSinceLastGoodFrame = 0;
            }
            else
            {
              framesSinceLastGoodFrame++;
            }

            fieldIndex = 0;
            fieldValue = 0;
            frame_bit_index = 0;
            receivedBitCount = 1; // the frame bit is the first bit of the frame so... one bit
            markerCounter = 0;
            displaySeconds = 0;
            lastDisplayUpdate = 0;
            bitsSinceLastMarker = 0;
          }
          else // marker
          {
            bitType = MARKER;
            fieldvalues[fieldIndex] = fieldValue;
            fieldValue = 0;
            fieldIndex += 1;
            markerCounter++;
          }
        }
        else
        {
          bitType = UNKNOWN;
          forcedResync = true;
        }

        if (bitType == BitTypes::FRAME && framesSinceLastGoodFrame == 0)
        {
          // TODO: validate the previous frame 
          Serial.print(" -- ");
          parseFields();
        }

        switch (bitType)
        {
        case BitTypes::FRAME:
          if (goodFrameCount > MIN_GOOD_FRAMES)
          {
            Serial.print("*F");
          }
          else
          {
            Serial.print(" F");
          }
          break;
        case BitTypes::INTERFIELD_SPACE:
          Serial.print('S');
          break;
        case BitTypes::MARKER:
          Serial.print(" M ");
          break;
        case BitTypes::ONE:
          Serial.print('1');
          break;
        case BitTypes::ZERO:
          Serial.print('0');
          break;
        case BitTypes::UNKNOWN:
          Serial.print('?');
          break;
        default:
          Serial.print('-');
          break;
        }

        lastBitType = bitType;
        Showval = false;
      }

#if !defined(ESP32)
      if (framed)
      {
        display.showNumberDec(hourToHourMode(resolvedTime.tm_hour, mode_12hour), 0b01000000, false, 2, 0);
        display.showNumberDec(resolvedTime.tm_min, 0b01000000, true, 2, 2);
      }
#endif
    }
  }

#if defined(ESP32)
    // if ((framed == true) && (millis() - lastDisplayUpdate > 1000))
    if (updateOutput)
    {
      // Serial.println(rtc->getDateTime(true));

      struct tm t = rtc->getTimeStruct();
      bool noSync = (framesSinceLastGoodFrame > MAX_BAD_FRAMES);
      // display the time form the RTC if there are good frames or too many bad frames have come through

      if (goodFrameCount > MIN_GOOD_FRAMES || noSync)
      {
        display.showNumberDec(hourToHourMode(t.tm_hour, mode_12hour), 0b01000000, false, 2, 0);
        display.showNumberDec(t.tm_min, 0b01000000, true, 2, 2);
        display.showNumberDec(t.tm_sec, noSync ? 0b01000000 : 0b00000000, true, 2, 4);
      }
      else
      {
        display.showString("F", 1, 0, 0);
        display.showNumberDec(goodFrameCount, 0, false, 1, 1);
        display.showString("b", 1, 3, 0);
        display.showNumberDec(receivedBitCount, 0b00000000, true, 2, 4);
      }

      updateOutput = false;
      lastDisplayUpdate = millis();
    }
#endif

    break;
  case CONFIG_1224:
    display.showString("h", 1, 0, 0);
    display.showNumberDec(mode_12hour ? 12 : 24, 0, 0, 2, 4);
    break;
  case CONFIG_OFFSET:
    display.showString("o", 1, 0, 0);
    display.showNumberDec(utcOffsetsMinutes[utcOffsetIndex], 0, 0, 4, 2);
    break;
  case CONFIG_BRIGHTNESS:
    display.showString("b", 1, 0, 0);
    display.showNumberDec(displayBrightness, 0, 0, 1, 5);
    display.setBrightness(displayBrightness);
    break;
  default:
    Serial.println("Invalid operating mode");
    break;
  }
} // END OF LOOP

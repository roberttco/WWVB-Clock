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
#include "wwvb_frame.h"

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

#define LED_PIN 8

#define MIN_GOOD_FRAMES 2
#define MAX_BAD_FRAMES 5
#define RECEIVE_TIMEOUT_MS 60000

uint8_t receivedBitCount = 0;
uint8_t frame_bit_index = 0;

// variables modified in ISR
volatile bool bit_received = false;
volatile unsigned long rise_time;
volatile unsigned long pulse_width;
volatile bool updateOutput = false;
volatile BitTypes isr_bit_type;

// frame variables
bool populating_frame = false; // true if a valid frame was detected
bool forcedResync = false;
BitTypes lastBitType;

// configuration
enum OperationMode
{
  CLOCK,
  CONFIG_BEGIN,
  CONFIG_1224,
  CONFIG_OFFSET,
  CONFIG_BRIGHTNESS,
  CONFIG_FLIPDISPLAY
} mode;
uint8_t utcOffsetIndex = DEFAULT_UTCOFFSET;
uint8_t display_brightness = DEFAULT_BRIGHTNESS;
bool mode_12hour = false;
bool flipped_display = false;
unsigned long receive_watchdog = 0;

Frame *f = nullptr;
char outputBuffer[80];

// Display variables
TM1637TinyDisplay6 display(CLK, DIO);
bool synchronized = false;
uint8_t display_dots = 0b01011000;

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
      mode = CONFIG_FLIPDISPLAY;
      break;
    case CONFIG_FLIPDISPLAY:
      Serial.println("CLOCK");
      {
        savePrefs("prefs", mode_12hour, utcOffsetIndex, display_brightness, flipped_display);
        receivedBitCount = 100;
        display.setBrightness(display_brightness);
        display.flipDisplay(flipped_display);
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
      mode_12hour = mode_12hour ? false: true;
      break;
    case CONFIG_OFFSET:
      utcOffsetIndex++;
      if (utcOffsetIndex >= NUM_OFFSETS)
      {
        utcOffsetIndex = 0;
      }
      break;
    case CONFIG_BRIGHTNESS:
      display_brightness++;
      if (display_brightness > MAX_BRIGHTNESS)
      {
        display_brightness = 0;
      }
      break;
    case CONFIG_FLIPDISPLAY:
      flipped_display = flipped_display ? false : true;
    default:
      break;
    }
    break;
  default:
    break;
  }
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
    // fall_time = millis();
    pulse_width = millis() - rise_time;

    if ((pulse_width < 100) || (pulse_width > 800))
    {
      // bad antenna alignment probably
      isr_bit_type = BAD;
    }
    else if (pulse_width < 200)
    {
      isr_bit_type = ZERO;
    }
    else if (pulse_width < 500)
    {
      isr_bit_type = ONE;
    }
    else if (pulse_width < 800)
    {
      isr_bit_type = MARKER;
    }
    else
    {
      isr_bit_type = UNKNOWN;
    }

    bit_received = true;
  }
}

void setup()
{
  Serial.begin(115200);
  display.begin();
  delay(1000);


  display.setBrightness(7);
  display.showString("boot");

  delay(2000);

  loadPreferences();
  
  display.setBrightness(display_brightness);
  display.flipDisplay(flipped_display);

  pinMode(Signal, INPUT); // Sets the WWVB NOT signal as an input (also S4)
  pinMode(PDN, OUTPUT);   // Sets the WWVB PDN control as an output
  pinMode(LED_PIN, OUTPUT);

  digitalWrite(PDN, HIGH); // Initialize the PDN signal as HIGH (Receiver off)
  delay(1000);             // Delay a bit
  digitalWrite(PDN, LOW);  // Now let the WWVB receiver operate - PDN is LOW
  delay(1000);             // Delay a bit

  
  f = new Frame();

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
  Serial.print("received: ");

  receive_watchdog = millis();  // start the watchdog time
  mode = CLOCK;
}

// Now that the setup has been done, the main loop is started
// **** MAIN LOOP *********************************************************************
void loop()
{
  button.check();

  if (millis() - receive_watchdog > RECEIVE_TIMEOUT_MS)
  {
    Serial.println();
    Serial.println("No bit received before watchdot timeout.");
    display.showString("no rcv");
    delay(5000);
    ESP.restart();
  }

  switch (mode)
  {
  case CLOCK:
  {
    if (bit_received)
    {
      receivedBitCount += 1;
      receive_watchdog = millis();

      // Serial.printf("\nbit number = %2d, pulse width = %4dms, bit type = %1d, bit value = ", f->capturedBitCount(), pulse_width, isr_bit_type);

      // deal with some special cases
      if (isr_bit_type == BAD) // bad antenna alignment or something
        synchronized = false;
      else if ((isr_bit_type == MARKER) && (lastBitType == MARKER)) // frame
        isr_bit_type = FRAME;

      Serial.print(f->BitTypeToChar(isr_bit_type));

      // dont add the next frame's frame bit to the current frame buffer
      if (isr_bit_type != FRAME && isr_bit_type != BAD)
      {
        f->add(isr_bit_type);
      }
      else
      {
        Serial.println();
        Serial.println("captured: ");

        f->printFrame();

        // field values and other info
        Serial.printf(" fi:%d y:%d d:%d h:%d m:%d c:%d %s bt:%d",
                      f->capturedBitCount(), f->year(), f->doy(), f->hours(), f->minutes(), f->dutMs(), (f->isDST() ? "DST" : "ST"), isr_bit_type);

        if (f->isValid())
        {
          Serial.print(" valid ");

          // TODO: calculate time and update RTC clock
          struct tm frame_time;

          // Add one minute to account for the fact that the frame is one minute behind the actual time
          // (by the time the frame is parsed).
          if (f->frameTime(&frame_time, (utcOffsetsMinutes[utcOffsetIndex]) + 1))
          {
            strftime(outputBuffer, 80, "%c", &frame_time);
            Serial.println(outputBuffer);
          }

          // set the RTC on a valid frame to the local time
          rtc->setTimeStruct(frame_time);
          synchronized = true;
        }
        else
        {
          Serial.println(" invalid");
        }

        /// reset for a new frame
        f->reset();
        receivedBitCount = 1; // received one bit for this frame - the frame bit

        // put this here so the received string output begins with the 'F'
        Serial.printf("\nNew frame:\nreceived: ");

        f->add(BitTypes::FRAME); // add the FRM bit to the new frame
      }
    }

    lastBitType = isr_bit_type;
    bit_received = false;

    if (updateOutput)
    {
      updateOutput = false;
      struct tm rtctime = rtc->getTimeStruct();

      snprintf(outputBuffer, 80, "%2d%02d%02d", (mode_12hour ? (rtctime.tm_hour > 12 ? rtctime.tm_hour - 12 : rtctime.tm_hour) : rtctime.tm_hour), rtctime.tm_min, rtctime.tm_sec);
      if (synchronized)
        display_dots = 0b01010000;
      else if (display_dots == 0b01011000)
        display_dots = 0b01011000;
      else
        display_dots = 0b01010000;

      display.showString(outputBuffer, 6, 0, display_dots);
    }
  }
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
    display.showNumberDec(display_brightness+1, 0, 0, 1, 5);
    display.setBrightness(display_brightness);
    break;
  case CONFIG_FLIPDISPLAY:
    display.showString("f", 1, 0, 0);
    display.showNumberDec(flipped_display ? 1 : 0, 0, 0, 1, 5);
    display.flipDisplay(flipped_display);
    break;
  default:
    Serial.println("Invalid operating mode");
    break;
  }
} // END OF LOOP

#include <Arduino.h>
#include <ReducedSoftwareSerial.h>
#include <Emf_Off_common.h>
#include "SinLookupTable.h"

static const int TX_PIN = 4;
static const int RX_PIN = 3;
static const int STATUS_LED_PIN = 0;
static const int BUTTON_PIN = A0;
static const int SUPPLY_VOLTAGE_SENSE_PIN = A1;
static const int VIBRATE_MOTOR_PIN = 1;

static const int POWER_ON_STATUS_DISPLAY_DURATION_MS = 2000;
static const unsigned long COMMS_CHECK_RETRY_INTERVAL_MICROS = 2000000;
static const unsigned long BUTTON_DEBOUNCE_TIMEOUT_MICROSECONDS = 500000;
static const unsigned long MUTE_LED_PULSE_INTERVAL_MICROSECONDS = 60000000;
static const unsigned long ALERT_ACTIVE_LED_FLASH_DURATION_MICROSECONDS = 100000;
static const unsigned long ALERT_ACTIVE_VIBRATE_DURATION_MICROSECONDS = 1000000;
static const int LED_PULSE_LOOKUP_TABLE_SIZE = 100;
static const int BUTTON_PRESSED_ADC_VALUE_THRESHOLD = 750;

static const unsigned long TRANSMITTER_LOW_BATTERY_WARNING_LED_FLASH_DURATION_MICROSEDONDS = 700000;
static const unsigned long LOW_BATTERY_WARNING_LED_FLASH_DURATION_MICROSECONDS = 2000000;

SoftwareSerial softSerial(RX_PIN, TX_PIN);

bool alertActive = false;
volatile bool muted = false;
volatile bool doCommsTest = true;
bool displayTransmitterLowBattery = false;

unsigned long ledFlashDurationTimer = 0;
unsigned long vibrateDurationTimer = 0;
byte ledFlashCount = 0;
bool ledIsOn = false;

bool previousButtonIsPressed = false;
volatile unsigned long microsSinceLastButtonPress = BUTTON_DEBOUNCE_TIMEOUT_MICROSECONDS;

unsigned long microsecondsSinceCommsCheckRetry = COMMS_CHECK_RETRY_INTERVAL_MICROS;
volatile unsigned long microsecondsSinceMuteLedPulse;

bool pulseLed = false;
bool pulseLedOnlyOnce = false;
byte initialLedPulseBrightnessLevel = 0;
byte ledPulseBrightnessLevelLookupInput = 0;
unsigned int delayBetweenLedPulseBrightnessLevelsMicroseconds;

unsigned long previousMicros = 0;

void setup() {
  softSerial.begin(9600);

  pinMode(STATUS_LED_PIN, OUTPUT);
  pinMode(VIBRATE_MOTOR_PIN, OUTPUT);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(SUPPLY_VOLTAGE_SENSE_PIN, INPUT_PULLUP);

  if (batteryVoltageIsOk(SUPPLY_VOLTAGE_SENSE_PIN)) {
    displayStatusOk();
  }
  else {
    displayStatusLowBattery();
  }
}

void displayStatusOk() {
  digitalWrite(STATUS_LED_PIN, HIGH);
  delay(POWER_ON_STATUS_DISPLAY_DURATION_MS);
  digitalWrite(STATUS_LED_PIN, LOW);
}

void displayStatusLowBattery() {
  byte flashSpeedMilliseconds = 100;
  byte flashCount = POWER_ON_STATUS_DISPLAY_DURATION_MS / (flashSpeedMilliseconds * 2);

  for(byte i = 0; i < flashCount; i++) {
    digitalWrite(STATUS_LED_PIN, HIGH);
    delay(flashSpeedMilliseconds);
    digitalWrite(STATUS_LED_PIN, LOW);
    delay(flashSpeedMilliseconds);
  }
}

bool buttonClickHasHappened() {
  if (buttonIsDown() && !previousButtonIsPressed) {
    previousButtonIsPressed = true;

    microsSinceLastButtonPress = 0;
    return true;
  }

  if (!buttonIsDown() && previousButtonIsPressed && microsSinceLastButtonPress > BUTTON_DEBOUNCE_TIMEOUT_MICROSECONDS) {
    previousButtonIsPressed = false;
  }

  return false;
}

bool buttonIsDown() {
  analogReference(DEFAULT);
  int rawAdcValue = analogRead(BUTTON_PIN);

  if (rawAdcValue < BUTTON_PRESSED_ADC_VALUE_THRESHOLD) {
    return true;
  }
  return false;
}

void onClick() {
  if (alertActive) {
    cancelAlert();
  }
  else if (muted) {
    unmute();
  }
  else if (!displayTransmitterLowBattery) {
    doCommsTest = true;
  }
  else {
    cancelTransmitterLowBatteryAlert();
  }
}

void cancelAlert() {
  muted = true;
  microsecondsSinceMuteLedPulse = MUTE_LED_PULSE_INTERVAL_MICROSECONDS;
  delayBetweenLedPulseBrightnessLevelsMicroseconds = 6000;
  vibrateDurationTimer = 0;
  digitalWrite(VIBRATE_MOTOR_PIN, LOW);
}

void cancelTransmitterLowBatteryAlert() {
  displayTransmitterLowBattery = false;
  digitalWrite(STATUS_LED_PIN, LOW);
}

void unmute() {
  muted = false;
  doCommsTest = true;
}

void testCommsToTransmitter() {
  bool commsEstablished = false;
  unsigned long previousLoopMicros = micros();

  pulseLed = true;
  delayBetweenLedPulseBrightnessLevelsMicroseconds = 2000;

  while (!commsEstablished) {
    unsigned int microsSinceLastLoop = micros() - previousLoopMicros;
    previousLoopMicros = micros();

    pulseStatusLed(microsSinceLastLoop);

    microsecondsSinceCommsCheckRetry += microsSinceLastLoop;

    if (microsecondsSinceCommsCheckRetry >= COMMS_CHECK_RETRY_INTERVAL_MICROS) {
      softSerial.print('P');

      microsecondsSinceCommsCheckRetry = 0;
    }


    if (softSerial.available()) {
      char receivedChar = softSerial.read();

      if (receivedChar == 'P') {
        commsEstablished = true;
        microsecondsSinceCommsCheckRetry = COMMS_CHECK_RETRY_INTERVAL_MICROS;
      }
    }
  }

  pulseLed = false;
  slowDimStatusLedFromOnToOff();
}

void loop() {
  unsigned int microsSinceLastLoop = micros() - previousMicros;
  previousMicros = micros();

  microsSinceLastButtonPress += microsSinceLastLoop;
  if (buttonClickHasHappened()) {
    onClick();
  }

  if (doCommsTest) {
    testCommsToTransmitter();
    doCommsTest = false;
  }

  if (muted) {
    alertActive = false;
    microsecondsSinceMuteLedPulse += microsSinceLastLoop;

    if (microsecondsSinceMuteLedPulse > MUTE_LED_PULSE_INTERVAL_MICROSECONDS) {
      microsecondsSinceMuteLedPulse = 0;

      pulseLed = true;
      pulseLedOnlyOnce = true;

      initialLedPulseBrightnessLevel = 189;
      ledPulseBrightnessLevelLookupInput = 10;
    }
  }
  else if (alertActive) {
    flashStatusLed(microsSinceLastLoop, ALERT_ACTIVE_LED_FLASH_DURATION_MICROSECONDS, 255);
    vibrate(microsSinceLastLoop);
  }
  else if (displayTransmitterLowBattery) {
    flashStatusLed(microsSinceLastLoop, TRANSMITTER_LOW_BATTERY_WARNING_LED_FLASH_DURATION_MICROSEDONDS, 10);
  }
  else if (!batteryVoltageIsOk(SUPPLY_VOLTAGE_SENSE_PIN)) {
    flashStatusLed(microsSinceLastLoop, LOW_BATTERY_WARNING_LED_FLASH_DURATION_MICROSECONDS, 10);
  }

  if (pulseLed) {
    pulseStatusLed(microsSinceLastLoop);
  }

  respondToReceivedSerialData();
}

void pulseStatusLed(unsigned int microsSinceLastLoop) {
  ledFlashDurationTimer += microsSinceLastLoop;

  if (ledFlashDurationTimer >= delayBetweenLedPulseBrightnessLevelsMicroseconds) {
    ledFlashDurationTimer = 0;

    byte brightnessLevelOutput = getLedPulseBrightnessLevel(ledPulseBrightnessLevelLookupInput);
    analogWrite(STATUS_LED_PIN, brightnessLevelOutput);

    ledPulseBrightnessLevelLookupInput++;
    if (ledPulseBrightnessLevelLookupInput == LED_PULSE_LOOKUP_TABLE_SIZE * 2) {
      ledPulseBrightnessLevelLookupInput = 0;
    }

    if (pulseLedOnlyOnce && ledPulseBrightnessLevelLookupInput == initialLedPulseBrightnessLevel) {
      pulseLed = false;
      pulseLedOnlyOnce = false;
    }
  }
}

byte getLedPulseBrightnessLevel(byte inputBrightnessLevel) {
  byte completeCycleIndex = (LED_PULSE_LOOKUP_TABLE_SIZE * 2) - 1;
  while (inputBrightnessLevel > completeCycleIndex) {
    inputBrightnessLevel -= completeCycleIndex;
  }

  byte halfCycleIndex = LED_PULSE_LOOKUP_TABLE_SIZE - 1;
  if (inputBrightnessLevel > halfCycleIndex) {
    inputBrightnessLevel = halfCycleIndex - ((inputBrightnessLevel - halfCycleIndex) - 1);
  }

  return sinLookup[inputBrightnessLevel];
}

void slowDimStatusLedFromOnToOff() {
  for (byte i = 255; i > 0; i--) {
    analogWrite(STATUS_LED_PIN, i);
    delay(6);
  }
  digitalWrite(STATUS_LED_PIN, LOW);
}

void flashStatusLed(unsigned int microsSinceLastLoop, unsigned long flashDuration, byte brightness) {
  ledFlashDurationTimer += microsSinceLastLoop;

  if (ledFlashDurationTimer > flashDuration) {
    ledFlashDurationTimer = 0;

    ledIsOn = !ledIsOn;
    if (ledIsOn) {
      analogWrite(STATUS_LED_PIN, brightness);
    }
    else {
      digitalWrite(STATUS_LED_PIN, LOW);
    }
  }
}

void vibrate(unsigned int microsSinceLastLoop) {
  vibrateDurationTimer += microsSinceLastLoop;

  if (vibrateDurationTimer < ALERT_ACTIVE_VIBRATE_DURATION_MICROSECONDS) {
    digitalWrite(VIBRATE_MOTOR_PIN, HIGH);
  }
  else if (vibrateDurationTimer < 2 * ALERT_ACTIVE_VIBRATE_DURATION_MICROSECONDS) {
    digitalWrite(VIBRATE_MOTOR_PIN, LOW);
  }
  else {
    vibrateDurationTimer = 0;
  }
}

void respondToReceivedSerialData() {
  while (softSerial.available()) {
    char receivedChar = softSerial.read();

    switch (receivedChar) {
      case 'A':
        softSerial.print('A');
        alertActive = true;
        break;
      case 'B':
        softSerial.print('B');
        displayTransmitterLowBattery = true;
        break;
    }
  }
}
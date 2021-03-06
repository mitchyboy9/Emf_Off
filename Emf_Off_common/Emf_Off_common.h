#ifndef EMFOFFCOMMON_H_
#define EMFOFFCOMMON_H_

#include <Arduino.h>

static const int LOW_BATTERY_VOLTAGE_THRESHOLD_RAW_ADC_VALUE = 827;
static const int BATTERY_VOLTAGE_OK_THRESHOLD_RAW_ADC_VALUE = 864; // Hopefully roughly 125mV higher than low voltage threshold (I haven't tested my rough calculations)

static const int POWER_ON_STATUS_DISPLAY_DURATION_MS = 3000;

bool batteryVoltageIsOk(int supplyVoltageSensePin);
void displayStatusOk(int statusLedPin);
void displayStatusLowBattery(int statusLedPin);

#endif

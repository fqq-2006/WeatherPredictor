#ifndef SERIAL_DEBUG_H
#define SERIAL_DEBUG_H

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <time.h>
#include "WeatherTypes.h"

extern State state;
extern float temperature;
extern float humidity;
extern Weather weatherToday;
extern Weather weather[3];
extern struct tm timeInfo;
extern unsigned long Future_updateTime;
extern unsigned long Today_updateTime;
extern unsigned long lastDHTUpdate;
extern unsigned long lastMQTTStateUpdate;
extern unsigned long lastDisplayUpdate;
extern unsigned long lastAutoSwitch;
extern bool pageButtonPressed;
extern bool voiceButtonPressed;
extern bool switchPressed;
extern bool silentPressed;
extern WiFiClient espClient;
extern PubSubClient client;

void SerialPrintDebug();

#endif // SERIAL_DEBUG_H

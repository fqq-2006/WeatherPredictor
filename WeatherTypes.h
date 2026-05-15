#ifndef WEATHER_TYPES_H
#define WEATHER_TYPES_H

#include <Arduino.h>

struct Weather {
  char date[20];
  char MaxTemp[10];
  char MinTemp[10];
  char Temperature[10];
  char weatherText[50];
  char day[50];
  char night[50];
  char rain[10];
  char wind_direction[40];
  char wind_scale[10];
  char weather_code[4];
  char day_code[4];
  char night_code[4];
};

struct State {
  bool remote_mode;
  uint8_t page;
  bool voice_state;
  String city;
  bool voice_update;
  bool update_today_flag;
  bool update_future_flag;
  bool power_on;
};

#endif // WEATHER_TYPES_H

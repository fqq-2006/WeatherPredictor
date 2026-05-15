#include "Serial_Debug.h"
#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <time.h>

extern struct State state;
extern float temperature;
extern float humidity;
extern struct Weather weatherToday;
extern struct Weather weather[3];
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

void SerialPrintDebug()
{
    unsigned long now = millis();

    Serial.println();
    Serial.println(F("╔══════════════════════════════════════════════╗"));
    Serial.println(F("║             SYSTEM DEBUG INFO               ║"));
    Serial.println(F("╠══════════════════════════════════════════════╣"));

    Serial.print(F("║ "));
    Serial.print(state.power_on ? F("电源:ON ") : F("电源:OFF"));
    Serial.print(F(" │ "));
    Serial.print(state.remote_mode ? F("远程") : F("本地"));
    Serial.print(F(" │ "));
    Serial.print(F("页面:"));
    Serial.print(state.page);
    Serial.println(F("              ║"));

    Serial.print(F("║ "));
    Serial.print(state.voice_state ? F("语音:ON ") : F("语音:OFF"));
    Serial.print(F(" │ "));
    Serial.print(F("城市:"));
    Serial.print(state.city);
    Serial.println(F("               ║"));

    Serial.print(F("║ WiFi:"));
    Serial.print(WiFi.RSSI());
    Serial.print(F("dBm │ "));
    Serial.print(client.connected() ? F("MQTT:已连") : F("MQTT:断开"));
    Serial.print(F(" │ IP:"));
    Serial.print(WiFi.localIP());
    Serial.println(F("    ║"));

    Serial.print(F("║ 时间:"));
    if (timeInfo.tm_year > 100) {
        Serial.printf("%04d-%02d-%02d %02d:%02d:%02d",
            timeInfo.tm_year + 1900, timeInfo.tm_mon + 1, timeInfo.tm_mday,
            timeInfo.tm_hour, timeInfo.tm_min, timeInfo.tm_sec);
    } else {
        Serial.print(F("-- 未同步 --"));
    }
    Serial.println(F("           ║"));

    Serial.print(F("║ 室内:"));
    if (!isnan(temperature) && !isnan(humidity)) {
        Serial.print(temperature, 1);
        Serial.print(F("°C │ 湿度:"));
        Serial.print((int)humidity);
        Serial.print(F("%"));
    } else {
        Serial.print(F("-- DHT未就绪 --"));
    }
    Serial.println(F("                  ║"));

    Serial.print(F("║ 当前:"));
    if (strlen(weatherToday.Temperature) > 0) {
        Serial.print(weatherToday.Temperature);
        Serial.print(F("°C "));
        Serial.print(weatherToday.weatherText);
    } else {
        Serial.print(F("-- 暂无数据 --"));
    }
    Serial.println(F("                    ║"));

    Serial.print(F("║ 标志:"));
    Serial.print(state.update_today_flag   ? F("今▲ ") : F("今▽ "));
    Serial.print(state.update_future_flag  ? F("未▲ ") : F("未▽ "));
    Serial.print(state.voice_update        ? F("语▲ ") : F("语▽ "));
    Serial.print(state.power_on            ? F("开")   : F("关"));

    Serial.print(F("║ 计时:autoSw="));
    Serial.print((now - lastAutoSwitch) / 1000);
    Serial.print(F("s DHT="));
    Serial.print((now - lastDHTUpdate) / 1000);
    Serial.print(F("s MQTT="));
    Serial.print((now - lastMQTTStateUpdate) / 1000);
    Serial.print(F("s 显="));
    Serial.print((now - lastDisplayUpdate));
    Serial.println(F("ms       ║"));

    Serial.print(F("║ 更新:"));
    if (Today_updateTime > 0) {
        Serial.print(F("今="));
        Serial.print((now - Today_updateTime) / 1000);
        Serial.print(F("s"));
    } else {
        Serial.print(F("今=--"));
    }
    if (Future_updateTime > 0) {
        Serial.print(F(" 未="));
        Serial.print((now - Future_updateTime) / 1000);
        Serial.print(F("s"));
    } else {
        Serial.print(F(" 未=--"));
    }
    Serial.println(F("                     ║"));

    Serial.println(F("╚══════════════════════════════════════════════╝"));
    Serial.println();
}

#include <ESP8266WiFi.h>
#include <time.h>
#include <U8g2lib.h>
#include <DHT.h>
#include <Wire.h>
#include <WiFiManager.h>
#include <PubSubClient.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecureBearSSL.h>
#include <ArduinoJson.h>
#include <cstring>

#define DHTPIN 4
#define DHTTYPE DHT11
DHT dht(DHTPIN, DHTTYPE);

#define BUTTON_PAGE 5
#define BUTTON_VOICE 6

U8G2_SSD1306_128X64_NONAME_1_HW_I2C u8g2(U8G2_R0, /* reset=*/ U8X8_PIN_NONE);

static const unsigned char sun_bits[] U8X8_PROGMEM = {
  0x00,0x00,0x00,0x00,0x3C,0x00,0x42,0x00,0x81,0x80,0xA5,0x80,0x81,0x80,0x81,0x80,
  0xA5,0x80,0x81,0x80,0x81,0x80,0x42,0x00,0x3C,0x00,0x00,0x00,0x00,0x00,0x00,0x00
};
static const unsigned char cloud_bits[] U8X8_PROGMEM = {
  0x00,0x00,0x00,0x00,0x1C,0x00,0x2E,0x00,0x5F,0x00,0x7F,0x80,0x7F,0x80,0x3E,0x00,
  0x1C,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00
};
static const unsigned char rain_bits[] U8X8_PROGMEM = {
  0x00,0x00,0x00,0x00,0x1C,0x00,0x2E,0x00,0x5F,0x00,0x7F,0x80,0x7F,0x80,0x3E,0x00,
  0x10,0x00,0x28,0x00,0x10,0x00,0x28,0x00,0x10,0x00,0x28,0x00,0x00,0x00,0x00,0x00
};
static const unsigned char snow_bits[] U8X8_PROGMEM = {
  0x00,0x00,0x00,0x00,0x10,0x00,0x10,0x00,0x92,0x40,0x7C,0x00,0x10,0x00,0x7C,0x00,
  0x92,0x40,0x10,0x00,0x10,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00
};
static const unsigned char overcast_bits[] U8X8_PROGMEM = {
  0x00,0x00,0x00,0x00,0x1C,0x00,0x2E,0x00,0x5F,0x00,0x7F,0x80,0x7F,0x80,0x7F,0x80,
  0x3E,0x00,0x1C,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00
};
// 全局显示/播报缓冲区（避免栈溢出）
char timeBuf[6];
char temperatureBuf[16];
char sensorBuf[32];
char titleBuf[64];
char rangeBuf[32];
char dayNightBuf[120];
char detailsBuf[64];
char mqttBuffer[256];
String voiceText;

unsigned long lastDisplayUpdate = 0;
unsigned long lastPageButtonMillis = 0;
unsigned long lastVoiceButtonMillis = 0;
bool pageButtonPressed = false;
bool voiceButtonPressed = false;


//数据初始化
String Future_url;
String Today_url;
char payload[1024];
enum WeatherType{
  Today = 0,
  Future
} SearchType;

struct tm timeInfo;

struct Weather{
  char date[20];
  char MaxTemp[10];
  char MinTemp[10];
  char Temperature[10];
  char weatherText[50];
  char day[50];
  char night[50];
  char rain[10];
  char wind_direction[20];
  char wind_scale[10];
} weather[3],weatherToday;
unsigned long Future_updateTime, Today_updateTime;
unsigned long lastMQTTStateUpdate ;

unsigned long lastDHTUpdate = 0;
float temperature,humidity;
struct State{
    bool remote_mode; 
    uint8_t page;
    bool voice_state;
    String city;
    bool voice_update;
    bool update_today_flag ;
    bool update_future_flag ;
} state = {false,0,true,"beijing",false,false,false};

void DrawStatusBar();
void DrawCurrentPage();
void DrawTodayPage();
void DrawTomorrowPage();
void DrawAfterTomorrowPage();
void DrawPage();
void PlayVoiceForPage(uint8_t pageIndex);
void CheckButtons();
const unsigned char* SelectWeatherIcon(const char* weatherText);

//定义服务器配置
WiFiManager wifiManager;
String mqtt_server = "192.168.5.4";
WiFiManagerParameter custom_city("city", "输入城市", "beijing", 20);
WiFiManagerParameter custom_mqtt("mqtt", "MQTT服务器地址", "192.168.5.4", 40);
WiFiClient espClient;
PubSubClient client(espClient);
char msg[50];
char position[10];
String clientID = "ESP8266Client-";

void DHT_update()
{
    temperature = dht.readTemperature();
    humidity = dht.readHumidity();
    if(isnan(temperature) || isnan(humidity))
    {
        Serial.println("读取DHT11传感器失败!");
        return;
    }
}

void url_update()
{
    Future_url = "https://api.seniverse.com/v3/weather/daily.json?key=S9iZu2FjVm2I_PwLY&location="+state.city+"&language=zh-Hans&unit=c&start=0&days=3";
    Today_url = "https://api.seniverse.com/v3/weather/now.json?key=S9iZu2FjVm2I_PwLY&location="+state.city+"&language=zh-Hans&unit=c";
}
//MQTT连接函数
void connectMQTT() {
    while (!client.connected()) {
        Serial.print("正在连接MQTT服务器...");
        if (client.connect(clientID.c_str())) {
            Serial.println("连接成功");
            client.subscribe("clock/equip1/set");
            client.subscribe("clock/equip1/control");
        } else {
            Serial.print("连接失败, rc=");
            Serial.print(client.state());
            Serial.println(" 5秒后重试...");
            delay(5000);
        }
    }
}

void MQTT_publish(const char* topic, const char* payload) {
    if (client.connected()) {
        client.publish(topic, payload);
    } else {
        Serial.println("MQTT未连接,无法发布消息");
    }
}

void State_publish(bool state_update,bool city_update,bool sensor_update) {
    StaticJsonDocument<256> doc;
    if(state_update){
        doc["mode"] = (state.remote_mode)?"remote":"local";
        doc["page"] = state.page;
        doc["rssi"] = WiFi.RSSI();
        doc["voicestate"] = state.voice_state;
        serializeJson(doc, mqttBuffer);
        MQTT_publish("clock/equip1/state", mqttBuffer);
        doc.clear();
    }
    if(city_update){
        doc["city"] = state.city;
        serializeJson(doc, mqttBuffer);
        MQTT_publish("clock/equip1/state/city", mqttBuffer);
        doc.clear();
    }
    if(sensor_update){
        doc["temperature"] = temperature;
        doc["humidity"] = humidity;
        serializeJson(doc, mqttBuffer);
        MQTT_publish("clock/equip1/state/sensor", mqttBuffer);
    }
}

void callback(char* topic, byte* payload, unsigned int length) {
    String topicStr(topic);
    DynamicJsonDocument doc(64);
    deserializeJson(doc, payload, length);
    JsonArray data = doc["command"];
    if(topicStr == "clock/equip1/control")
    {
        if(state.remote_mode == false)
        {
            Serial.println("设备处于本地模式，忽略控制命令");
            return;
        }
        if(!data[0].isNull() && data[0].as<int>() != state.page)
        {
            state.page = data[0].as<int>();
        }
        if(!data[1].isNull()&& data[1].as<bool>() == true)
        {
            state.update_today_flag = true;
        }
        if(!data[2].isNull()&& data[2].as<bool>() == true)
        {
            state.update_future_flag = true;
        }
        if(!data[3].isNull())
        {
            state.voice_update = data[3].as<bool>();
        }
        if(!data[4].isNull()&& data[4].as<bool>() != state.voice_state)
        {
            state.voice_state = data[4].as<bool>();
        }
        State_publish(true, false, false);
    }else if(topicStr == "clock/equip1/set")
    {
        //更新城市
        if(data[0].as<String>() != state.city && !data[0].isNull())
        {
            state.city = data[0].as<String>();
            url_update();
            Serial.print("城市设置为: ");
            Serial.println(state.city);
            State_publish(false, true, false);
            state.update_today_flag = true;
            state.update_future_flag = true;
        }
        if(!data[1].isNull()&& data[1].as<bool>() != state.remote_mode)
        {   
            state.remote_mode = data[1].as<bool>();
            Serial.print("自动模式设置为: ");
            Serial.println(state.remote_mode);
            State_publish(true, false, false);
        }
    }
}

//未来天气处理函数
void handlerFuture(const char* payload){ 
    StaticJsonDocument<512> filter;

    filter["results"][0]["last_update"] = true;

    // 只保留前三天
    for(int i = 0; i < 3; i++)
    {
        filter["results"][0]["daily"][i]["date"] = true;

        filter["results"][0]["daily"][i]["high"] = true;
        filter["results"][0]["daily"][i]["low"] = true;

        filter["results"][0]["daily"][i]["text_day"] = true;
        filter["results"][0]["daily"][i]["text_night"] = true;

        filter["results"][0]["daily"][i]["precip"] = true;
        filter["results"][0]["daily"][i]["wind_direction"] = true;
        filter["results"][0]["daily"][i]["wind_scale"] = true;
    }

    // 2. 创建 JSON 文档
    DynamicJsonDocument doc(1024);

    // 3. 解析 JSON
    deserializeJson(doc, payload, DeserializationOption::Filter(filter));

    // 4. 更新时间
    Future_updateTime = millis();

    JsonArray daily =
        doc["results"][0]["daily"];

    for(int i = 0; i < 3; i++)
    {
        strcpy(weather[i].date, daily[i]["date"].as<const char*>());

        strcpy(weather[i].MaxTemp, daily[i]["high"].as<const char*>());

        strcpy(weather[i].MinTemp, daily[i]["low"].as<const char*>());

        strcpy(weather[i].day, daily[i]["text_day"].as<const char*>());

        strcpy(weather[i].night, daily[i]["text_night"].as<const char*>());
        strcpy(weather[i].rain, daily[i]["precip"].as<const char*>());
        strcpy(weather[i].wind_direction, daily[i]["wind_direction"].as<const char*>());
        strcpy(weather[i].wind_scale, daily[i]["wind_scale"].as<const char*>());    
    }

    // 11. 提取字段
    Serial.println("==========3日天气信息==========");

    Serial.print("更新时间: ");
    Serial.printf("%d-%d-%d-%02d:%02d:%02d\n",timeInfo.tm_year+1900,timeInfo.tm_mon+1,timeInfo.tm_mday,timeInfo.tm_hour,timeInfo.tm_min,timeInfo.tm_sec);

    for(int i = 0; i < 3; i++)
    {
        Serial.println("----------------");

        Serial.print("日期: ");
        Serial.println(weather[i].date);

        Serial.print("温度：");
        Serial.print(weather[i].MinTemp);
        Serial.print("~");
        Serial.println(weather[i].MaxTemp);

        Serial.print("白天: ");
        Serial.println(weather[i].day);

        Serial.print("夜间: ");
        Serial.println(weather[i].night);
        Serial.print("降水概率：");
        Serial.println(weather[i].rain);
        Serial.print("风向：");
        Serial.println(weather[i].wind_direction);
        Serial.print("风力：");
        Serial.println(weather[i].wind_scale);
    }
    MQTT_publish("clock/equip1/event","future_update");
}

//当前天气处理函数
void handlerToday(const char* payload)
{ 
    DynamicJsonDocument doc(1024);
    deserializeJson(doc, payload);
    JsonObject results = doc["results"][0];
    Today_updateTime = millis();
    strcpy(weatherToday.Temperature, results["now"]["temperature"].as<const char*>());
    strcpy(weatherToday.weatherText, results["now"]["text"].as<const char*>());
    Serial.println("==========当前天气信息==========");
    Serial.printf("更新时间%d-%d-%d-%02d:%02d:%02d\n",timeInfo.tm_year+1900,timeInfo.tm_mon+1,timeInfo.tm_mday,timeInfo.tm_hour,timeInfo.tm_min,timeInfo.tm_sec);
    Serial.println("-------------------------------");
    Serial.print("当前温度：");
    Serial.println(weatherToday.Temperature);
    Serial.print("当前天气：");
    Serial.println(weatherToday.weatherText);
    MQTT_publish("clock/equip1/event","today_update");
}

const unsigned char* SelectWeatherIcon(const char* weatherText) {
    if (strcmp(weatherText, "晴") == 0) {
        return sun_bits;
    } else if (strcmp(weatherText, "多云") == 0) {
        return cloud_bits;
    } else if (strcmp(weatherText, "雨") == 0 || strstr(weatherText, "雨")) {
        return rain_bits;
    } else if (strcmp(weatherText, "雪") == 0 || strstr(weatherText, "雪")) {
        return snow_bits;
    } else {
        return overcast_bits;
    }
}

void DrawStatusBar() {
    u8g2.setFont(u8g2_font_6x12_tf);
    u8g2.drawHLine(0, 13, 128);

    snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d", timeInfo.tm_hour, timeInfo.tm_min);
    u8g2.drawStr(0, 11, timeBuf);

    String city = state.city;
    if (city.length() > 12) {
        city = city.substring(0, 12);
    }
    int16_t cityWidth = u8g2.getUTF8Width(city.c_str());
    int16_t cityX = max(0, (128 - cityWidth) / 2);
    u8g2.drawUTF8(cityX, 11, city.c_str());

    const char* voice = state.voice_state ? "VOC" : "MUT";
    int16_t voiceWidth = u8g2.getUTF8Width(voice);
    u8g2.drawStr(128 - voiceWidth, 11, voice);
}

void DrawCurrentPage() {
    u8g2.firstPage();
    do {
        DrawStatusBar();
        const unsigned char* icon = SelectWeatherIcon(weatherToday.weatherText);
        u8g2.drawXBMP(8, 20, 16, 16, icon);

        u8g2.setFont(u8g2_font_logisoso20_tf);
        snprintf(temperatureBuf, sizeof(temperatureBuf), "%s°C", weatherToday.Temperature);
        u8g2.drawUTF8(36, 44, temperatureBuf);

        u8g2.setFont(u8g2_font_wqy12_t_gb2312);
        u8g2.drawUTF8(40, 60, weatherToday.weatherText);

        snprintf(sensorBuf, sizeof(sensorBuf), "T:%d°C H:%d%%", (int)temperature, (int)humidity);
        u8g2.drawUTF8(0, 62, sensorBuf);
    } while (u8g2.nextPage());
}

void DrawFuturePage(uint8_t index, const char* label) {
    u8g2.firstPage();
    do {
        DrawStatusBar();
        u8g2.setFont(u8g2_font_wqy12_t_gb2312);

        snprintf(titleBuf, sizeof(titleBuf), "%s %s", label, weather[index].day);
        u8g2.drawUTF8(0, 28, titleBuf);

        snprintf(rangeBuf, sizeof(rangeBuf), "%s°C ~ %s°C", weather[index].MinTemp, weather[index].MaxTemp);
        u8g2.drawUTF8(0, 40, rangeBuf);

        snprintf(dayNightBuf, sizeof(dayNightBuf), "日:%s 夜:%s", weather[index].day, weather[index].night);
        u8g2.drawUTF8(0, 50, dayNightBuf);

        snprintf(detailsBuf, sizeof(detailsBuf), "降水:%s%% %s %s级", weather[index].rain, weather[index].wind_direction, weather[index].wind_scale);
        u8g2.drawUTF8(0, 62, detailsBuf);
    } while (u8g2.nextPage());
}

void DrawTodayPage() {
    DrawFuturePage(0, "今日");
}

void DrawTomorrowPage() {
    DrawFuturePage(1, "明日");
}

void DrawAfterTomorrowPage() {
    DrawFuturePage(2, "后天");
}

void DrawPage() {
    switch (state.page) {
        case 0:
            DrawCurrentPage();
            break;
        case 1:
            DrawTodayPage();
            break;
        case 2:
            DrawTomorrowPage();
            break;
        case 3:
        default:
            DrawAfterTomorrowPage();
            break;
    }
}

void PlayVoiceForPage(uint8_t pageIndex) {
    if (!state.voice_state) {
        return;
    }

    voiceText = "";
    if (pageIndex == 0) {
        voiceText = "现在";
        voiceText += weatherToday.weatherText;
        voiceText += "温度";
        voiceText += weatherToday.Temperature;
        voiceText += "度，室内温度";
        voiceText += String(temperature, 1);
        voiceText += "度，湿度百分之";
        voiceText += String((int)humidity);
    } else {
        const char* label = pageIndex == 1 ? "今天" : (pageIndex == 2 ? "明天" : "后天");
        Weather &w = weather[pageIndex - 1];
        voiceText = String(label) + "天气，白天";
        voiceText += w.day;
        voiceText += "，夜间";
        voiceText += w.night;
        voiceText += "，温度";
        voiceText += w.MinTemp;
        voiceText += "到";
        voiceText += w.MaxTemp;
        voiceText += "度，降水概率百分之";
        voiceText += w.rain;
        voiceText += "，";
        voiceText += w.wind_direction;
        voiceText += w.wind_scale;
        voiceText += "级。";
    }
    Serial1.println(voiceText);
}

void CheckButtons() {
    bool pagePressed = digitalRead(BUTTON_PAGE) == LOW;
    if (pagePressed && !pageButtonPressed && millis() - lastPageButtonMillis > 50) {
        pageButtonPressed = true;
    }
    if (!pagePressed && pageButtonPressed) {
        pageButtonPressed = false;
        lastPageButtonMillis = millis();
        state.page = (state.page + 1) % 4;
        State_publish(true, false, false);
    }

    bool voicePressed = digitalRead(BUTTON_VOICE) == LOW;
    if (voicePressed && !voiceButtonPressed && millis() - lastVoiceButtonMillis > 50) {
        voiceButtonPressed = true;
    }
    if (!voicePressed && voiceButtonPressed) {
        voiceButtonPressed = false;
        lastVoiceButtonMillis = millis();
        if (state.voice_state) {
            PlayVoiceForPage(state.page);
        }
    }
}

void getWeather(String url,enum WeatherType type)
{
    //私钥：S9iZu2FjVm2I_PwLY

    std::unique_ptr<BearSSL::WiFiClientSecure> client(
        new BearSSL::WiFiClientSecure);

    client->setInsecure();

    HTTPClient https;

    if (https.begin(*client, url))
    {

        int httpCode = https.GET();

        if (httpCode == 400 || httpCode == 200)
        {
            Serial.print("HTTP Code: ");
            Serial.println(httpCode);
            https.getString().toCharArray(payload, sizeof(payload));

            Serial.println(payload);
            
            switch (type)
            {
                case Today:
                    handlerToday(payload);
                    break;
                case Future:
                    handlerFuture(payload);
                    break;
            }
        }else
        {
            Serial.print("请求失败: ");
            Serial.println(httpCode);
        }

        https.end();
    }
    delay(0); // 让出 CPU，避免长时间阻塞软看门狗
}

void setup() {
    // put your setup code here, to run once:
    Serial.begin(115200);
    Serial1.begin(9600);
    dht.begin();
    pinMode(BUTTON_PAGE, INPUT_PULLUP);
    pinMode(BUTTON_VOICE, INPUT_PULLUP);
    u8g2.begin();
    url_update();
    
    wifiManager.addParameter(&custom_city);
    wifiManager.addParameter(&custom_mqtt);
    if (!wifiManager.startConfigPortal("AutoConnectAP")) {
        Serial.println("WiFi连接失败,重启...");
        delay(3000);
        ESP.restart();
    } 
    state.city = String(custom_city.getValue());
    mqtt_server = String(custom_mqtt.getValue());
    randomSeed(micros());
    
    clientID += String(random(0xffff),HEX);

    client.setServer(mqtt_server.c_str(), 1883);
    client.setCallback(callback);

    // 如果运行到这，说明WiFi已连接成功
    Serial.println("WiFi连接成功!");
    Serial.print("IP地址: ");
    Serial.println(WiFi.localIP());
    connectMQTT();
    configTime(8 * 3600, 0, "ntp.aliyun.com", "cn.ntp.org.cn");
    while(!getLocalTime(&timeInfo)){
        Serial.println("时间同步中...");
        delay(500);
    }
    Serial.println("时间同步成功!");
    Serial.println("开始请求当前天气...");
    getWeather(Today_url,SearchType = Today);
    Serial.println("当前天气请求完成");
    delay(0);
    Serial.println("开始请求未来天气...");
    getWeather(Future_url,SearchType = Future);
    Serial.println("未来天气请求完成");
    State_publish(true, true, false);
    lastMQTTStateUpdate = millis();
}

void loop() {
    // put your main code here, to run repeatedly:
    if (!client.connected()) {
        connectMQTT();
    }
    client.loop();
    CheckButtons();

    if(state.voice_state && state.voice_update)
    {
        PlayVoiceForPage(state.page);
        state.voice_update = false;
    }

    if(state.update_future_flag)
    {
        getWeather(Future_url,SearchType = Future);
        state.update_future_flag = false;
    }
    if(state.update_today_flag)
    {
        getWeather(Today_url,SearchType = Today);
        state.update_today_flag = false;
    }
    if(millis() - lastDHTUpdate > 10000) // 每10s更新一次传感器数据
    {
        DHT_update();
        State_publish(false, false, true);
        lastDHTUpdate = millis();
    }
    if(millis() - lastMQTTStateUpdate > 60000) // 每1分钟发布一次状态
    {
        State_publish(true, false, true);
        lastMQTTStateUpdate = millis();
    }
    // 每隔10分钟更新一次天气信息
    if (millis() - Today_updateTime > 600000) {
        Serial.println("更新当前天气...");
        getWeather(Today_url,SearchType = Today);
        Serial.println("当前天气更新完成");
        if (millis() - Future_updateTime > 21600000) {
            Serial.println("更新未来天气...");
            getWeather(Future_url,SearchType = Future);
            Serial.println("未来天气更新完成");
        }
    }

    if (millis() - lastDisplayUpdate >= 200) {
        if (getLocalTime(&timeInfo, 0)) {
            DrawPage();
        }
        lastDisplayUpdate = millis();
    }
}

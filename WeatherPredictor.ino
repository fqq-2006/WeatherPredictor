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
#include "Voice.h"

#define DHTPIN 13
#define DHTTYPE DHT11
DHT dht(DHTPIN, DHTTYPE);

#define BUTTON_PAGE 14
#define BUTTON_VOICE 15
#define BUTTON_SILENT 12
#define BUTTON_SWITCH 16

U8G2_SSD1306_128X64_NONAME_1_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);

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
unsigned long lastAutoSwitch = 0;
unsigned long lastSwitchMillis = 0;
unsigned long lastSilentMillis = 0;
bool pageButtonPressed = false;
bool voiceButtonPressed = false;
bool switchPressed = false;
bool silentPressed = false;


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
  char weather_code[4];
  char day_code[4];
  char night_code[4];
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
    bool power_on;
} state = {false,0,true,"beijing",false,false,false,true};

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
    // 读取DHT11传感器的温度和湿度
    temperature = dht.readTemperature();
    humidity = dht.readHumidity();
    // 检查读取是否成功，如果失败则打印错误信息并返回
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
        // 尝试使用客户端ID连接
        if (client.connect(clientID.c_str())) {
            Serial.println("连接成功");
            // 订阅控制命令和设置命令的主题
            client.subscribe("clock/equip1/set");
            client.subscribe("clock/equip1/control");
        } else {
            // 连接失败，打印状态码并等待5秒后重试
            Serial.print("连接失败, rc=");
            Serial.print(client.state());
            Serial.println(" 5秒后重试...");
            delay(5000);
        }
    }
}

void MQTT_publish(const char* topic, const char* payload) {
    // 如果MQTT已连接，则发布消息到指定主题
    if (client.connected()) {
        client.publish(topic, payload);
    } else {
        Serial.println("MQTT未连接,无法发布消息");
    }
}

void State_publish(bool state_update,bool city_update,bool sensor_update) {
    // 创建JSON文档用于存储状态信息
    StaticJsonDocument<256> doc;
    
    // 如果需要更新设备状态（模式、页面、信号强度、语音状态）
    if(state_update){
        doc["mode"] = (state.remote_mode)?"remote":"local";
        doc["page"] = state.page;
        doc["rssi"] = WiFi.RSSI();
        doc["voicestate"] = state.voice_state;
        serializeJson(doc, mqttBuffer);
        // 发布设备整体状态
        MQTT_publish("clock/equip1/state", mqttBuffer);
        doc.clear();
    }
    
    // 如果需要更新城市信息
    if(city_update){
        doc["city"] = state.city;
        serializeJson(doc, mqttBuffer);
        // 发布城市状态
        MQTT_publish("clock/equip1/state/city", mqttBuffer);
        doc.clear();
    }
    
    // 如果需要更新传感器数据（温度、湿度）
    if(sensor_update){
        doc["temperature"] = temperature;
        doc["humidity"] = humidity;
        serializeJson(doc, mqttBuffer);
        // 发布传感器状态
        MQTT_publish("clock/equip1/state/sensor", mqttBuffer);
    }
}

void callback(char* topic, byte* payload, unsigned int length) {
    // MQTT消息回调函数，处理接收到的命令
    String topicStr(topic);
    DynamicJsonDocument doc(64);
    deserializeJson(doc, payload, length);
    JsonArray data = doc["command"];
    if(topicStr == "clock/equip1/control")
    {
        // 如果设备处于本地模式，忽略远程控制命令
        if(state.remote_mode == false)
        {
            Serial.println("设备处于本地模式，忽略控制命令");
            return;
        }
        // 更新页面索引 (data[0])
        if(!data[0].isNull() && data[0].as<int>() != state.page)
        {
            state.page = data[0].as<int>();
        }
        // 标记需要更新今日天气 (data[1])
        if(!data[1].isNull()&& data[1].as<bool>() == true)
        {
            state.update_today_flag = true;
        }
        // 标记需要更新未来天气 (data[2])
        if(!data[2].isNull()&& data[2].as<bool>() == true)
        {
            state.update_future_flag = true;
        }
        // 更新语音播报触发标志 (data[3])
        if(!data[3].isNull())
        {
            state.voice_update = data[3].as<bool>();
        }
        // 更新语音开关状态 (data[4])
        if(!data[4].isNull()&& data[4].as<bool>() != state.voice_state)
        {
            state.voice_state = data[4].as<bool>();
        }
        // 发布更新后的设备状态
        State_publish(true, false, false);
        
    // 处理设置命令主题: clock/equip1/set
    }else if(topicStr == "clock/equip1/set")
    {
        // 更新城市设置 (data[0])
        if(data[0].as<String>() != state.city && !data[0].isNull())
        {
            state.city = data[0].as<String>();
            url_update(); // 重新生成API URL
            Serial.print("城市设置为: ");
            Serial.println(state.city);
            State_publish(false, true, false); // 发布城市变更
            // 城市变更后，标记需要重新获取天气数据
            state.update_today_flag = true;
            state.update_future_flag = true;
        }
        // 更新远程/本地模式设置 (data[1])
        if(!data[1].isNull()&& data[1].as<bool>() != state.remote_mode)
        {   
            state.remote_mode = data[1].as<bool>();
            Serial.print("远控模式设置为: ");
            Serial.println(state.remote_mode);
            State_publish(true, false, false); // 发布模式变更
        }
    }
}

//未来天气处理函数
void handlerFuture(const char* payload){ 
    // 创建过滤器，只解析需要的字段以节省内存
    StaticJsonDocument<512> filter;

    filter["results"][0]["last_update"] = true;

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
        filter["results"][0]["daily"][i]["code_day"] = true;
        filter["results"][0]["daily"][i]["code_night"] = true;
    }

    DynamicJsonDocument doc(1024);
    deserializeJson(doc, payload, DeserializationOption::Filter(filter));

    // 更新最后获取时间
    Future_updateTime = millis();

    // 提取每日天气数组
    JsonArray daily = doc["results"][0]["daily"];

    // 遍历并保存未来3天的天气数据到全局结构体
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
        strcpy(weather[i].day_code, daily[i]["code_day"].as<const char*>());    
        strcpy(weather[i].night_code, daily[i]["code_night"].as<const char*>());
    }

    // 打印调试信息到串口
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
        Serial.print(weather[i].day_code);
        Serial.print("夜间: ");
        Serial.println(weather[i].night);
        Serial.print(weather[i].night_code);
        Serial.print("降水概率：");
        Serial.println(weather[i].rain);
        Serial.print("风向：");
        Serial.println(weather[i].wind_direction);
        Serial.print("风力：");
        Serial.println(weather[i].wind_scale);
    }
    // 发布事件通知：未来天气已更新
    MQTT_publish("clock/equip1/event","future_update");
}

//当前天气处理函数
void handlerToday(const char* payload)
{ 
    DynamicJsonDocument doc(1024);
    deserializeJson(doc, payload);
    JsonObject results = doc["results"][0];
    
    // 更新最后获取时间
    Today_updateTime = millis();
    
    // 提取当前温度和天气状况文本
    strcpy(weatherToday.Temperature, results["now"]["temperature"].as<const char*>());
    strcpy(weatherToday.weatherText, results["now"]["text"].as<const char*>());
    strcpy(weatherToday.weather_code, results["now"]["code"].as<const char*>());
    
    Serial.println("==========当前天气信息==========");
    Serial.printf("更新时间%d-%d-%d-%02d:%02d:%02d\n",timeInfo.tm_year+1900,timeInfo.tm_mon+1,timeInfo.tm_mday,timeInfo.tm_hour,timeInfo.tm_min,timeInfo.tm_sec);
    Serial.println("-------------------------------");
    Serial.print("当前温度：");
    Serial.println(weatherToday.Temperature);
    Serial.print("当前天气：");
    Serial.println(weatherToday.weatherText);
    Serial.print(weatherToday.weather_code);
    
    // 发布事件通知：当前天气已更新
    MQTT_publish("clock/equip1/event","today_update");
}

const unsigned char* SelectWeatherIcon(const char* weatherText) {
    // 根据天气文本选择对应的图标位图
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
    // 绘制顶部状态栏：时间、城市、语音状态
    u8g2.setFont(u8g2_font_6x12_tf);
    //u8g2.drawHLine(0, 13, 128); 

    //格式化时间 HH:MM
    snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d", timeInfo.tm_hour, timeInfo.tm_min);
    u8g2.drawStr(0, 11, timeBuf);

    // 绘制城市名称，限制长度以防溢出
    String city = state.city;
    if (city.length() > 12) {
        city = city.substring(0, 12);
    }
    int16_t cityWidth = u8g2.getUTF8Width(city.c_str());
    int16_t cityX = max(0, (128 - cityWidth) / 2); // 居中显示
    u8g2.drawUTF8(cityX, 11, city.c_str());

    // 绘制语音状态指示器 (VOC/MUT)
    const char* voice = state.voice_state ? "VOC" : "MUT";
    int16_t voiceWidth = u8g2.getUTF8Width(voice);
    u8g2.drawStr(128 - voiceWidth, 11, voice);
}

void DrawCurrentPage() {
    // 绘制首页：当前天气、温度、传感器数据
    u8g2.firstPage();
    do {
        DrawStatusBar();
        // 获取并绘制天气图标
        const unsigned char* icon = SelectWeatherIcon(weatherToday.weatherText);
        u8g2.drawXBMP(8, 20, 16, 16, icon);

        // 绘制大号温度显示
        u8g2.setFont(u8g2_font_logisoso20_tf);
        snprintf(temperatureBuf, sizeof(temperatureBuf), "%s°C", weatherToday.Temperature);
        u8g2.drawUTF8(36, 44, temperatureBuf);

        // 绘制天气文字描述
        u8g2.setFont(u8g2_font_wqy12_t_gb2312);
        u8g2.drawUTF8(95, 35, weatherToday.weatherText);

        // 绘制室内温湿度传感器数据
        snprintf(sensorBuf, sizeof(sensorBuf), "T:%d°C H:%d%%", (int)temperature, (int)humidity);
        u8g2.drawUTF8(25, 62, sensorBuf);
    } while (u8g2.nextPage());
}

void DrawFuturePage(uint8_t index, const char* label) {
    // 绘制未来天气页面（今日/明日/后天）
    u8g2.firstPage();
    do {
        DrawStatusBar();
        u8g2.setFont(u8g2_font_wqy12_t_gb2312);

        // 绘制标题：标签 + 白天天气
        snprintf(titleBuf, sizeof(titleBuf), "%s", label);
        u8g2.drawUTF8(50, 30, titleBuf);

        // 绘制温度范围
        snprintf(rangeBuf, sizeof(rangeBuf), "%s°C ~ %s°C", weather[index].MinTemp, weather[index].MaxTemp);
        u8g2.drawUTF8(25, 41, rangeBuf);

        // 绘制昼夜天气详情
        snprintf(dayNightBuf, sizeof(dayNightBuf), "日:%s 夜:%s", weather[index].day, weather[index].night);
        uint16_t dayNightWidth = u8g2.getUTF8Width(dayNightBuf);
        uint16_t dayNightX = max(0, (128 - dayNightWidth) / 2);
        u8g2.drawUTF8(dayNightX, 52, dayNightBuf);

        // 绘制降水、风向、风力详情
        snprintf(detailsBuf, sizeof(detailsBuf), "降水:%s%% %s风 %s级", weather[index].rain, weather[index].wind_direction, weather[index].wind_scale);
        u8g2.drawUTF8(0, 63, detailsBuf);
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

void DrawSetupPage() {
    u8g2.firstPage();
    do {
        // 配网界面：不依赖 timeInfo / DrawStatusBar，独立绘制
        u8g2.setFont(u8g2_font_6x12_tf);
        u8g2.drawStr(0, 11, "--:--");
        u8g2.drawStr(128 - u8g2.getUTF8Width("SETUP"), 11, "SETUP");

        // 提示文字
        u8g2.setFont(u8g2_font_wqy12_t_gb2312);
        u8g2.drawUTF8(0, 35, "连接AutoAP并配网");
        u8g2.drawUTF8(0, 50, "请连接WiFi: AutoConnectAP");
    } while (u8g2.nextPage());
}

void DrawPage() {
    // 根据当前页面索引分发绘制任务
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
    // 如果语音功能关闭，直接返回
    if (!state.voice_state) {
        return;
    }

    // 逐词输出：每个部分都是纯GBK，不混用编码
    if (pageIndex == 0) {
        // 首页：通过天气代码获取GBK文本，逐词播报
        Serial1.print(V_NOW);
        Serial1.print(GetWeatherGBK(atoi(weatherToday.weather_code)));
        Serial1.print(V_TEMP);
        Serial1.print(weatherToday.Temperature);
        Serial1.print(V_ROOM);
        Serial1.print(String(temperature, 1));
        Serial1.print(V_HUM);
        Serial1.print((int)humidity);
    } else {
        // 其他页：通过day_code/night_code获取GBK天气文本
        const char* label = pageIndex == 1 ? V_TODAY : (pageIndex == 2 ? V_TOMORROW : V_AFTER);
        Weather &w = weather[pageIndex - 1];
        Serial1.print(label);
        Serial1.print(V_WEATHER);
        Serial1.print(GetWeatherGBK(atoi(w.day_code)));
        Serial1.print(V_NIGHT);
        Serial1.print(GetWeatherGBK(atoi(w.night_code)));
        Serial1.print(V_RANGE);
        Serial1.print(w.MinTemp);
        Serial1.print(V_TO);
        Serial1.print(w.MaxTemp);
        Serial1.print(V_RAIN);
        Serial1.print(w.rain);
        Serial1.print(V_WIND);
        Serial1.print(w.wind_direction);
        Serial1.print(w.wind_scale);
        Serial1.print(V_LEVEL);
    }
    Serial1.println();
}

void CheckButtons() {
   
    bool swRead = digitalRead(BUTTON_SWITCH) == HIGH;
    if (swRead && !switchPressed && millis() - lastSwitchMillis > 50) {
        switchPressed = true;
    }
    if (!swRead && switchPressed) {
        switchPressed = false;
        lastSwitchMillis = millis();
        state.power_on = !state.power_on;
        if (state.power_on) {
            u8g2.setPowerSave(0);  // 唤醒屏幕
            Serial.println("电源打开");
        } else {
            u8g2.setPowerSave(1);  // 关闭屏幕
            state.voice_state = false; // 同时关闭语音
            Serial.println("电源关闭");
        }
        State_publish(true, false, false);
    }

    // remote_mode 下禁用所有本地操控
    if (state.remote_mode) return;

    
    bool pagePressed = digitalRead(BUTTON_PAGE) == HIGH;
    if (pagePressed && !pageButtonPressed && millis() - lastPageButtonMillis > 50) {
        pageButtonPressed = true;
    }
    if (!pagePressed && pageButtonPressed) {
        pageButtonPressed = false;
        lastPageButtonMillis = millis();
        state.page = (state.page + 1) % 4;
        lastAutoSwitch = millis(); // 手动翻页后重置自动计时
        if (state.power_on && state.voice_state) {
            PlayVoiceForPage(state.page);
        }
        State_publish(true, false, false);
    }

    // === 静音按钮
    bool silentRead = digitalRead(BUTTON_SILENT) == HIGH;
    if (silentRead && !silentPressed && millis() - lastSilentMillis > 50) {
        silentPressed = true;
    }
    if (!silentRead && silentPressed) {
        silentPressed = false;
        lastSilentMillis = millis();
        state.voice_state = !state.voice_state;
        Serial.print("语音");
        Serial.println(state.voice_state ? "开启" : "关闭");
        State_publish(true, false, false);
    }
}

void getWeather(String url,enum WeatherType type)
{
    // 私钥：S9iZu2FjVm2I_PwLY

    // 创建安全的WiFi客户端实例
    std::unique_ptr<BearSSL::WiFiClientSecure> client(
        new BearSSL::WiFiClientSecure);

    // 跳过证书验证（适用于自签名证书或简化调试，生产环境建议验证证书）
    client->setInsecure();

    HTTPClient https;

    // 发起HTTPS GET请求
    if (https.begin(*client, url))
    {
        int httpCode = https.GET();

        // 如果请求成功 (200) 或特定错误 (400，有时API返回400但仍有数据或用于调试)
        if (httpCode == 400 || httpCode == 200)
        {
            Serial.print("HTTP Code: ");
            Serial.println(httpCode);
            // 将响应字符串复制到全局payload缓冲区
            https.getString().toCharArray(payload, sizeof(payload));

            Serial.println(payload);
            
            // 根据类型调用相应的处理函数
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
            // 请求失败，打印错误码
            Serial.print("请求失败: ");
            Serial.println(httpCode);
        }

        https.end(); // 释放资源
    }
    delay(0); // 让出 CPU，避免长时间阻塞软看门狗
}

void setup() {
    // 初始化串口通信
    Serial.begin(115200); // 用于调试输出
    Serial1.begin(9600);  // 用于连接语音模块
    
    // 初始化DHT传感器
    dht.begin();
    
    // 配置按钮引脚为上拉输入模式
    pinMode(BUTTON_PAGE, INPUT);
    pinMode(BUTTON_VOICE, INPUT);
    pinMode(BUTTON_SILENT, INPUT);
    pinMode(BUTTON_SWITCH, INPUT);
    
    // 默认开机，电源按钮在 loop 中通过 CheckButtons 切换
    
    // 初始化OLED显示屏
    Wire.begin();
    u8g2.begin();
    
    // 生成初始API URL
    url_update();
    
    // 添加WiFiManager自定义参数：城市和MQTT服务器地址
    wifiManager.addParameter(&custom_city);
    wifiManager.addParameter(&custom_mqtt);
    
    // 显示配网界面
    DrawSetupPage();
    
    // 启动WiFi配置门户，如果连接失败则重启
    if (!wifiManager.startConfigPortal("AutoConnectAP")) {
        Serial.println("WiFi连接失败,重启...");
        delay(3000);
        ESP.restart();
    } 
    
    // 从WiFiManager获取保存的城市和MQTT配置
    state.city = String(custom_city.getValue());
    mqtt_server = String(custom_mqtt.getValue());
    
    // 生成唯一的客户端ID
    randomSeed(micros());
    clientID += String(random(0xffff),HEX);

    // 配置MQTT服务器和回调函数
    client.setServer(mqtt_server.c_str(), 1883);
    client.setCallback(callback);

    // WiFi连接成功提示
    Serial.println("WiFi连接成功!");
    Serial.print("IP地址: ");
    Serial.println(WiFi.localIP());
    
    // 连接MQTT服务器
    connectMQTT();
    
    // 配置NTP时间同步
    configTime(8 * 3600, 0, "ntp.aliyun.com", "cn.ntp.org.cn");
    
    // 等待时间同步成功
    while(!getLocalTime(&timeInfo)){
        Serial.println("时间同步中...");
        delay(500);
    }
    Serial.println("时间同步成功!");
    
    // 首次启动时立即获取天气数据
    Serial.println("开始请求当前天气...");
    getWeather(Today_url,SearchType = Today);
    Serial.println("当前天气请求完成");
    delay(0);
    
    Serial.println("开始请求未来天气...");
    getWeather(Future_url,SearchType = Future);
    Serial.println("未来天气请求完成");
    
    // 配网完成后立即刷新显示，确保从配网界面正确过渡
    if (state.power_on && getLocalTime(&timeInfo, 0)) {
        DrawPage();
    }
    
    // 发布初始状态
    State_publish(true, true, false);
    lastMQTTStateUpdate = millis();
}

void loop() {
    // 主循环
    
    // 检查MQTT连接状态，断开则重连
    if (!client.connected()) {
        connectMQTT();
    }
    client.loop();
    CheckButtons();

    // 自动翻页：开机且本地模式下每20秒切换一次
    if (state.power_on && !state.remote_mode && millis() - lastAutoSwitch >= 20000) {
        state.page = (state.page + 1) % 4;
        lastAutoSwitch = millis();
        if (state.voice_state) {
            PlayVoiceForPage(state.page);
        }
        State_publish(true, false, false);
    }

    if(state.voice_state && state.voice_update)
    {
        PlayVoiceForPage(state.page);
        state.voice_update = false; // 清除标志位
    }

    // 如果标记需要更新未来天气
    if(state.update_future_flag)
    {
        getWeather(Future_url,SearchType = Future);
        state.update_future_flag = false; // 清除标志位
    }
    
    // 如果标记需要更新今日天气
    if(state.update_today_flag)
    {
        getWeather(Today_url,SearchType = Today);
        state.update_today_flag = false; // 清除标志位
    }
    
    // 每10秒更新一次DHT传感器数据并发布
    if(millis() - lastDHTUpdate > 10000) 
    {
        DHT_update();
        State_publish(false, false, true);
        lastDHTUpdate = millis();
    }
    
    // 每1分钟发布一次设备整体状态
    if(millis() - lastMQTTStateUpdate > 60000) 
    {
        State_publish(true, false, true);
        lastMQTTStateUpdate = millis();
    }
    
    // 每隔10分钟自动更新一次当前天气信息
    if (millis() - Today_updateTime > 600000) {
        Serial.println("更新当前天气...");
        getWeather(Today_url,SearchType = Today);
        Serial.println("当前天气更新完成");
        
        // 如果未来天气数据超过6小时未更新，则同时更新未来天气
        if (millis() - Future_updateTime > 21600000) {
            Serial.println("更新未来天气...");
            getWeather(Future_url,SearchType = Future);
            Serial.println("未来天气更新完成");
        }
    }

    // 每200毫秒刷新一次屏幕显示（仅开机时）
    if (millis() - lastDisplayUpdate >= 200) {
        if (state.power_on) {
            // 获取最新时间
            if (getLocalTime(&timeInfo, 0)) {
                DrawPage();
            }
        }
        lastDisplayUpdate = millis();
    }
}

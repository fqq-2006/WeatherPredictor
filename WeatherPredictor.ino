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

#define DHTPIN D4
#define DHTTYPE DHT11
DHT dht(DHTPIN, DHTTYPE);

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
    bool update_today_flag = false;
    bool update_future_flag = false;
} state = {false,0,false,"beijing"};

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
    char buffer[256];
    if(state_update){
        doc["mode"] = (state.remote_mode)?"remote":"local";
        doc["page"] = state.page;
        doc["rssi"] = WiFi.RSSI();
        doc["voicestate"] = state.voice_state;
        serializeJson(doc, buffer);
        MQTT_publish("clock/equip1/state", buffer);
        doc.clear();
    }
    if(city_update){
        doc["city"] = state.city;
        serializeJson(doc, buffer);
        MQTT_publish("clock/equip1/state/city", buffer);
        doc.clear();
    }
    if(sensor_update){
        doc["temperature"] = temperature;
        doc["humidity"] = humidity;
        serializeJson(doc, buffer);
        MQTT_publish("clock/equip1/state/sensor", buffer);
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
        if(!data[3].isNull()&& data[3].as<bool>() != state.voice_state)
        {
            state.voice_update = data[3].as<bool>();
        }
        if(!data[4].isNull()&& data[4].as<bool>() == true)
        {
            state.voice_state = true;
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
    dht.begin();
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

}

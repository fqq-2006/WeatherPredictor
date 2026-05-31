// RemoteControl - 电位器翻页遥控器
// 发送 MQTT control 消息，仅携带翻页信息，其余字段为 null

#include <ESP8266WiFi.h>
#include <WiFiManager.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

#define POT_PIN A0

WiFiManager wifiManager;
String mqtt_server = "192.168.5.4";
const int mqtt_port = 1883;

WiFiManagerParameter custom_mqtt("mqtt", "MQTT服务器地址", "192.168.5.4", 40);

const char* topic_control = "clock/equip1/control";

WiFiClient espClient;
PubSubClient client(espClient);
String clientID = "RemoteControl-";

#define NUM_PAGES 4
const int pageThresholds[NUM_PAGES] = {256, 512, 768, 1025};

int  currentPage  = 0;
int  lastPage     = -1;
unsigned long lastPotRead = 0;
#define POT_READ_INTERVAL 100

void connectMQTT();
int  readPotPage();
void sendPageCommand(int page);

void setup() {
    Serial.begin(115200);
    Serial.println("\n========== 电位器遥控器启动 ==========");

    randomSeed(micros());
    clientID += String(random(0xffff), HEX);

    wifiManager.addParameter(&custom_mqtt);
    wifiManager.setConfigPortalTimeout(180);

    Serial.println("启动配网 AP: RemoteSetup");
    if (!wifiManager.startConfigPortal("RemoteSetup")) {
        Serial.println("WiFi 配网失败, 重启...");
        delay(3000);
        ESP.restart();
    }

    mqtt_server = String(custom_mqtt.getValue());

    Serial.println("WiFi 配网成功!");
    Serial.print("IP 地址: ");
    Serial.println(WiFi.localIP());
    Serial.print("MQTT 服务器: ");
    Serial.println(mqtt_server);

    client.setServer(mqtt_server.c_str(), mqtt_port);
    connectMQTT();

    currentPage = readPotPage();
    lastPage = currentPage;
    Serial.print("初始页面: ");
    Serial.println(currentPage);

    sendPageCommand(currentPage);
}

void loop() {
    if (!client.connected()) {
        connectMQTT();
    }
    client.loop();

    if (millis() - lastPotRead >= POT_READ_INTERVAL) {
        int newPage = readPotPage();

        if (newPage != currentPage) {
            currentPage = newPage;
            Serial.print("电位器翻页 -> 第 ");
            Serial.print(currentPage);
            Serial.println(" 页");

            sendPageCommand(currentPage);
        }

        lastPotRead = millis();
    }
}

void connectMQTT() {
    while (!client.connected()) {
        Serial.print("正在连接 MQTT 服务器...");
        if (client.connect(clientID.c_str())) {
            Serial.println("连接成功");
        } else {
            Serial.print("连接失败, rc=");
            Serial.print(client.state());
            Serial.println(" 5秒后重试...");
            delay(5000);
        }
    }
}

int readPotPage() {
    int raw = analogRead(POT_PIN);
    Serial.println(raw);
    int page = 0;

    for (int i = 0; i < NUM_PAGES; i++) {
        if (raw < pageThresholds[i]) {
            page = i;
            break;
        }
    }

    return page;
}

void sendPageCommand(int page) {
    // 仅填 page，其余字段为 null，服务端通过 isNull() 跳过
    StaticJsonDocument<128> doc;
    JsonArray cmd = doc.createNestedArray("command");

    cmd.add(page);
    cmd.add(nullptr);
    cmd.add(nullptr);
    cmd.add(nullptr);
    cmd.add(nullptr);

    char buffer[128];
    serializeJson(doc, buffer, sizeof(buffer));

    client.publish(topic_control, buffer);
    Serial.print("已发送: ");
    Serial.println(buffer);
}

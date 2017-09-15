
#include <ESP8266WiFi.h>
#include <mDNSResolver.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <PubSubClient.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <EEPROM.h>
#include <math.h>
#include <vector>

#define UNUSED(x) (void)x
//#define SERIAL_OUT
#ifdef SERIAL_OUT
  #define SERIAL(x) Serial.x
#else
  #define SERIAL(x)
#endif

//Secrets
#define OTA_PASSWORD ""
#define OTA_HOSTNAME ""
const String ssid = "";//SET THIS
const String password = "";//SET THIS
const String domain = "local";

//MQTT SETUP (set these)
#define MQTT_CLIENT_NAME ""
#define MQTT_HOSTNAME ""
#define MQTT_IP ""
#define MQTT_PORT 1883 // use 8883 for SSL
const char* MQTT_ORDERS = "";

bool mqtt_led = false;
Adafruit_SSD1306 oled = Adafruit_SSD1306();

//WIFI SETUP
WiFiClient wifi;
WiFiUDP udp;
IPAddress mqtt_ip;

bool wifi_connect() {
  delay(10);
  oled.printf("Connecting to %s", ssid.c_str());
  oled.display();
  SERIAL(printf("Connecting to %s", ssid.c_str()));
  WiFi.begin(ssid.c_str(), password.c_str());
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    oled.print(".");
    oled.display();
    SERIAL(print("."));
  }
  oled.print("\n");
  oled.println(WiFi.localIP());
  oled.display();
  SERIAL(print(WiFi.localIP()));
  SERIAL(print(" ("));
  SERIAL(print(WiFi.subnetMask()));
  SERIAL(print(")\n"));
  delay(500);
  oled.clearDisplay();
  return wifi.connected();
}

// $ per minute
// $ today
typedef struct {
  uint32_t cost;
  uint32_t expire_time;
} order;
float total_money = 0.0f;
float display_money = 0.0f;
std::vector<order> past_orders;
const uint32_t expire_seconds = 5;
const float max_lagtime = 2000.0f;
const float max_discrepancy = 10000.0f;
uint32_t time_counter = 0;

StaticJsonBuffer<2048> jsonBuffer;

void update_display() {
  float discrepancy = (total_money - display_money);
  uint32_t t = millis() - time_counter;
  float pressure = (float)t / max_lagtime;
  if (pressure >= 1.0f)
    display_money = total_money;
  else
    display_money += discrepancy * pressure;

  oled.clearDisplay();
  oled.setCursor(0, 0);
  char money_str[17];
  char postfix;
  if (display_money < 100000.00f) {
    dtostrf(display_money, 8, 2, money_str);
    postfix = ' ';
  } else if (display_money >= 100000.0f && display_money < 100000000.0f) {
    dtostrf(display_money/1000.0f, 8, 2, money_str);
    postfix = 'K';
  } else if (display_money >= 100000000.0f && display_money < 100000000000.0f) {
    dtostrf(display_money/1000000, 8, 2, money_str);
    postfix = 'M'; 
  } else if (display_money >= 100000000000.0f && display_money < 100000000000000.0f) {
    dtostrf(display_money/1000000000, 8, 2, money_str);
    postfix = 'B';
  }
  uint32_t money_per_second = 0;
  std::vector<order>::iterator i = past_orders.begin();
  while(i != past_orders.end()) {
    if (i->expire_time > millis()) {
      money_per_second += i->cost;
      ++i;
    } else {
      i = past_orders.erase(i);
    }
  }
  
  SERIAL(printf("$%s%c\n", &money_str[0], postfix));
  oled.printf("$%s%c", &money_str[0], postfix);
  SERIAL(printf("A$ps %d\n", money_per_second)); 
  oled.printf("$%5d/sec", money_per_second);
  oled.display();
}

//MQTT
PubSubClient client(wifi);
String clientId;
void mqtt_connect() {
  while (!client.connected()) {
    if (client.connect(clientId.c_str())) {
      client.subscribe(MQTT_ORDERS, 1);
      SERIAL(println("MQTT Connected"));
    } else {
      delay(5000);
      SERIAL(printf("MQTT Failure %d\n", client.state()));
    }
  }
}

bool is_topic(const char* t, const char *tt) {
  return strcmp(t, tt) == 0;
}

uint32_t write_timer = 0;
const uint32_t write_rate = 360000;

void mqtt_callback(char *topic, uint8_t* payload, uint32_t len) {
  UNUSED(payload);
  UNUSED(len);
  if (is_topic(topic, MQTT_ORDERS)) {
    digitalWrite(0, mqtt_led ? HIGH : LOW);
    mqtt_led = !mqtt_led;
    JsonObject& root = jsonBuffer.parseObject((char*)payload);
    if(!root.containsKey("spend_amount") || !root.containsKey("tip_amount")) return;
    time_counter = millis();
    float money = (((float)atoi(root["spend_amount"])) + ((float)atoi(root["tip_amount"])))/100.0f;
    past_orders.push_back({((uint32_t)money)/expire_seconds, millis()+(1000 * expire_seconds)});
    total_money += money;
    if (millis() - write_timer > write_rate) { 
      write_totals();
      write_timer = millis();
    }
    update_display();
    jsonBuffer.clear();
    
  }
}

//OTA setup
void setup_ota() {
  // Port defaults to 8266
  ArduinoOTA.setPort(8266);

  // Hostname defaults to esp8266-[ChipID]
  ArduinoOTA.setHostname(OTA_HOSTNAME);

  // No authentication by default
  ArduinoOTA.setPassword((const char *)OTA_PASSWORD);

  ArduinoOTA.onStart([]() {
    oled.clearDisplay();
    oled.println("Start");
    oled.display();
  });
  ArduinoOTA.onEnd([]() {
    oled.clearDisplay();
    oled.println("\nEnd");
    oled.display();
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    oled.clearDisplay();
    oled.printf("Progress: %u%%\r", (progress / (total / 100)));
    oled.display();
  });
  ArduinoOTA.onError([](ota_error_t error) {
    oled.clearDisplay();
    oled.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) oled.println("Auth Failed");
    else if (error == OTA_BEGIN_ERROR) oled.println("Begin Failed");
    else if (error == OTA_CONNECT_ERROR) oled.println("Connect Failed");
    else if (error == OTA_RECEIVE_ERROR) oled.println("Receive Failed");
    else if (error == OTA_END_ERROR) oled.println("End Failed");
    oled.display();
  });
  ArduinoOTA.begin();
}

//==ENTRY==
void setup_connections() {
  wifi_connect();
  while (udp.parsePacket() > 0);
  int res = WiFi.hostByName(MQTT_HOSTNAME, mqtt_ip);
  if (res != 1) {
    mDNSResolver::Resolver resolver(udp);
    resolver.setLocalIP(WiFi.localIP());
    mqtt_ip = resolver.search(MQTT_HOSTNAME);
    if (mqtt_ip == INADDR_NONE) {
      //HARDCODED IP
      oled.println("MQTT:Hardcoded");
      oled.display();
      SERIAL(println("Using hardcoded IP"));
      mqtt_ip.fromString(MQTT_IP);
    }
  }
  clientId = MQTT_CLIENT_NAME;
  clientId += String(random(0xffff), HEX);

  SERIAL(printf("Client: %s\n", clientId.c_str()));
  oled.clearDisplay();
  oled.setCursor(0, 3);
  oled.printf("%s:", clientId.c_str());
  oled.print(mqtt_ip);
  oled.println("");
  client.setServer(mqtt_ip, MQTT_PORT);
  client.setCallback(mqtt_callback);
  udp.stop();
}

int address = 0;
byte value;

//Total money and last average value
void read_totals() {
  EEPROM.get(0, total_money);
}

void write_totals() {
  EEPROM.put(0, total_money);
  EEPROM.commit();
}

void setup() {
#ifdef SERIAL_OUT
  Serial.begin(115200);
#endif
  pinMode(0, OUTPUT);
  digitalWrite(0, LOW);
  oled.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  oled.clearDisplay();
  oled.setCursor(0, 0);
  oled.setTextSize(1);
  oled.setTextColor(WHITE);
  oled.println("Booting");
  oled.display();
  SERIAL(println("Booting"));
  setup_connections();
  setup_ota();
  time_counter = millis();
  EEPROM.begin(512);
  read_totals();
  if (isnan(total_money))
    total_money = 0.0f;
  oled.setCursor(0, 1);
  oled.println("System Up");
  oled.display();
  oled.setCursor(0, 0);
  SERIAL(println("System Up"));
  oled.setTextSize(2);
  delay(1000);
}

void loop() {
  ArduinoOTA.handle();
  mqtt_connect();
  client.loop();
  update_display();
}


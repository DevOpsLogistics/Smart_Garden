#include <Arduino.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <DHT.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
// ==== CHAN KET NOI ====
#define PIN_DHT        13
#define PIN_SOIL       34
#define PIN_WATER      35
#define PIN_LIGHT      32
#define PIN_PUMP       14
#define PIN_LED_GROW   27

#define PIN_BUZZER     25
#define PIN_BTN        26

// ==== THONG SO HE THONG ====
#define DHTTYPE DHT22
int threshold_soil_low   = 40;
int threshold_soil_high  = 70;
int threshold_water_low  = 10;
int threshold_light_low  = 1000;
float threshold_temp_high  = 35.0;
float threshold_hum_high   = 80.0;

// LCD 16x2 I2C (dia chi mac dinh 0x27)
LiquidCrystal_I2C lcd(0x27, 16, 2);
DHT dht(PIN_DHT, DHTTYPE);

// WiFi & MQTT
const char* ssid = "Wokwi-GUEST";
const char* password = "";

const char* mqtt_server = "broker.hivemq.com";
const int mqtt_port = 1883;
const char* topic_state = "smartgarden_v1/state";
const char* topic_cmd   = "smartgarden_v1/cmd";

WiFiClient espClient;
PubSubClient client(espClient);

bool isPumpOn = false;
bool isLedOn = false;
bool isAutoMode = true;
unsigned long lastReadTime = 0;
unsigned long lastScreenSwitch = 0;
int currentScreen = 0;

// Button debounce variables
bool lastBtnState = HIGH;
bool btnState = HIGH;
unsigned long lastDebounceTime = 0;
unsigned long debounceDelay = 50;
#define NUM_SCREENS    3
#define SCREEN_INTERVAL 2000 // Doi man hinh moi 2 giay

// Luu gia tri de hien thi
float g_temp = 0.0;
float g_hum = 0.0;
int g_soil = 0;
int g_water = 0;
int g_light = 0;
float temp_offset = 0.0; // Biến giả lập giảm nhiệt độ khi bật bơm

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String message;
  for (unsigned int i = 0; i < length; i++) {
    message += (char)payload[i];
  }
  
  if (String(topic) == topic_cmd) {
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, message);
    if (!error) {
      String cmd = doc["cmd"].as<String>();
      if (cmd == "auto") {
        isAutoMode = doc["value"].as<bool>();
      } else if (cmd == "pump" && !isAutoMode) {
        isPumpOn = doc["value"].as<bool>();
      } else if (cmd == "led" && !isAutoMode) {
        isLedOn = doc["value"].as<bool>();
      } else if (cmd == "set_threshold_soil") {
        threshold_soil_low = doc["value"].as<int>();
      } else if (cmd == "set_threshold_temp") {
        threshold_temp_high = doc["value"].as<float>();
      }
    }
  }
}

void reconnect() {
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    String clientId = "ESP32Client-";
    clientId += String(random(0xffff), HEX);
    if (client.connect(clientId.c_str())) {
      Serial.println("connected");
      client.subscribe(topic_cmd);
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 2 seconds");
      delay(2000);
    }
  }
}

// Ky tu dac biet: giot nuoc va mat troi
byte dropIcon[8] = {
  0b00100, 0b00100, 0b01110, 0b01110,
  0b11111, 0b11111, 0b11111, 0b01110
};
byte sunIcon[8] = {
  0b00100, 0b10101, 0b01110, 0b11111,
  0b01110, 0b10101, 0b00100, 0b00000
};
byte leafIcon[8] = {
  0b00010, 0b00110, 0b01110, 0b11110,
  0b01110, 0b00110, 0b00010, 0b00000
};

void displayScreen(int screen) {
  lcd.clear();

  switch (screen) {
    case 0: // Man hinh 1: Do am dat & Muc nuoc
      lcd.setCursor(0, 0);
      lcd.write(0); // drop icon
      lcd.print("Dat:");
      lcd.print(g_soil);
      lcd.print("%");

      lcd.setCursor(9, 0);
      lcd.print("Nc:");
      lcd.print(g_water);
      lcd.print("%");

      lcd.setCursor(0, 1);
      lcd.print("Bom:");
      lcd.print(isPumpOn ? "BAT " : "TAT ");
      lcd.print("Den:");
      lcd.print(isLedOn ? "BAT" : "TAT");
      break;

    case 1: // Man hinh 2: Nhiet do & Do am khong khi
      lcd.setCursor(0, 0);
      lcd.print("Nhiet:");
      lcd.print(g_temp, 1);
      lcd.print((char)223); // Ky tu do (°)
      lcd.print("C");

      lcd.setCursor(0, 1);
      lcd.print("DoAm KK:");
      lcd.print(g_hum, 1);
      lcd.print("%");
      break;

    case 2: // Man hinh 3: Anh sang & Canh bao
      lcd.setCursor(0, 0);
      lcd.write(1); // sun icon
      lcd.print("Sang:");
      lcd.print(g_light);

      lcd.setCursor(0, 1);
      if (g_water <= threshold_water_low && g_soil < threshold_soil_low) {
        lcd.print("!! HET NUOC !!");
      } else if (g_temp > threshold_temp_high) {
        lcd.print("!! QUA NONG !!");
      } else {
        lcd.write(2); // leaf icon
        lcd.print(" He thong OK ");
        lcd.write(2);
      }
      break;
  }
}

void setup() {
  Serial.begin(115200);
  Serial.println("=== SMART GARDEN IoT ===");
  Serial.println("System starting...");

  pinMode(PIN_PUMP, OUTPUT);
  pinMode(PIN_LED_GROW, OUTPUT);
  pinMode(PIN_BUZZER, OUTPUT);
  pinMode(PIN_BTN, INPUT_PULLUP);
  
  digitalWrite(PIN_PUMP, LOW);
  digitalWrite(PIN_LED_GROW, LOW);
  digitalWrite(PIN_BUZZER, LOW);

  pinMode(PIN_SOIL, INPUT);
  pinMode(PIN_WATER, INPUT);
  pinMode(PIN_LIGHT, INPUT);

  dht.begin();

  // Khoi tao LCD
  lcd.init();
  lcd.backlight();
  lcd.createChar(0, dropIcon);
  lcd.createChar(1, sunIcon);
  lcd.createChar(2, leafIcon);

  // Man hinh chao
  lcd.setCursor(0, 0);
  lcd.print("  SMART GARDEN  ");
  lcd.setCursor(0, 1);
  lcd.print("   Connecting   ");

  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(100);
  }
  
  lcd.setCursor(0, 1);
  lcd.print(" WiFi Connected ");
  delay(1000);
  lcd.clear();

  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(mqttCallback);
}

void loop() {
  unsigned long currentMillis = millis();

  if (!client.connected()) {
    reconnect();
  }
  client.loop();

  // Doc cam bien moi giay
  if (currentMillis - lastReadTime >= 1000) {
    lastReadTime = currentMillis;

    float h = dht.readHumidity();
    float t = dht.readTemperature();

    int rawSoil = analogRead(PIN_SOIL);
    int rawWater = analogRead(PIN_WATER);
    int rawLight = analogRead(PIN_LIGHT);

    g_soil = map(rawSoil, 0, 4095, 0, 100);
    g_water = map(rawWater, 0, 4095, 0, 100);
    g_light = rawLight;

    if (isnan(h) || isnan(t)) {
      h = 0;
      t = 0;
    }

    // Giả lập làm mát: Nếu bơm đang bật, nhiệt độ từ từ giảm xuống (tối đa giảm 10 độ)
    if (isPumpOn && temp_offset < 10.0) {
      temp_offset += 0.2; // Giảm 0.2 độ mỗi giây
    } else if (!isPumpOn && temp_offset > 0.0) {
      temp_offset -= 0.1; // Phục hồi nhiệt độ từ từ khi tắt bơm
      if (temp_offset < 0) temp_offset = 0;
    }

    g_temp = t - temp_offset;
    g_hum = h;

    bool hasWarning = false;

    // Logic dieu khien
    if (isAutoMode) {
      // Logic may bom
      if (g_soil < threshold_soil_low) {
        if (g_water > threshold_water_low) {
          isPumpOn = true;
        } else {
          isPumpOn = false;
          hasWarning = true;
          Serial.println("[CANH BAO] MUC NUOC QUA THAP! Khong the bat may bom.");
        }
      } else if (g_soil > threshold_soil_high) {
        isPumpOn = false;
      } else {
        if (isPumpOn && g_water <= threshold_water_low) {
          isPumpOn = false;
          hasWarning = true;
          Serial.println("[CANH BAO KHAN] Het nuoc trong luc dang tuoi! Ngat bom.");
        }
      }

      // Logic den LED
      if (g_light < threshold_light_low) {
        isLedOn = true;
      } else {
        isLedOn = false;
      }
    } else {
      // Che do thu cong: kiem tra an toan
      if (isPumpOn && g_water <= threshold_water_low) {
        isPumpOn = false;
        hasWarning = true;
      }
    }

    // Nếu nhiệt độ cao nhưng máy bơm đang bật (đang tưới/làm mát) thì im lặng còi
    if (g_temp > threshold_temp_high && !isPumpOn) {
      hasWarning = true;
    }

    if (hasWarning) {
      tone(PIN_BUZZER, 1000, 200);
    } else if (g_water <= 20 && g_water > 10) {
      // Cảnh báo sắp hết nước (Mực nước từ 11% đến 20%)
      // Kêu tít... tít... chậm hơn để phân biệt với lỗi Hết nước
      if (currentMillis % 2000 < 200) {
        tone(PIN_BUZZER, 1500, 100);
      }
    }

    digitalWrite(PIN_PUMP, isPumpOn ? HIGH : LOW);
    digitalWrite(PIN_LED_GROW, isLedOn ? HIGH : LOW);

    // Gui du lieu qua MQTT
    JsonDocument doc;
    doc["type"] = "state";
    doc["soil"] = g_soil;
    doc["water"] = g_water;
    doc["temp"] = g_temp;
    doc["hum"] = g_hum;
    doc["light"] = g_light;
    doc["pump"] = isPumpOn;
    doc["led"] = isLedOn;
    doc["auto"] = isAutoMode;
    
    String jsonString;
    serializeJson(doc, jsonString);
    client.publish(topic_state, jsonString.c_str());

    // In ra Serial de theo doi
    Serial.printf("Soil:%d%% Water:%d%% Temp:%.1fC Hum:%.1f%% Light:%d | Pump:%s LED:%s Mode:%s\n",
      g_soil, g_water, g_temp, g_hum, g_light,
      isPumpOn ? "ON" : "OFF", isLedOn ? "ON" : "OFF", isAutoMode ? "AUTO" : "MANUAL");
  }


  // Xu ly Nut nhan voi chong doi
  bool reading = digitalRead(PIN_BTN);
  if (reading != lastBtnState) {
    lastDebounceTime = currentMillis;
  }
  if ((currentMillis - lastDebounceTime) > debounceDelay) {
    if (reading != btnState) {
      btnState = reading;
      if (btnState == LOW) {
        // Chuyen che do sang thu cong va toggle bom
        isAutoMode = false;
        isPumpOn = !isPumpOn;
        Serial.println("Nut duoc nhan: Chuyen sang MANUAL, Toggle Pump");
        
        // Gui trang thai moi ngay lap tuc qua MQTT
        JsonDocument doc;
        doc["type"] = "state";
        doc["soil"] = g_soil;
        doc["water"] = g_water;
        doc["temp"] = g_temp;
        doc["hum"] = g_hum;
        doc["light"] = g_light;
        doc["pump"] = isPumpOn;
        doc["led"] = isLedOn;
        doc["auto"] = isAutoMode;
        
        String jsonString;
        serializeJson(doc, jsonString);
        client.publish(topic_state, jsonString.c_str());
      }
    }
  }
  lastBtnState = reading;

  // Chuyen man hinh LCD moi 2 giay
  if (currentMillis - lastScreenSwitch >= SCREEN_INTERVAL) {
    lastScreenSwitch = currentMillis;
    displayScreen(currentScreen);
    currentScreen = (currentScreen + 1) % NUM_SCREENS;
  }
}

#include <Arduino.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <ElegantOTA.h>
#include <Webpage.h>
#include <FastLED.h>
#include "embedded_html.h"

// Ustawienia diod LED
#define NUM_LEDS 1        // Liczba diod w pasku/pierścieniu
#define DATA_PIN 48       // Pin GPIO, do którego podłączono linię danych DIN
#define LED_TYPE WS2812B  // Typ diody LED
#define COLOR_ORDER GRB   // Kolejność kolorów (zależy od producenta)

// Definiowanie tablicy diod
CRGB leds[NUM_LEDS];

// Replace with your network credentials
const char* ssid = "ZTE_34FC8B";
const char* password = "H28V254H84";

const int SIZE = 15;
const int RELAY_PIN = 3;     // the Arduino pin, which connects to the IN pin of relay
bool automatic = true;
unsigned long previousMillis = 0;
unsigned long currentMillis = 0;
bool isOn = false;
int sensorValue = 0;  // value read from the pot
const unsigned long goAutomaticAfter = 900000;
float voltageSum = 0;
float avgVoltage = 0;
float level = 0;
float voltages[SIZE] = {0};
int i = 0;
Webpage page = Webpage();

// Create AsyncWebServer object on port 80
AsyncWebServer server(80);

// Initialize WiFi
void initWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi ..");
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print('.');
    delay(1000);
  }
  Serial.println(WiFi.localIP());
}

void handleAnalogReading() {
  int sensorValue = analogRead(A0);
  Serial.println("sensorValue: ");
  Serial.print(sensorValue);
  float voltage = sensorValue * (5 / 4095.0);
  voltageSum = voltageSum - voltages[i] + voltage;
  voltages[i] = voltage;
  avgVoltage = voltageSum / SIZE;
  level = (avgVoltage * 200.0) / 5;
  i++;
  if (i >= SIZE) {
    i = 0;
  }
}

void handleAutomaticMode() {
  unsigned long diff = currentMillis - previousMillis;
  if ((automatic == false) && (diff) >= goAutomaticAfter) {
    automatic = true;
  }
  if (automatic) {
    if (level > 130) {
      digitalWrite(RELAY_PIN, HIGH);
      leds[0] = CRGB(0, 255, 0); 
      FastLED.show(); 
      isOn = true;
    } else {
      if (level <= 126) {
        digitalWrite(RELAY_PIN, LOW);
        leds[0] = CRGB(255, 0, 0); 
        FastLED.show(); 
        isOn = false;
      }
    }
  }
}

void setup() {
  // Inicjalizacja biblioteki FastLED
  FastLED.addLeds<LED_TYPE, DATA_PIN, COLOR_ORDER>(leds, NUM_LEDS);
  
  // Ustawienie jasności diod
  FastLED.setBrightness(50);
  pinMode(RELAY_PIN, OUTPUT);
  leds[0] = CRGB(255, 0, 0); 
  FastLED.show(); 

  Serial.begin(9600);
  initWiFi();

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/html", INDEX_HTML);
  });
  server.on("/g;(n3", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/html", INDEX_HTML);
  });
  server.on("/api/status", HTTP_GET, [](AsyncWebServerRequest *request) {
    std::string json = page.buildJson(isOn, level, currentMillis, previousMillis, automatic, goAutomaticAfter);
    request->send(200, "application/json", String(json.c_str()));
  });
  server.on("/g;(n3WLACZ", HTTP_GET, [](AsyncWebServerRequest *request) {
    digitalWrite(RELAY_PIN, HIGH);
    leds[0] = CRGB(0, 255, 0);
    FastLED.show();
    previousMillis = currentMillis;
    isOn = true;
    automatic = false;
    std::string json = page.buildJson(isOn, level, currentMillis, previousMillis, automatic, goAutomaticAfter);
    request->send(200, "application/json", String(json.c_str()));
  });
  server.on("/g;(n3WYLACZ", HTTP_GET, [](AsyncWebServerRequest *request) {
    previousMillis = currentMillis;
    digitalWrite(RELAY_PIN, LOW);
    leds[0] = CRGB(255, 0, 0);
    FastLED.show();
    isOn = false;
    automatic = false;
    std::string json = page.buildJson(isOn, level, currentMillis, previousMillis, automatic, goAutomaticAfter);
    request->send(200, "application/json", String(json.c_str()));
  });
  server.on("/g;(n3AUTOMATYCZNY", HTTP_GET, [](AsyncWebServerRequest *request) {
    previousMillis = 0UL;
    automatic = true;
    std::string json = page.buildJson(isOn, level, currentMillis, previousMillis, automatic, goAutomaticAfter);
    request->send(200, "application/json", String(json.c_str()));
  });
  server.begin();

  ElegantOTA.begin(&server, "admin", "admin");    // Start ElegantOTA
}

void loop() {
  ElegantOTA.loop();
  currentMillis = millis();
  handleAnalogReading();
  handleAutomaticMode();
}

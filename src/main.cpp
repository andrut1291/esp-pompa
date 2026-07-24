#include <Arduino.h>
#include <WiFi.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <ElegantOTA.h>
#include <Webpage.h>
#include <FastLED.h>
#include <Preferences.h>
#include "embedded_html.h"

#define NUM_LEDS 1
#define DATA_PIN 48
#define LED_TYPE WS2812B
#define COLOR_ORDER GRB

CRGB leds[NUM_LEDS];

#define BOOT_BUTTON_PIN 0
#define DEBOUNCE_MS 30
#define AP_SSID "ESP-Pompa / 192.168.4.1"
#define WIFI_TIMEOUT_MS 10000

bool configPortalActive = false;
bool connectingToWifi = false;
unsigned long connectStartTime = 0;
unsigned long apShutdownTime = 0;
Preferences preferences;

unsigned long lastPressTime = 0;
bool lastButtonReading = HIGH;
bool buttonState = HIGH;
unsigned long lastDebounceTime = 0;

const int SIZE = 15;
const int RELAY_PIN = 3;
bool automatic = true;
unsigned long previousMillis = 0;
unsigned long currentMillis = 0;
bool isOn = false;
const unsigned long goAutomaticAfter = 900000;
float voltageSum = 0;
float avgVoltage = 0;
float level = 0;
float voltages[SIZE] = {0};
int i = 0;
Webpage page = Webpage();

AsyncWebServer server(80);
DNSServer dnsServer;

static const IPAddress apIP(192, 168, 4, 1);
static const IPAddress subnet(255, 255, 255, 0);

void saveWifiCreds(const String& ssid, const String& pass) {
  preferences.begin("wifi", false);
  preferences.putString("ssid", ssid);
  preferences.putString("pass", pass);
  preferences.end();
}

void clearWifiCreds() {
  preferences.begin("wifi", false);
  preferences.remove("ssid");
  preferences.remove("pass");
  preferences.end();
}

String getSavedSsid() {
  preferences.begin("wifi", true);
  String s = preferences.getString("ssid", "");
  preferences.end();
  return s;
}

String getSavedPass() {
  preferences.begin("wifi", true);
  String p = preferences.getString("pass", "");
  preferences.end();
  return p;
}

String getAuthPassword() {
  preferences.begin("app", true);
  String p = preferences.getString("auth_pass", "admin");
  preferences.end();
  return p;
}

void setAuthPassword(const String& pw) {
  preferences.begin("app", false);
  preferences.putString("auth_pass", pw);
  preferences.end();
}

void initWiFi() {
  String ssid = getSavedSsid();
  String pass = getSavedPass();

  if (ssid.length() > 0) {
    WiFi.mode(WIFI_STA);
    WiFi.begin(ssid.c_str(), pass.c_str());

    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < WIFI_TIMEOUT_MS) {
      delay(100);
    }
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println(WiFi.localIP());
    MDNS.begin("pompa");
  } else {
    WiFi.mode(WIFI_AP);
    WiFi.softAPConfig(apIP, apIP, subnet);
    WiFi.softAP(AP_SSID);
    delay(100);
    MDNS.begin("pompa");
    dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
    dnsServer.start(53, "*", apIP);
    configPortalActive = true;
    Serial.print("AP mode: ESP-Pompa @ ");
    Serial.println(apIP);
  }
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

void handleBootButton() {
  unsigned long now = millis();
  bool reading = digitalRead(BOOT_BUTTON_PIN);

  if (reading != lastButtonReading) {
    lastDebounceTime = now;
  }

  if ((now - lastDebounceTime) > DEBOUNCE_MS) {
    if (reading != buttonState) {
      buttonState = reading;
      if (buttonState == LOW) {
        lastPressTime = now;
      }
    }

    if (buttonState == LOW && lastPressTime > 0 && (now - lastPressTime) >= 4000) {
      clearWifiCreds();
      leds[0] = CRGB(255, 0, 0);
      FastLED.show();
      delay(200);
      ESP.restart();
    }
  }
  lastButtonReading = reading;
}

void handleBlueBlink() {
  static unsigned long ledTimer = 0;
  if (currentMillis - ledTimer > 500) {
    if (leds[0] == CRGB(0, 0, 255)) {
      leds[0] = CRGB(0, 0, 0);
    } else {
      leds[0] = CRGB(0, 0, 255);
    }
    FastLED.show();
    ledTimer = currentMillis;
  }
}

void setup() {
  FastLED.addLeds<LED_TYPE, DATA_PIN, COLOR_ORDER>(leds, NUM_LEDS);
  FastLED.setBrightness(50);
  pinMode(RELAY_PIN, OUTPUT);
  leds[0] = CRGB(255, 0, 0); 
  FastLED.show(); 

  Serial.begin(9600);

  pinMode(BOOT_BUTTON_PIN, INPUT_PULLUP);
  if (digitalRead(BOOT_BUTTON_PIN) == LOW) {
    clearWifiCreds();
    delay(100);
  }

  initWiFi();

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(200, "text/html; charset=utf-8", INDEX_HTML);
  });
  server.on("/api/status", HTTP_GET, [](AsyncWebServerRequest *request) {
    std::string json = page.buildJson(isOn, level, currentMillis, previousMillis, automatic, goAutomaticAfter);
    request->send(200, "application/json", String(json.c_str()));
  });
  server.on("/api/auth", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (request->arg("password") == getAuthPassword()) {
      request->send(200, "application/json", "{\"status\":\"ok\"}");
    } else {
      request->send(401, "application/json", "{\"status\":\"error\"}");
    }
  });
  server.on("/api/control", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (request->arg("password") != getAuthPassword()) {
      request->send(401, "application/json", "{\"status\":\"unauthorized\"}");
      return;
    }
    String action = request->arg("action");
    if (action == "WLACZ") {
      digitalWrite(RELAY_PIN, HIGH);
      leds[0] = CRGB(0, 255, 0);
      FastLED.show();
      previousMillis = currentMillis;
      isOn = true;
      automatic = false;
    } else if (action == "WYLACZ") {
      previousMillis = currentMillis;
      digitalWrite(RELAY_PIN, LOW);
      leds[0] = CRGB(255, 0, 0);
      FastLED.show();
      isOn = false;
      automatic = false;
    } else if (action == "AUTOMATYCZNY") {
      previousMillis = 0UL;
      automatic = true;
    }
    std::string json = page.buildJson(isOn, level, currentMillis, previousMillis, automatic, goAutomaticAfter);
    request->send(200, "application/json", String(json.c_str()));
  });
  server.on("/api/wifi/scan", HTTP_GET, [](AsyncWebServerRequest *request) {
    static bool scanActive = false;
    int n = WiFi.scanComplete();
    if (n == WIFI_SCAN_RUNNING) {
      request->send(200, "application/json", "{\"status\":\"scanning\"}");
      return;
    }
    if (n >= 0 && scanActive) {
      scanActive = false;
      String json = "{\"status\":\"done\",\"networks\":[";
      for (int i = 0; i < n; i++) {
        if (i > 0) json += ",";
        String ssid = WiFi.SSID(i);
        ssid.replace("\\", "\\\\");
        ssid.replace("\"", "\\\"");
        json += "{\"ssid\":\"" + ssid + "\",\"rssi\":" + WiFi.RSSI(i) + "}";
      }
      json += "]}";
      WiFi.scanDelete();
      request->send(200, "application/json", json);
      return;
    }
    scanActive = true;
    WiFi.scanDelete();
    WiFi.mode(WIFI_AP_STA);
    delay(50);
    WiFi.scanNetworks(true);
    request->send(200, "application/json", "{\"status\":\"scanning\"}");
  });
  server.on("/api/wifi", HTTP_POST, [](AsyncWebServerRequest *request) {
    String ssid = request->arg("ssid");
    String pass = request->arg("password");
    String authPass = request->arg("auth_pass");
    if (ssid.length() > 0) {
      saveWifiCreds(ssid, pass);
      if (authPass.length() > 0) setAuthPassword(authPass);
      request->send(200, "application/json", "{\"status\":\"ok\"}");
      WiFi.disconnect();
      WiFi.mode(WIFI_AP_STA);
      delay(50);
      WiFi.begin(ssid.c_str(), pass.c_str());
      connectingToWifi = true;
      connectStartTime = millis();
    } else {
      request->send(400, "application/json", "{\"status\":\"error\",\"message\":\"SSID required\"}");
    }
  });
  server.on("/api/wifi/status", HTTP_GET, [](AsyncWebServerRequest *request) {
    String savedSsid = getSavedSsid();
    String json = "{\"configured\":" + String(savedSsid.length() > 0 ? "true" : "false") +
                  ",\"ssid\":\"" + savedSsid + "\"" +
                  ",\"connected\":" + String(WiFi.status() == WL_CONNECTED ? "true" : "false") +
                  ",\"ip\":\"" + WiFi.localIP().toString() + "\"" +
                  ",\"connecting\":" + String(connectingToWifi ? "true" : "false") + "}";
    request->send(200, "application/json", json);
  });
  server.on("/api/wifi/disable-ap", HTTP_POST, [](AsyncWebServerRequest *request) {
    WiFi.softAPdisconnect(true);
    dnsServer.stop();
    WiFi.mode(WIFI_STA);
    configPortalActive = false;
    apShutdownTime = 0;
    request->send(200, "application/json", "{\"status\":\"ok\"}");
  });
  server.onNotFound([](AsyncWebServerRequest *request) {
    if (configPortalActive) {
      request->redirect("http://192.168.4.1/");
    } else {
      request->send(404, "text/plain", "Not found");
    }
  });

  server.begin();

  ElegantOTA.begin(&server, "admin", "admin");
}

void loop() {
  ElegantOTA.loop();
  currentMillis = millis();

  handleBootButton();

  if (configPortalActive) {
    dnsServer.processNextRequest();
    handleBlueBlink();

    if (connectingToWifi) {
      wl_status_t status = WiFi.status();
      if (status == WL_CONNECTED) {
        connectingToWifi = false;
        MDNS.begin("pompa");
        apShutdownTime = millis() + 30000;
      } else if (status == WL_CONNECT_FAILED || millis() - connectStartTime > WIFI_TIMEOUT_MS) {
        connectingToWifi = false;
        WiFi.mode(WIFI_AP);
        WiFi.softAPConfig(apIP, apIP, subnet);
        WiFi.softAP(AP_SSID);
        delay(100);
        MDNS.begin("pompa");
        dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
        dnsServer.start(53, "*", apIP);
      }
    }

    if (apShutdownTime > 0 && millis() > apShutdownTime) {
      apShutdownTime = 0;
      WiFi.softAPdisconnect(true);
      dnsServer.stop();
      WiFi.mode(WIFI_STA);
      configPortalActive = false;
    }

    return;
  }

  handleAnalogReading();
  handleAutomaticMode();
}

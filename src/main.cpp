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
#include "tls_redirect.h"

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

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
TLSRedirectServer tlsRedirect;

void tlsRedirectTask(void *param) {
  for (;;) {
    if (configPortalActive) {
      tlsRedirect.handle();
    }
    vTaskDelay(20 / portTICK_PERIOD_MS);
  }
}

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

const unsigned int MAX_AUTH_ATTEMPTS = 5;
const unsigned long AUTH_LOCKOUT_MS = 30000;
unsigned int failedAuthAttempts = 0;
unsigned long lastAuthFailMs = 0;

bool authLocked() {
  return failedAuthAttempts >= MAX_AUTH_ATTEMPTS && (millis() - lastAuthFailMs) < AUTH_LOCKOUT_MS;
}

void noteAuthFailure() {
  if (failedAuthAttempts < 1000) failedAuthAttempts++;
  lastAuthFailMs = millis();
}

void noteAuthSuccess() {
  failedAuthAttempts = 0;
  lastAuthFailMs = 0;
}

bool checkCtrlPassword(const String& pass) {
  if (pass.length() == 0) return false;
  if (pass == getAuthPassword()) {
    noteAuthSuccess();
    return true;
  }
  noteAuthFailure();
  return false;
}

bool wifiConfigured() {
  return getSavedSsid().length() > 0;
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
        leds[0] = CRGB(0, 0, 0);
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
  FastLED.show();

  Serial.begin(9600);
  portalDebug("[boot] portal build (tls-redirect + logging)");

  pinMode(BOOT_BUTTON_PIN, INPUT_PULLUP);
  if (digitalRead(BOOT_BUTTON_PIN) == LOW) {
    clearWifiCreds();
    delay(100);
  }

  initWiFi();

  Serial.printf("[boot] mode=%d portal=%d sta=%s\n", WiFi.getMode(), configPortalActive,
                WiFi.localIP().toString().c_str());
  portalDebug("[boot] mode=%d portal=%d sta=%s", WiFi.getMode(), configPortalActive,
              WiFi.localIP().toString().c_str());
  if (configPortalActive) {
    tlsRedirect.begin();
  }
  xTaskCreatePinnedToCore(tlsRedirectTask, "tlsRedirect", 16384, nullptr, 1, nullptr, 1);

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    portalDebug("[http] GET / from %s", request->client()->remoteIP().toString().c_str());
    request->send(200, "text/html; charset=utf-8", INDEX_HTML);
  });
  server.on("/api/debug", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (request->arg("password") != getAuthPassword()) {
      request->send(401, "text/plain", "unauthorized");
      return;
    }
    request->send(200, "text/plain", getPortalDebugLog());
  });
  server.on("/api/status", HTTP_GET, [](AsyncWebServerRequest *request) {
    std::string json = page.buildJson(isOn, level, currentMillis, previousMillis, automatic, goAutomaticAfter);
    json.insert(json.size() - 1, ",\"ver\":3");
    request->send(200, "application/json", String(json.c_str()));
  });
  server.on("/api/auth", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (authLocked()) {
      request->send(429, "application/json", "{\"status\":\"too_many_attempts\"}");
      return;
    }
    if (request->arg("password") == getAuthPassword()) {
      noteAuthSuccess();
      request->send(200, "application/json", "{\"status\":\"ok\"}");
    } else {
      noteAuthFailure();
      request->send(401, "application/json", "{\"status\":\"error\"}");
    }
  });
  server.on("/api/control", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (authLocked()) {
      request->send(429, "application/json", "{\"status\":\"too_many_attempts\"}");
      return;
    }
    if (!checkCtrlPassword(request->arg("password"))) {
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
      leds[0] = CRGB(0, 0, 0);
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
    if (wifiConfigured() && !checkCtrlPassword(request->arg("password"))) {
      request->send(401, "application/json", "{\"status\":\"unauthorized\"}");
      return;
    }
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
    if (wifiConfigured() && !checkCtrlPassword(request->arg("ctrl_pass"))) {
      request->send(401, "application/json", "{\"status\":\"unauthorized\"}");
      return;
    }
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
    if (!checkCtrlPassword(request->arg("password"))) {
      request->send(401, "application/json", "{\"status\":\"unauthorized\"}");
      return;
    }
    WiFi.softAPdisconnect(true);
    dnsServer.stop();
    tlsRedirect.stop();
    WiFi.mode(WIFI_STA);
    configPortalActive = false;
    apShutdownTime = 0;
    request->send(200, "application/json", "{\"status\":\"ok\"}");
  });
  server.onNotFound([](AsyncWebServerRequest *request) {
    portalDebug("[http] %s %s from %s", request->methodToString(), request->url().c_str(),
                request->client()->remoteIP().toString().c_str());
    if (configPortalActive) {
      request->redirect("http://192.168.4.1/");
    } else {
      request->send(404, "text/plain", "Not found");
    }
  });

  server.begin();

  ElegantOTA.begin(&server, "4pd9zxvaq6", "ddaf8X1mFxGFpSZtYVMkNokl");
  ElegantOTA.onStart([](){ portalDebug("[ota] start"); });
  ElegantOTA.onProgress([](size_t cur, size_t tot){
    static size_t last = 0;
    if (cur - last > 200000) { last = cur; portalDebug("[ota] progress %u/%u", cur, tot); }
  });
  ElegantOTA.onEnd([](bool ok){ portalDebug("[ota] end ok=%d", ok); });
}

void loop() {
  ElegantOTA.loop();
  currentMillis = millis();

  handleBootButton();

  if (configPortalActive) {
    dnsServer.processNextRequest();
    handleBlueBlink();
    static unsigned long portalLogTimer = 0;
    if (currentMillis - portalLogTimer > 3000) {
      portalLogTimer = currentMillis;
      portalDebug("[portal] active, sta_conn=%u, ip=%s",
                  WiFi.softAPgetStationNum(), WiFi.softAPIP().toString().c_str());
    }
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
      tlsRedirect.stop();
      WiFi.mode(WIFI_STA);
      configPortalActive = false;
    }

    return;
  }

  handleAnalogReading();
  handleAutomaticMode();
}

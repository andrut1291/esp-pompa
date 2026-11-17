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
#include "secrets.h"

#ifndef ELEGANT_OTA_USER
#error "Missing include/secrets.h - copy include/secrets.h.example to include/secrets.h and fill it in"
#endif

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
#define WIFI_RETRY_INTERVAL_MS 30000UL   // retry the saved network this often
#define STA_DOWN_PORTAL_MS 300000UL      // reopen the setup AP after 5 min without WiFi
#define BOOT_RESET_HOLD_MS 2000UL        // hold button this long to wipe credentials
#define SAMPLE_INTERVAL_MS 100UL

bool configPortalActive = false;
bool connectingToWifi = false;
unsigned long connectStartTime = 0;
unsigned long apShutdownTime = 0;
unsigned long wifiRetryTimer = 0;
unsigned long staDownSince = 0;
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
unsigned long goAutomaticAfter = 900000UL;
float voltageSum = 0;
float avgVoltage = 0;
float level = 0;
float voltages[SIZE] = {0};
int i = 0;
int sampleCount = 0;
unsigned long lastSampleMillis = 0;
unsigned long lastSensorLog = 0;
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

// The Preferences object is a single shared handle: the web handlers run in the
// AsyncTCP task while WiFi retries run in the Arduino loop task. Calling
// begin()/end() from both at once closes the handle under the other task's feet
// and makes reads silently fall back to their defaults (e.g. the auth password
// becoming "admin"). Serialise every access, and keep the WiFi credentials in
// RAM so the loop never needs NVS at all.
static SemaphoreHandle_t prefsMutex = nullptr;

void prefsLock() {
  if (prefsMutex) xSemaphoreTake(prefsMutex, portMAX_DELAY);
}

void prefsUnlock() {
  if (prefsMutex) xSemaphoreGive(prefsMutex);
}

String cachedSsid;
String cachedPass;

String getSavedSsid();
String getSavedPass();

void loadWifiCredsCache() {
  cachedSsid = getSavedSsid();
  cachedPass = getSavedPass();
}

void saveWifiCreds(const String& ssid, const String& pass) {
  prefsLock();
  preferences.begin("wifi", false);
  preferences.putString("ssid", ssid);
  preferences.putString("pass", pass);
  preferences.end();
  prefsUnlock();
  cachedSsid = ssid;
  cachedPass = pass;
}

void clearWifiCreds() {
  prefsLock();
  preferences.begin("wifi", false);
  preferences.remove("ssid");
  preferences.remove("pass");
  preferences.end();
  prefsUnlock();
  cachedSsid = "";
  cachedPass = "";
}

String getSavedSsid() {
  prefsLock();
  preferences.begin("wifi", true);
  String s = preferences.getString("ssid", "");
  preferences.end();
  prefsUnlock();
  return s;
}

String getSavedPass() {
  prefsLock();
  preferences.begin("wifi", true);
  String p = preferences.getString("pass", "");
  preferences.end();
  prefsUnlock();
  return p;
}

String getAuthPassword() {
  prefsLock();
  preferences.begin("app", true);
  String p = preferences.getString("auth_pass", "admin");
  preferences.end();
  prefsUnlock();
  return p;
}

void setAuthPassword(const String& pw) {
  prefsLock();
  preferences.begin("app", false);
  preferences.putString("auth_pass", pw);
  preferences.end();
  prefsUnlock();
}

unsigned long getAutoMinutes() {
  prefsLock();
  preferences.begin("app", true);
  unsigned long m = preferences.getULong("auto_min", 15UL);
  preferences.end();
  prefsUnlock();
  return m;
}

void setAutoMinutes(unsigned long minutes) {
  goAutomaticAfter = minutes * 60000UL;
  prefsLock();
  preferences.begin("app", false);
  preferences.putULong("auto_min", minutes);
  preferences.end();
  prefsUnlock();
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
  return cachedSsid.length() > 0;
}

// Bring the setup AP up without killing the STA interface: the board keeps
// retrying the saved network in the background and leaves the portal as soon
// as it connects (this is what makes it recover after a power cut, when the
// router is still booting while the ESP32 is already up).
void startPortalAp() {
  if (configPortalActive) return;

  portalDebug("[portal] starting setup AP");
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAPConfig(apIP, apIP, subnet);
  WiFi.softAP(AP_SSID);
  delay(100);
  MDNS.begin("pompa");
  dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
  dnsServer.start(53, "*", apIP);
  tlsRedirect.begin();

  configPortalActive = true;
  connectingToWifi = false;
  apShutdownTime = 0;
  wifiRetryTimer = millis();
  portalDebug("[portal] AP up at http://%s/", apIP.toString().c_str());
}

void initWiFi() {
  if (cachedSsid.length() > 0) {
    WiFi.mode(WIFI_STA);
    WiFi.begin(cachedSsid.c_str(), cachedPass.c_str());

    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < WIFI_TIMEOUT_MS) {
      delay(100);
    }
    wifiRetryTimer = millis();
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println(WiFi.localIP());
    portalDebug("[wifi] STA connected at %s", WiFi.localIP().toString().c_str());
    MDNS.begin("pompa");
  } else {
    startPortalAp();
  }
}

void handleAnalogReading() {
  if (currentMillis - lastSampleMillis < SAMPLE_INTERVAL_MS) return;
  lastSampleMillis = currentMillis;

  int sensorValue = analogRead(A0);
  float voltage = sensorValue * (5 / 4095.0);
  voltageSum = voltageSum - voltages[i] + voltage;
  voltages[i] = voltage;
  if (sampleCount < SIZE) sampleCount++;
  avgVoltage = voltageSum / sampleCount;
  level = (avgVoltage * 200.0) / 5;
  i++;
  if (i >= SIZE) {
    i = 0;
  }

  // Log once per second instead of hammering the 9600 baud console on every
  // sample (it blocks the loop and makes the portal feel unresponsive).
  if (currentMillis - lastSensorLog >= 1000) {
    lastSensorLog = currentMillis;
    portalDebug("[adc] raw=%d level=%.0f", sensorValue, level);
  }
}

// Keep the STA link alive after boot. A router reboot or a short power dip no
// longer leaves the device stranded: it retries the saved network and, if the
// network is gone for good, reopens the setup AP so it can be reconfigured.
void handleWifiMaintenance() {
  if (configPortalActive) return;

  if (!wifiConfigured()) {
    startPortalAp();
    return;
  }

  if (WiFi.status() == WL_CONNECTED) {
    staDownSince = 0;
    return;
  }

  if (staDownSince == 0) staDownSince = currentMillis;

  if (currentMillis - wifiRetryTimer >= WIFI_RETRY_INTERVAL_MS) {
    wifiRetryTimer = currentMillis;
    portalDebug("[wifi] link down, retrying '%s'", cachedSsid.c_str());
    WiFi.disconnect();
    WiFi.begin(cachedSsid.c_str(), cachedPass.c_str());
  }

  if (currentMillis - staDownSince >= STA_DOWN_PORTAL_MS) {
    portalDebug("[wifi] no network for %lu s, reopening setup AP",
                (unsigned long)(STA_DOWN_PORTAL_MS / 1000));
    staDownSince = 0;
    startPortalAp();
  }
}

void handleAutomaticMode() {
  unsigned long diff = currentMillis - previousMillis;
  if ((automatic == false) && (diff) >= goAutomaticAfter) {
    automatic = true;
  }
  if (!automatic) return;

  // Only touch the relay/LED on state changes: this runs on every loop pass and
  // used to refresh the LED thousands of times per second.
  if (level > 130 && !isOn) {
    digitalWrite(RELAY_PIN, HIGH);
    leds[0] = CRGB(0, 255, 0);
    FastLED.show();
    isOn = true;
  } else if (level <= 126 && isOn) {
    digitalWrite(RELAY_PIN, LOW);
    leds[0] = CRGB(0, 0, 0);
    FastLED.show();
    isOn = false;
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
  digitalWrite(RELAY_PIN, LOW);
  FastLED.show();

  prefsMutex = xSemaphoreCreateMutex();
  loadWifiCredsCache();
  goAutomaticAfter = getAutoMinutes() * 60000UL;

  Serial.begin(9600);
  portalDebug("[boot] portal build (tls-redirect + logging)");

  pinMode(BOOT_BUTTON_PIN, INPUT_PULLUP);
  // Only wipe the credentials when the button is deliberately held. A single
  // glitchy read during a brown-out used to erase the saved WiFi and leave the
  // device stuck in AP mode after every power loss.
  if (digitalRead(BOOT_BUTTON_PIN) == LOW) {
    unsigned long holdStart = millis();
    bool held = true;
    while (millis() - holdStart < BOOT_RESET_HOLD_MS) {
      if (digitalRead(BOOT_BUTTON_PIN) != LOW) {
        held = false;
        break;
      }
      delay(20);
    }
    if (held) {
      portalDebug("[boot] reset button held, clearing WiFi credentials");
      clearWifiCreds();
      delay(100);
    }
  }

  initWiFi();

  Serial.printf("[boot] mode=%d portal=%d sta=%s\n", WiFi.getMode(), configPortalActive,
                WiFi.localIP().toString().c_str());
  portalDebug("[boot] mode=%d portal=%d sta=%s", WiFi.getMode(), configPortalActive,
              WiFi.localIP().toString().c_str());
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
    } else if (action == "SETAUTO") {
      long minutes = request->arg("minutes").toInt();
      if (minutes == 15 || minutes == 30 || minutes == 45 || minutes == 60) {
        setAutoMinutes(minutes);
      }
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
    if (wifiConfigured() && !checkCtrlPassword(request->arg("password"))) {
      request->send(401, "application/json", "{\"status\":\"unauthorized\"}");
      return;
    }
    String savedSsid = cachedSsid;
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
    staDownSince = 0;
    wifiRetryTimer = millis();
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

  ElegantOTA.begin(&server, ELEGANT_OTA_USER, ELEGANT_OTA_PASS);
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

  // Pump control must keep running in setup-AP mode too, otherwise the device
  // stops watering until it is manually reconfigured.
  handleAnalogReading();
  handleAutomaticMode();
  handleWifiMaintenance();

  if (configPortalActive) {
    dnsServer.processNextRequest();
    if (!isOn) {
      handleBlueBlink();
    }
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
        portalDebug("[wifi] connected to '%s', closing portal in 30s", getSavedSsid().c_str());
      } else if (status == WL_CONNECT_FAILED || millis() - connectStartTime > WIFI_TIMEOUT_MS) {
        // Keep the AP up (and running in AP_STA) so the next attempt can
        // happen without kicking off the client that is configuring us.
        connectingToWifi = false;
        wifiRetryTimer = millis();
        portalDebug("[wifi] connect failed, retrying in %lu s",
                    (unsigned long)(WIFI_RETRY_INTERVAL_MS / 1000));
      }
    } else if (wifiConfigured() && WiFi.status() != WL_CONNECTED &&
               millis() - wifiRetryTimer >= WIFI_RETRY_INTERVAL_MS) {
      // Router came back late (typical after a power cut) - join it now.
      wifiRetryTimer = millis();
      portalDebug("[wifi] retrying saved network '%s'", cachedSsid.c_str());
      WiFi.mode(WIFI_AP_STA);
      WiFi.begin(cachedSsid.c_str(), cachedPass.c_str());
      connectingToWifi = true;
      connectStartTime = millis();
    }

    if (apShutdownTime > 0 && millis() > apShutdownTime) {
      apShutdownTime = 0;
      WiFi.softAPdisconnect(true);
      dnsServer.stop();
      tlsRedirect.stop();
      WiFi.mode(WIFI_STA);
      configPortalActive = false;
      staDownSince = 0;
      wifiRetryTimer = millis();
      portalDebug("[portal] STA online, setup AP closed");
    }

    return;
  }
}

#include "webpage.h"
#include <sstream>
#include <string>
#include <cmath>
#include <iomanip>

std::string Webpage::roundFloatToString(float val) {
    float rounded_val = std::round(val);
    std::ostringstream oss;
    oss.precision(0);
    oss << std::fixed << rounded_val;
    return oss.str();
}

std::string Webpage::buildJson(bool isOn, float level, unsigned long currentMillis, unsigned long previousMillis, bool automatic, unsigned long goAutomaticAfter) {
    unsigned long allSeconds = currentMillis / 1000;

    float perc = (level / 140.0f) * 100.0f;
    if (perc > 100.0f) perc = 100.0f;
    if (perc < 0.0f) perc = 0.0f;

    unsigned long autoInSec = 0;
    if (!automatic) {
        unsigned long diff = currentMillis - previousMillis;
        if (diff < goAutomaticAfter) {
            autoInSec = (goAutomaticAfter - diff) / 1000;
        }
    }

    std::string json = "{\"isOn\":";
    json += isOn ? "true" : "false";
    json += ",\"level\":";
    json += roundFloatToString(level);
    json += ",\"perc\":";
    json += roundFloatToString(perc);
    json += ",\"automatic\":";
    json += automatic ? "true" : "false";
    json += ",\"uptime\":";
    json += std::to_string(allSeconds);
    json += ",\"autoInSec\":";
    json += std::to_string(autoInSec);
    json += "}";
    return json;
}

Webpage::~Webpage() {}

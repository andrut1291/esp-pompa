#include "webpage.h"
#include <sstream>
#include <string>
#include <cstring>
#include <cmath>
#include <iomanip>

std::string Webpage::roundFloatToString(float val) {
    float rounded_val = std::round(val); // std::round returns double/float
    std::ostringstream oss;
    oss.precision(0);
    oss << std::fixed << rounded_val;
    return oss.str();
}

char* Webpage::buildWebpage(bool isOn, float level, unsigned long currentMillis, unsigned long previousMillis, bool automatic, unsigned long goAutomaticAfter)
{
    std::string s;
    s.append("<html>"
    "<head><script type='text/javascript'>"
    "let intervalId;"
    "function setAutoInterval() {"
    "   if(intervalId) {clearInterval(intervalId);}"
    "   const autoInSecElement = document.getElementById('auto-in-sec');"
    "   const autoInElement = document.getElementById('auto-in');"
    "   if (autoInSecElement && autoInSecElement.textContent !== '' && autoInElement) {"
    "       let val = Number(autoInSecElement.textContent);"
    "       if (!isNaN(val) && val > 0) {"
    "           intervalId = setInterval(() => {"
    "           const value = Number(autoInSecElement.textContent);"
    "               autoInSecElement.textContent = Number(autoInSecElement.textContent) - 1;"
    "               if (value <= 0) {"
    "                   autoInElement.textContent = '';"
    "                   clearInterval(intervalId);"
    "               } else {"
    "                   const minutes = Math.floor(value / 60);"
    "                   const seconds = value % 60;"
    "                   autoInElement.innerHTML = `Auto in: <b>${minutes}</b>m <b>${seconds}</b>s`;"
    "               }"
    "           }, 1000);"
    "           const minutes = Math.floor(val / 60);"
    "           const seconds = val % 60;"
    "           autoInElement.innerHTML = `Auto in: <b>${minutes}</b>m <b>${seconds}</b>s`;"
    "       } else {"
    "            autoInElement.textContent = '';"
    "       }"
    "   } else if (autoInElement) {"
    "        autoInElement.textContent = '';"
    "   }"
    "}"
    "(function () {"

    "    window.onload = () => {"
                "if (document.location.hash.substring(1) === '') {"
                "    const buttons = document.getElementById('buttons');"
                "    if (buttons) buttons.style.display = 'none';"
                "}"
                "const uptimeElement = document.getElementById('uptime');"
                "let uptimeInSec = Number(document.getElementById('uptime-in-sec').textContent);"

                "setInterval(() => {"
                "   uptimeInSec = uptimeInSec + 1;"
                "   const minutes = Math.floor(uptimeInSec / 60);"
                "   const hours = Math.floor(minutes / 60);"
                "   const seconds = uptimeInSec % 60;"
                "   const days = Math.floor(hours / 24);"
                "   uptimeElement.innerHTML = `Uptime: <b>${days}</b>d<b>${hours % 24}</b>h<b>${minutes % 60}</b>m<b>${uptimeInSec % 60}</b>s`;"
                "}, 1000);"
                "setAutoInterval();"
    "    };"
    "})();"
    "async function getAction(a) {"
    "    await fetch(document.location.hash.substring(1) + a).then((response) => {"
    "        if(response.status === 200) {"
    "            if(a === 'WLACZ') {"
    "                document.getElementById('state').textContent = 'Obecny stan - zalaczony';"
    "                document.getElementById('mode').textContent = 'Tryb pracy manualny';"
    "                document.getElementById('auto-in-sec').textContent = '900';"
    "                setAutoInterval();"
    "            } else if(a === 'WYLACZ') {"
    "                document.getElementById('state').textContent = 'Obecny stan - wylaczony';"
    "                document.getElementById('mode').textContent = 'Tryb pracy manualny';"
    "                document.getElementById('auto-in-sec').textContent = '900';"
    "                setAutoInterval();"
    "            } else {"
    "                document.getElementById('mode').textContent = 'Tryb pracy automatyczny';"
    "                document.getElementById('state').textContent = 'Obecny stan - wylaczony';"
    "                document.getElementById('auto-in-sec').textContent = '0';"
    "            }"
    "        }"
    "    });}"
    "</script></head>"
    "<link rel=\"stylesheet\" href=\"https://cdn.jsdelivr.net/npm/purecss@3.0.0/build/pure-min.css\" integrity=\"sha384-X38yfunGUhNzHpBaEBsWLO+A0HDYOQi8ufWDkZ0k9e0eXz/tH3II7uKZ9msv++Ls\" crossorigin=\"anonymous\">"
    "<style>body {text-align:center}.button-success, .button-error, .button-secondary {color: white; border-radius: 4px;text-shadow: 0 1px 1px rgba(0, 0, 0, 0.2);}</style>"
    "<style>"
    ".tank {"
    "    width: 150px;"
    "    height: 200px;"
    "    border: 2px solid black;"
    "    border-top: none;"
    "    background: white;"
    "    position: relative;"
    "    display: inline-block;"
    "    margin: 20px 10px;"
    "    overflow: hidden;"
    "}"
    ""
    ".tank .water {"
    "    position: absolute;"
    "    background: #64b5f6;"
    "    width: 100%;"
    "    bottom: 0;"
    "    left: 0;"
    "    border-top-left-radius: 0;"
    "    border-top-right-radius: 0;"
    "}"
    ""
    ".tank .water .perc {"
    "    position: relative;"
    "    z-index: 20;"
    "    text-align: center;"
    "    font-size: 16px;"
    "    font-weight: bold;"
    "    color: black;"
    "    padding-top: 20px;"
    "    text-shadow: 1px 1px 2px white;"
""
    "}"
    "@keyframes wave {"
    "    0% {"
    "        transform: translateX(0) rotate(0deg);"
    "    }"
    "    50% {"
    "         transform: translateX(-25%) rotate(6deg);"
    "    }"
    "    100% {"
    "        transform: translateX(0) rotate(0deg);"
    "    }"
    "}"
    ".tank .water::before,"
    ".tank .water::after {"
    "    content: '';"
    "    position: absolute;"
    "    width: 200%;"
    "    height: 15px;"
    "    background-color: rgba(255, 255, 255, 1);"
    "    left: -50%;"
    "    top: -10px;"
    "    border-radius: 45%;"
    "    animation: wave 5s linear infinite;"
    "    z-index: 10;"
    "    border-top-right-radius: 1% !important;"
    "}"
    ".tank .water::after {"
    "    height: 25px;"
    "    top: -15px;"
    "    border-radius: 40%;"
    "    background-color: rgba(255, 255, 255, 1);"
    "    animation-duration: 7s;"
    "    animation-direction: reverse;"
    "     z-index: 9;"
    "}"
    "</style>"
    "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"
    "<h2 id='state'> Obecny stan - ");
    if(isOn) {
        s.append("zalaczony</h2>");
    } else {
        s.append("wylaczony</h2>");
    }
    s.append("<h2 id='mode'> Tryb pracy ");
    if(automatic) {
        s.append("automatyczny</h2>");
    } else {
        s.append("manualny</h2>");
    }
    s.append("<h1> Poziom wody ");
    s.append(this->roundFloatToString(level));
    s.append(" cm</h1>");
    s.append("<div class=\"tank\"><div class=\"water\" style=\"height: ");
    s.append(this->roundFloatToString(level / 140 * 100));
    s.append("%;\"><div class=\"perc\">");
    s.append(this->roundFloatToString(level / 140 * 100));
    s.append("%</div></div></div>"
        "<div id='buttons'>"
        "<div style=\"padding: 1em 0; width: 100%\"><button style=\"background: rgb(28, 184, 65);\" class=\"pure-button button-success\" onclick=\"getAction('WLACZ')\">ON</button></div>"
        "<div style=\"padding: 1em 0; width: 100%\"><button style=\"background: rgb(202, 60, 60);\" class=\"pure-button button-success\" onclick=\"getAction('WYLACZ')\">OFF</button></div>"
        "<div style=\"padding: 1em 0; width: 100%\"><button class=\"pure-button pure-button-primary\" onclick=\"getAction('AUTOMATYCZNY')\">AUTO</button></div>"
        "</div>"
        "<p id='uptime'>Uptime: "
    );
    unsigned long runMillis = currentMillis;
    unsigned long allSeconds = currentMillis/1000;
    int runHours= allSeconds/3600;
    int runDays= runHours/24;

    int secsRemaining=allSeconds%3600;
    int runMinutes=secsRemaining/60;
    int runSeconds=secsRemaining%60;

    char buf[50];
    sprintf(buf,"<b>%d</b>d<b>%d</b>h<b>%d</b>m<b>%d</b>s", runDays, runHours % 24, runMinutes, runSeconds);
    s.append(buf);
    s.append("</p>");

    s.append("<span style='display: none' id='uptime-in-sec'>");
    s.append(std::to_string(currentMillis/1000));
    s.append("</span>");
    if(!automatic) {
        s.append("<span style='display: none' id='auto-in-sec'>");
        s.append(std::to_string((goAutomaticAfter - (currentMillis - previousMillis)) / 1000));
        s.append("</span>");
    } else {
        s.append("<span style='display: none' id='auto-in-sec'>0</span>"); 
    }
    s.append("<p id='auto-in'></p>");
    s.append("</html>");

    char * output = new char [s.length() + 1];
    strcpy (output, s.c_str());
    return output;
}

Webpage::~Webpage() {}
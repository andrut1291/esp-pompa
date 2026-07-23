#ifndef EMBEDDED_HTML_H
#define EMBEDDED_HTML_H

const char INDEX_HTML[] = R"rawliteral(
<html>
<head>
<script type='text/javascript'>
let intervalId;
let uptimeCount = 0;

function setAutoInterval(val) {
   if (intervalId) { clearInterval(intervalId); }
   const autoInElement = document.getElementById('auto-in');
   const autoInSecElement = document.getElementById('auto-in-sec');
   if (val > 0) {
       autoInSecElement.textContent = val;
       intervalId = setInterval(() => {
           const v = Number(autoInSecElement.textContent);
           if (v <= 0) {
               autoInElement.textContent = '';
               clearInterval(intervalId);
               fetchStatus();
           } else {
               autoInSecElement.textContent = v - 1;
               const m = Math.floor((v - 1) / 60);
               const s = (v - 1) % 60;
               autoInElement.innerHTML = 'Auto in: <b>' + m + '</b>m <b>' + s + '</b>s';
           }
       }, 1000);
       const m = Math.floor(val / 60);
       const s = val % 60;
       autoInElement.innerHTML = 'Auto in: <b>' + m + '</b>m <b>' + s + '</b>s';
   } else {
       autoInElement.textContent = '';
   }
}

function updateUptime() {
   uptimeCount++;
   const m = Math.floor(uptimeCount / 60);
   const h = Math.floor(m / 60);
   const s = uptimeCount % 60;
   const d = Math.floor(h / 24);
   document.getElementById('uptime').innerHTML = 'Uptime: <b>' + d + '</b>d<b>' + (h % 24) + '</b>h<b>' + (m % 60) + '</b>m<b>' + (uptimeCount % 60) + '</b>s';
}

function updateUI(data) {
   document.getElementById('state').textContent = data.isOn ? 'Obecny stan - zalaczony' : 'Obecny stan - wylaczony';
   document.getElementById('mode').textContent = data.automatic ? 'Tryb pracy automatyczny' : 'Tryb pracy manualny';
   document.getElementById('level').innerHTML = 'Poziom wody ' + data.level + ' cm';
   document.getElementById('water').style.height = data.perc + '%';
   document.getElementById('perc').textContent = data.perc + '%';
   document.getElementById('auto-in-sec').textContent = data.autoInSec;
   setAutoInterval(data.autoInSec);
   uptimeCount = data.uptime;
}

async function fetchStatus() {
   try {
       const r = await fetch('/api/status');
       if (r.ok) updateUI(await r.json());
   } catch (e) {}
}

async function getAction(a) {
   try {
       const r = await fetch(document.location.hash.substring(1) + a);
       if (r.ok) fetchStatus();
   } catch (e) {}
}

(function () {
   window.onload = () => {
       if (document.location.hash.substring(1) === '') {
           const b = document.getElementById('buttons');
           if (b) b.style.display = 'none';
       }
       setInterval(updateUptime, 1000);
       fetchStatus();
       setInterval(fetchStatus, 5000);
   };
})();
</script>
</head>
<link rel="stylesheet" href="https://cdn.jsdelivr.net/npm/purecss@3.0.0/build/pure-min.css" integrity="sha384-X38yfunGUhNzHpBaEBsWLO+A0HDYOQi8ufWDkZ0k9e0eXz/tH3II7uKZ9msv++Ls" crossorigin="anonymous">
<style>
body { text-align: center; }
.button-success, .button-error, .button-secondary { color: white; border-radius: 4px; text-shadow: 0 1px 1px rgba(0, 0, 0, 0.2); }
.tank {
   width: 150px; height: 200px; border: 2px solid black; border-top: none;
   background: white; position: relative; display: inline-block;
   margin: 20px 10px; overflow: hidden;
}
.tank .water {
   position: absolute; background: #64b5f6; width: 100%;
   bottom: 0; left: 0;
}
.tank .water .perc {
   position: relative; z-index: 20; text-align: center; font-size: 16px;
   font-weight: bold; color: black; padding-top: 20px; text-shadow: 1px 1px 2px white;
}
@keyframes wave {
   0% { transform: translateX(0) rotate(0deg); }
   50% { transform: translateX(-25%) rotate(6deg); }
   100% { transform: translateX(0) rotate(0deg); }
}
.tank .water::before,
.tank .water::after {
   content: ''; position: absolute; width: 200%; height: 15px;
   background-color: rgba(255, 255, 255, 1); left: -50%; top: -10px;
   border-radius: 45%; animation: wave 5s linear infinite;
   z-index: 10;
}
.tank .water::after {
   height: 25px; top: -15px; border-radius: 40%;
   background-color: rgba(255, 255, 255, 1);
   animation-duration: 7s; animation-direction: reverse; z-index: 9;
}
</style>
<meta name="viewport" content="width=device-width, initial-scale=1">
<h2 id='state'>Obecny stan - wylaczony</h2>
<h2 id='mode'>Tryb pracy automatyczny</h2>
<h1 id='level'>Poziom wody 0 cm</h1>
<div class="tank"><div id="water" class="water" style="height: 0%;"><div id="perc" class="perc">0%</div></div></div>
<div id='buttons'>
<div style="padding: 1em 0; width: 100%"><button style="background: rgb(28, 184, 65);" class="pure-button button-success" onclick="getAction('WLACZ')">ON</button></div>
<div style="padding: 1em 0; width: 100%"><button style="background: rgb(202, 60, 60);" class="pure-button button-success" onclick="getAction('WYLACZ')">OFF</button></div>
<div style="padding: 1em 0; width: 100%"><button class="pure-button pure-button-primary" onclick="getAction('AUTOMATYCZNY')">AUTO</button></div>
</div>
<p id='uptime'>Uptime: <b>0</b>d<b>0</b>h<b>0</b>m<b>0</b>s</p>
<span style="display: none" id="uptime-in-sec">0</span>
<span style="display: none" id="auto-in-sec">0</span>
<p id='auto-in'></p>
</html>
)rawliteral";

#endif

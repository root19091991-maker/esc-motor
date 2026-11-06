// ESP32-C3 Super Mini: управление ESC (ШИМ 50 Гц, 1000..2000 мкс) через Wi-Fi с телефона.
// Телефон -> Wi-Fi "ESC-Motor" (пароль 12345678) -> браузер -> http://192.168.4.1
// ВНИМАНИЕ: снимите винт/нагрузку при первой проверке!
#include <WiFi.h>
#include <WebServer.h>

#ifndef ESP_ARDUINO_VERSION_MAJOR
#define ESP_ARDUINO_VERSION_MAJOR 2
#endif

// ---------- Настройки ----------
const char* AP_SSID = "ESC-Motor";
const char* AP_PASS = "12345678";          // минимум 8 символов
const int   ESC_PIN = 4;                   // сигнальный провод ESC -> GPIO4 (GND ESC -> GND платы)
const int   MIN_US  = 1000;                // 0% газа
const int   MAX_US  = 2000;                // 100% газа
const int   MAX_PERCENT = 50;              // ПРЕДЕЛ газа (защита). Поднимайте осторожно.
const int   START_PERCENT = 5;             // стартовое значение: 5% от диапазона 1000 мкс = 1050 мкс
const unsigned long FAILSAFE_MS = 1000;    // нет связи с телефоном дольше -> мотор стоп
const unsigned long ARM_MS = 3000;         // время подачи 1000 мкс при старте (арминг ESC)
// --------------------------------

const int PWM_FREQ = 50;
const int PWM_RES  = 14;                   // 16384 тика на период 20000 мкс

WebServer server(80);
bool motorOn = false;
int  percent = START_PERCENT;
unsigned long lastBeat = 0;

#if ESP_ARDUINO_VERSION_MAJOR >= 3
void pwmBegin()           { ledcAttach(ESC_PIN, PWM_FREQ, PWM_RES); }
void pwmDuty(uint32_t d)  { ledcWrite(ESC_PIN, d); }
#else
void pwmBegin()           { ledcSetup(0, PWM_FREQ, PWM_RES); ledcAttachPin(ESC_PIN, 0); }
void pwmDuty(uint32_t d)  { ledcWrite(0, d); }
#endif

int pulseUs() { return motorOn ? MIN_US + percent * (MAX_US - MIN_US) / 100 : MIN_US; }

void applyOutput() {
  uint32_t duty = (uint32_t)pulseUs() * 16384UL / 20000UL;
  pwmDuty(duty);
}

const char PAGE[] PROGMEM = R"rawliteral(<!doctype html><html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>ESC</title><style>
body{font-family:sans-serif;background:#111;color:#eee;text-align:center;margin:0;padding:16px}
button{font-size:22px;padding:16px;margin:6px;border:0;border-radius:12px;background:#333;color:#fff;min-width:44%}
#pw{width:92%;height:90px;font-size:30px;margin:10px 0;border-radius:16px}
.on{background:#c62828!important}.off{background:#2e7d32!important}.sel{outline:3px solid #fdd835}
input[type=range]{width:92%;height:40px}
</style></head><body>
<h2>Мотор / ESC</h2>
<div id="st">...</div>
<button id="pw" class="off" onclick="tog()">ВКЛЮЧИТЬ</button>
<h3>Режим</h3>
<div id="modes"></div>
<h3>Газ: <span id="pv">5</span>% (<span id="us">1050</span> мкс)</h3>
<input type="range" id="sl" min="0" max="%MAX%" value="5" oninput="setP(this.value)">
<script>
var on=false,p=5,max=%MAX%;
var modes=[["5%",5],["10%",10],["20%",20],["30%",30],["40%",40],["50%",50]];
var md=document.getElementById('modes');
modes.forEach(function(m){if(m[1]>max)return;var b=document.createElement('button');b.textContent=m[0];b.id='m'+m[1];b.onclick=function(){setP(m[1])};md.appendChild(b)});
function setP(v){p=Math.min(max,parseInt(v));document.getElementById('sl').value=p;ui();send()}
function tog(){on=!on;ui();send()}
function ui(){document.getElementById('pv').textContent=p;document.getElementById('us').textContent=on?1000+p*10:1000;
var b=document.getElementById('pw');b.textContent=on?'ВЫКЛЮЧИТЬ':'ВКЛЮЧИТЬ';b.className=on?'on':'off';
modes.forEach(function(m){var e=document.getElementById('m'+m[1]);if(e)e.className=(m[1]==p)?'sel':''})}
function send(){fetch('/api?on='+(on?1:0)+'&p='+p).then(function(r){return r.json()}).then(function(s){
document.getElementById('st').textContent='связь OK, импульс '+s.us+' мкс'}).catch(function(){
document.getElementById('st').textContent='НЕТ СВЯЗИ'})}
ui();send();setInterval(send,300);
</script></body></html>)rawliteral";

void handleRoot() {
  String html = FPSTR(PAGE);
  html.replace("%MAX%", String(MAX_PERCENT));
  server.send(200, "text/html; charset=utf-8", html);
}

void handleApi() {
  if (server.hasArg("p")) percent = constrain(server.arg("p").toInt(), 0, MAX_PERCENT);
  if (server.hasArg("on")) motorOn = (server.arg("on") == "1");
  lastBeat = millis();
  applyOutput();
  server.send(200, "application/json",
              String("{\"on\":") + (motorOn ? "true" : "false") + ",\"p\":" + percent + ",\"us\":" + pulseUs() + "}");
}

void setup() {
  Serial.begin(115200);
  pwmBegin();
  motorOn = false;
  applyOutput();                       // 1000 мкс = ноль газа
  Serial.println("ESC arming...");
  delay(ARM_MS);                       // ESC слышит минимум и армится
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASS);
  Serial.print("AP IP: "); Serial.println(WiFi.softAPIP());
  server.on("/", handleRoot);
  server.on("/api", handleApi);
  server.begin();
  lastBeat = millis();
}

void loop() {
  server.handleClient();
  if (motorOn && millis() - lastBeat > FAILSAFE_MS) {   // потеряли телефон -> стоп
    motorOn = false;
    applyOutput();
    Serial.println("FAILSAFE: stop");
  }
}

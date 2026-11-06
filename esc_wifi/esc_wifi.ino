// ESP32-C3 Super Mini: керування ESC (ШІМ 50 Гц, 1000..2000 мкс) через Wi-Fi з телефона.
// Телефон -> Wi-Fi "ESC-Motor" (пароль 12345678) -> браузер -> http://192.168.4.1
// Назву і пароль Wi-Fi можна змінити на сторінці. Скидання до заводських: утримуйте кнопку BOOT 5 секунд.
// УВАГА: при першій перевірці зніміть гвинт!
#include <WiFi.h>
#include <WebServer.h>
#include <Preferences.h>

#ifndef ESP_ARDUINO_VERSION_MAJOR
#define ESP_ARDUINO_VERSION_MAJOR 2
#endif

// ---------- Налаштування ----------
const char* DEF_SSID = "ESC-Motor";
const char* DEF_PASS = "12345678";         // мінімум 8 символів
const int   ESC_PIN  = 4;                  // сигнал ESC -> GPIO4 (GND ESC -> GND плати)
const int   RESET_BTN_PIN = 9;                  // кнопка BOOT: 5 с утримання = скидання Wi-Fi
const int   MIN_US   = 1000;               // 0% газу
const int   MAX_US   = 2000;               // 100% газу
const int   MAX_PERCENT = 10;              // МАКСИМУМ газу 10%
const int   START_PERCENT = 5;             // старт: 5% = 1050 мкс
const unsigned long FAILSAFE_MS = 1000;    // немає зв'язку з телефоном -> мотор стоп
const unsigned long ARM_MS = 3000;         // час 1000 мкс при старті (армінг ESC)
// --------------------------------

const int PWM_FREQ = 50;
const int PWM_RES  = 14;                   // 16384 тіків на період 20000 мкс

WebServer server(80);
Preferences prefs;
String apSsid, apPass;
bool motorOn = false;
int  percent = START_PERCENT;
unsigned long lastBeat = 0;
unsigned long restartAt = 0;
unsigned long bootHeldSince = 0;

#if ESP_ARDUINO_VERSION_MAJOR >= 3
void pwmBegin()          { ledcAttach(ESC_PIN, PWM_FREQ, PWM_RES); }
void pwmDuty(uint32_t d) { ledcWrite(ESC_PIN, d); }
#else
void pwmBegin()          { ledcSetup(0, PWM_FREQ, PWM_RES); ledcAttachPin(ESC_PIN, 0); }
void pwmDuty(uint32_t d) { ledcWrite(0, d); }
#endif

int pulseUs() { return motorOn ? MIN_US + percent * (MAX_US - MIN_US) / 100 : MIN_US; }

void applyOutput() {
  uint32_t duty = (uint32_t)pulseUs() * 16384UL / 20000UL;
  pwmDuty(duty);
}

String htmlEsc(const String& s) {
  String o;
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    if (c == '&') o += "&amp;";
    else if (c == '<') o += "&lt;";
    else if (c == '>') o += "&gt;";
    else if (c == '"') o += "&quot;";
    else o += c;
  }
  return o;
}

const char PAGE[] PROGMEM = R"rawliteral(<!doctype html><html lang="uk"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>ESC</title><style>
body{font-family:sans-serif;background:#111;color:#eee;text-align:center;margin:0;padding:16px}
button{font-size:22px;padding:16px;margin:6px;border:0;border-radius:12px;background:#333;color:#fff;min-width:44%}
#pw{width:92%;height:90px;font-size:30px;margin:10px 0;border-radius:16px}
.on{background:#c62828!important}.off{background:#2e7d32!important}.sel{outline:3px solid #fdd835}
input[type=range]{width:92%;height:40px}
input[type=text],input[type=password]{width:90%;font-size:20px;padding:10px;margin:6px 0;border-radius:8px;border:1px solid #555;background:#222;color:#fff}
.box{border-top:1px solid #444;margin-top:24px;padding-top:8px}
.sv{background:#1565c0;min-width:90%}
small{color:#aaa}
</style></head><body>
<h2>Мотор / ESC</h2>
<div id="st">...</div>
<button id="pw" class="off" onclick="tog()">УВІМКНУТИ</button>
<h3>Режим</h3>
<div id="modes"></div>
<h3>Газ: <span id="pv">5</span>% (<span id="us">1050</span> мкс)</h3>
<input type="range" id="sl" min="0" max="%MAX%" value="5" oninput="setP(this.value)">
<small>Максимум %MAX%%</small>
<div class="box">
<h3>Wi-Fi</h3>
<input type="text" id="ssid" maxlength="32" placeholder="Назва мережі" value="%SSID%">
<input type="password" id="pass" maxlength="63" placeholder="Новий пароль (мін. 8 символів)">
<button class="sv" onclick="saveWifi()">Зберегти й перезапустити</button>
<br><small>Після збереження плата перезапуститься.<br>Підключіть телефон до нової мережі й відкрийте 192.168.4.1</small>
</div>
<script>
var on=false,p=5,max=%MAX%;
var modes=[["5%",5],["7%",7],["10%",10]];
var md=document.getElementById('modes');
modes.forEach(function(m){if(m[1]>max)return;var b=document.createElement('button');b.textContent=m[0];b.id='m'+m[1];b.onclick=function(){setP(m[1])};md.appendChild(b)});
function setP(v){p=Math.min(max,parseInt(v));document.getElementById('sl').value=p;ui();send()}
function tog(){on=!on;ui();send()}
function ui(){document.getElementById('pv').textContent=p;document.getElementById('us').textContent=on?1000+p*10:1000;
var b=document.getElementById('pw');b.textContent=on?'ВИМКНУТИ':'УВІМКНУТИ';b.className=on?'on':'off';
modes.forEach(function(m){var e=document.getElementById('m'+m[1]);if(e)e.className=(m[1]==p)?'sel':''})}
function send(){fetch('/api?on='+(on?1:0)+'&p='+p).then(function(r){return r.json()}).then(function(s){
document.getElementById('st').textContent='зв\'язок OK, імпульс '+s.us+' мкс'}).catch(function(){
document.getElementById('st').textContent='НЕМАЄ ЗВ\'ЯЗКУ'})}
function saveWifi(){
var s=document.getElementById('ssid').value.trim(),w=document.getElementById('pass').value;
if(s.length<1||s.length>32){alert('Назва: від 1 до 32 символів');return}
if(w.length<8||w.length>63){alert('Пароль: від 8 до 63 символів');return}
if(!confirm('Змінити Wi-Fi на "'+s+'"? Плата перезапуститься.'))return;
on=false;ui();
fetch('/wifi?ssid='+encodeURIComponent(s)+'&pass='+encodeURIComponent(w)).then(function(r){return r.text()}).then(function(t){alert(t)}).catch(function(){alert('Збережено. Підключіться до нової мережі.')})}
ui();send();setInterval(send,300);
</script></body></html>)rawliteral";

void handleRoot() {
  String html = FPSTR(PAGE);
  html.replace("%MAX%", String(MAX_PERCENT));
  html.replace("%SSID%", htmlEsc(apSsid));
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

void handleWifi() {
  String s = server.arg("ssid");
  String w = server.arg("pass");
  if (s.length() < 1 || s.length() > 32 || w.length() < 8 || w.length() > 63) {
    server.send(400, "text/plain; charset=utf-8", "Помилка: назва 1-32 символи, пароль 8-63 символи");
    return;
  }
  motorOn = false;
  applyOutput();
  prefs.begin("wifi", false);
  prefs.putString("ssid", s);
  prefs.putString("pass", w);
  prefs.end();
  server.send(200, "text/plain; charset=utf-8", "Збережено. Плата перезапускається. Підключіться до мережі: " + s);
  restartAt = millis() + 1500;
}

void setup() {
  Serial.begin(115200);
  pinMode(RESET_BTN_PIN, INPUT_PULLUP);
  pwmBegin();
  motorOn = false;
  applyOutput();                       // 1000 мкс = нуль газу
  prefs.begin("wifi", true);
  apSsid = prefs.getString("ssid", DEF_SSID);
  apPass = prefs.getString("pass", DEF_PASS);
  prefs.end();
  Serial.println("ESC arming...");
  delay(ARM_MS);                       // ESC чує мінімум і армиться
  WiFi.mode(WIFI_AP);
  WiFi.softAP(apSsid.c_str(), apPass.c_str());
  Serial.print("AP: "); Serial.print(apSsid);
  Serial.print("  IP: "); Serial.println(WiFi.softAPIP());
  server.on("/", handleRoot);
  server.on("/api", handleApi);
  server.on("/wifi", handleWifi);
  server.begin();
  lastBeat = millis();
}

void loop() {
  server.handleClient();

  if (motorOn && millis() - lastBeat > FAILSAFE_MS) {   // втратили телефон -> стоп
    motorOn = false;
    applyOutput();
    Serial.println("FAILSAFE: stop");
  }

  if (restartAt && millis() > restartAt) ESP.restart();

  // Скидання Wi-Fi до заводських: утримувати BOOT 5 с
  if (digitalRead(RESET_BTN_PIN) == LOW) {
    if (!bootHeldSince) bootHeldSince = millis();
    if (millis() - bootHeldSince > 5000) {
      motorOn = false;
      applyOutput();
      prefs.begin("wifi", false);
      prefs.clear();
      prefs.end();
      Serial.println("Wi-Fi reset. Release BOOT.");
      while (digitalRead(RESET_BTN_PIN) == LOW) delay(50);   // відпустіть кнопку, потім перезапуск
      ESP.restart();
    }
  } else {
    bootHeldSince = 0;
  }
}

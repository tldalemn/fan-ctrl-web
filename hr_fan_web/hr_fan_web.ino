// HR Fan Web: heart rate controlled fan with a phone/browser control panel
// Based on fan-ctrl by Andrew Grabbs (https://github.com/agrabbs/fan-ctrl), MIT License
// ESP32 + Bluetooth heart rate strap + 3 relays (low, medium, high)
// Open http://fan.local (or the IP shown in Serial Monitor) in any browser.
//
// Modes:
//   Off     - fan off
//   Heart   - fan speed follows your heart rate zones
//   Manual  - you pick Low, Medium, or High
// Settings and zones are saved, so they survive a power cycle.

#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <BLEDevice.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>   // Library Manager: "LiquidCrystal I2C" by Frank de Brabander
#include "secrets.h"             // your WiFi name and password (copy secrets.example.h)

// ===== Your settings =====
const uint8_t RELAY_PINS[3] = {25, 26, 27};   // low, medium, high
const bool RELAY_ACTIVE_LOW = true;           // most relay boards turn on with LOW
const bool INTERLOCK_WIRING = false;          // true if relays are daisy chained (see README)
const char* HRM_ADDRESS = "";                 // optional: lock to one strap, e.g. "aa:bb:cc:dd:ee:ff"
const int LCD_SDA = 21;                       // I2C screen data pin
const int LCD_SCL = 22;                       // I2C screen clock pin
const uint32_t STEP_DOWN_DELAY_MS = 15000;    // wait this long before slowing the fan
const uint32_t HR_TIMEOUT_MS = 10000;         // no HR data this long = treat as no HR

// ===== BLE IDs =====
static BLEUUID HR_SERVICE((uint16_t)0x180D);
static BLEUUID HR_MEASURE((uint16_t)0x2A37);

WebServer server(80);
Preferences prefs;

// ===== State =====
enum { MODE_OFF = 0, MODE_HR = 1, MODE_MANUAL = 2 };
int mode = MODE_HR;
int manualSpeed = 1;
int zLow = 100, zMed = 130, zHigh = 150;      // bpm where low, medium, high start

int fanSpeed = 0;                             // 0 off, 1 low, 2 med, 3 high
int pendingSpeed = -1;
uint32_t pendingSince = 0;
const char* SPEED_NAMES[4] = {"Off", "Low", "Medium", "High"};

volatile int heartRate = 0;
volatile uint32_t lastHrTime = 0;
volatile bool hrConnected = false;
volatile bool doConnect = false;
bool scanning = false;
uint32_t scanStart = 0;
BLEAdvertisedDevice* hrDevice = nullptr;
BLEClient* client = nullptr;
String hrName = "";

LiquidCrystal_I2C* lcd = nullptr;
String lcdLine1 = "", lcdLine2 = "";

// ===== Control page =====
const char PAGE[] PROGMEM = R"rawliteral(
<!doctype html><html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Fan Control</title>
<style>
body{margin:0;background:#111;color:#eee;font-family:system-ui,sans-serif;max-width:520px;margin:auto}
#st{font-size:13px;color:#999;padding:10px 12px}
.c{background:#1d1d1d;border-radius:12px;padding:14px;margin:0 10px 10px}
.l{font-size:12px;color:#999;text-transform:uppercase;letter-spacing:.05em;margin-bottom:8px}
#hrv{font-size:96px;font-weight:700;text-align:center;line-height:1}
#hrv span{font-size:20px;color:#999;font-weight:400}
.row{display:flex;gap:8px}
.row>*{flex:1}
.seg{text-align:center;padding:10px 0;border-radius:8px;background:#2a2a2a;color:#777;font-weight:600}
.seg.on{background:#2f8cff;color:#fff}
button{font-size:16px;padding:14px 0;border:0;border-radius:8px;background:#2a2a2a;color:#eee;font-weight:600}
button.on{background:#2f8cff;color:#fff}
#pend{text-align:center;font-size:13px;color:#ffcc3f;min-height:18px;margin-top:8px}
label{font-size:13px;color:#999;display:block}
input{width:100%;box-sizing:border-box;font-size:18px;padding:8px;border-radius:6px;border:1px solid #333;background:#111;color:#eee;margin-top:4px}
#zmsg{font-size:13px;color:#999;margin-top:8px;min-height:16px}
</style></head><body>
<div id="st">Loading...</div>
<div class="c"><div id="hrv">--<span> bpm</span></div></div>
<div class="c"><div class="l">Fan now</div>
 <div class="row"><div class="seg" id="f0">Off</div><div class="seg" id="f1">Low</div><div class="seg" id="f2">Med</div><div class="seg" id="f3">High</div></div>
 <div id="pend"></div>
</div>
<div class="c"><div class="l">Mode</div>
 <div class="row"><button id="m0" onclick="set('mode=0')">Off</button><button id="m1" onclick="set('mode=1')">Heart rate</button><button id="m2" onclick="set('mode=2')">Manual</button></div>
</div>
<div class="c"><div class="l">Manual speed</div>
 <div class="row"><button id="s1" onclick="set('mode=2&speed=1')">Low</button><button id="s2" onclick="set('mode=2&speed=2')">Med</button><button id="s3" onclick="set('mode=2&speed=3')">High</button></div>
</div>
<div class="c"><div class="l">Heart rate zones (bpm where each speed starts)</div>
 <div class="row">
  <div><label>Low</label><input id="zl" type="number"></div>
  <div><label>Medium</label><input id="zm" type="number"></div>
  <div><label>High</label><input id="zh" type="number"></div>
 </div>
 <div style="margin-top:10px"><button style="width:100%" onclick="saveZones()">Save zones</button></div>
 <div id="zmsg"></div>
</div>
<script>
const $=id=>document.getElementById(id);
let loaded=false;
async function refresh(){
 try{
  const S=await (await fetch('/status')).json();
  $('st').textContent=S.conn?('Connected to '+S.name):'Searching for heart rate strap...';
  $('hrv').innerHTML=(S.conn&&S.hr?S.hr:'--')+'<span> bpm</span>';
  for(let i=0;i<4;i++)$('f'+i).classList.toggle('on',S.fan==i);
  for(let m=0;m<3;m++)$('m'+m).classList.toggle('on',S.mode==m);
  for(let s=1;s<4;s++)$('s'+s).classList.toggle('on',S.mode==2&&S.manual==s);
  $('pend').textContent=S.pend>=0?('Slowing down in '+S.pend+'s'):'';
  if(!loaded){$('zl').value=S.z[0];$('zm').value=S.z[1];$('zh').value=S.z[2];loaded=true}
 }catch(e){$('st').textContent='Lost connection to fan controller'}
}
async function set(q){await fetch('/set?'+q);refresh()}
async function saveZones(){
 const r=await fetch(`/zones?low=${$('zl').value}&med=${$('zm').value}&high=${$('zh').value}`);
 $('zmsg').textContent=await r.text();
}
function loop(){refresh().then(()=>setTimeout(loop,1000))}
loop();
</script></body></html>
)rawliteral";

// ===== Relays =====
void relayWrite(int i, bool on) {
  digitalWrite(RELAY_PINS[i], (on != RELAY_ACTIVE_LOW) ? HIGH : LOW);
}

void applySpeed(int s) {
  if (s == fanSpeed) return;
  for (int i = 0; i < 3; i++) relayWrite(i, false);   // break before make
  if (s > 0) {
    delay(150);
    if (INTERLOCK_WIRING) {
      relayWrite(1, s >= 2);   // relay 2: low vs medium/high
      relayWrite(2, s == 3);   // relay 3: medium vs high
      delay(50);
      relayWrite(0, true);     // relay 1: power, switched on last
    } else {
      relayWrite(s - 1, true); // one relay per speed
    }
  }
  fanSpeed = s;
  Serial.printf("Fan: %s\n", SPEED_NAMES[s]);
}

int hrTarget() {
  if (!hrConnected || heartRate == 0 || millis() - lastHrTime > HR_TIMEOUT_MS) return 0;
  if (heartRate >= zHigh) return 3;
  if (heartRate >= zMed) return 2;
  if (heartRate >= zLow) return 1;
  return 0;
}

void updateFan() {
  int target;
  if (mode == MODE_OFF) {
    target = 0;
  } else if (mode == MODE_MANUAL) {
    target = manualSpeed;
  } else {
    target = hrTarget();
    if (target < fanSpeed) {                // slowing down: wait first
      if (pendingSpeed != target) {
        pendingSpeed = target;
        pendingSince = millis();
      }
      if (millis() - pendingSince < STEP_DOWN_DELAY_MS) return;
    }
  }
  pendingSpeed = -1;
  applySpeed(target);
}

int pendingSecs() {
  if (pendingSpeed < 0) return -1;
  uint32_t el = millis() - pendingSince;
  return el < STEP_DOWN_DELAY_MS ? (STEP_DOWN_DELAY_MS - el) / 1000 + 1 : 0;
}

// ===== LCD screen =====
void setupLcd() {
  Wire.begin(LCD_SDA, LCD_SCL);
  uint8_t found = 0;
  for (uint8_t a = 0x20; a <= 0x3F && !found; a++) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) found = a;
  }
  if (!found) {
    Serial.println("No LCD found. Running without screen.");
    return;
  }
  Serial.printf("LCD found at 0x%02X\n", found);
  lcd = new LiquidCrystal_I2C(found, 16, 2);
  lcd->init();
  lcd->backlight();
  lcd->setCursor(0, 0);
  lcd->print("Starting...");
}

void lcdShow(const String& l1, const String& l2) {
  if (!lcd) return;
  if (l1 != lcdLine1) {
    lcd->setCursor(0, 0);
    lcd->print((l1 + "                ").substring(0, 16));
    lcdLine1 = l1;
  }
  if (l2 != lcdLine2) {
    lcd->setCursor(0, 1);
    lcd->print((l2 + "                ").substring(0, 16));
    lcdLine2 = l2;
  }
}

void lcdUpdate() {
  if (!lcd) return;
  const char* FAN_SHORT[4] = {"Off", "Low", "Med", "High"};
  char l1[17], l2[17];

  if (!hrConnected) {
    snprintf(l1, sizeof(l1), "Scanning...");
  } else {
    char hr[4];
    if (heartRate > 0) snprintf(hr, sizeof(hr), "%d", (int)heartRate);
    else snprintf(hr, sizeof(hr), "--");
    snprintf(l1, sizeof(l1), "HR %3s Fan:%-4s", hr, FAN_SHORT[fanSpeed]);
  }

  // Line 2 rotates between status and IP every 3 seconds
  bool showIp = (millis() / 3000) % 2 == 1 && WiFi.status() == WL_CONNECTED;
  int pend = pendingSecs();
  if (showIp) {
    snprintf(l2, sizeof(l2), "%s", WiFi.localIP().toString().c_str());
  } else if (pend > 0) {
    snprintf(l2, sizeof(l2), "Slowing in %ds", pend);
  } else if (mode == MODE_OFF) {
    snprintf(l2, sizeof(l2), "Mode: Off");
  } else if (mode == MODE_MANUAL) {
    snprintf(l2, sizeof(l2), "Mode: Manual");
  } else {
    snprintf(l2, sizeof(l2), "Mode: Heart rate");
  }
  lcdShow(l1, l2);
}

// ===== Bluetooth heart rate =====
void onHr(BLERemoteCharacteristic* c, uint8_t* d, size_t len, bool notify) {
  if (len < 2) return;
  int bpm = ((d[0] & 0x01) && len >= 3) ? (d[1] | (d[2] << 8)) : d[1];
  heartRate = bpm;
  lastHrTime = millis();
}

class ConnCB : public BLEClientCallbacks {
  void onConnect(BLEClient* c) { hrConnected = true; }
  void onDisconnect(BLEClient* c) {
    hrConnected = false;
    heartRate = 0;
    Serial.println("Heart rate strap disconnected. Searching...");
  }
};

class AdvCB : public BLEAdvertisedDeviceCallbacks {
  void onResult(BLEAdvertisedDevice dev) {
    if (doConnect) return;
    if (!dev.haveServiceUUID() || !dev.isAdvertisingService(HR_SERVICE)) return;
    if (strlen(HRM_ADDRESS) > 0) {
      String addr = dev.getAddress().toString().c_str();
      if (!addr.equalsIgnoreCase(HRM_ADDRESS)) return;
    }
    BLEDevice::getScan()->stop();
    if (hrDevice) delete hrDevice;
    hrDevice = new BLEAdvertisedDevice(dev);
    doConnect = true;
  }
};

void startScan() {
  scanning = true;
  scanStart = millis();
  BLEDevice::getScan()->start(5, nullptr, false);   // non-blocking 5 s scan
}

void connectHrm() {
  doConnect = false;
  scanning = false;
  BLEDevice::getScan()->clearResults();
  Serial.printf("Connecting to %s (%s)\n",
    hrDevice->getName().c_str(), hrDevice->getAddress().toString().c_str());

  if (!client) {
    client = BLEDevice::createClient();
    client->setClientCallbacks(new ConnCB());
  }
  if (!client->connect(hrDevice)) { Serial.println("Connect failed."); return; }

  BLERemoteService* s = client->getService(HR_SERVICE);
  if (!s) { client->disconnect(); return; }
  BLERemoteCharacteristic* ch = s->getCharacteristic(HR_MEASURE);
  if (!ch || !ch->canNotify()) { client->disconnect(); return; }
  ch->registerForNotify(onHr);

  hrName = hrDevice->getName().c_str();
  if (hrName.length() == 0) hrName = "heart rate strap";
  hrName.replace("\"", "");
  Serial.printf("Connected to %s\n", hrName.c_str());
}

// ===== Saved settings =====
void loadSettings() {
  prefs.begin("fan", true);
  mode = prefs.getInt("mode", MODE_HR);
  manualSpeed = prefs.getInt("manual", 1);
  zLow = prefs.getInt("zl", 100);
  zMed = prefs.getInt("zm", 130);
  zHigh = prefs.getInt("zh", 150);
  prefs.end();
}

void saveSettings() {
  prefs.begin("fan", false);
  prefs.putInt("mode", mode);
  prefs.putInt("manual", manualSpeed);
  prefs.putInt("zl", zLow);
  prefs.putInt("zm", zMed);
  prefs.putInt("zh", zHigh);
  prefs.end();
}

// ===== Web handlers =====
void handleRoot() { server.send_P(200, "text/html", PAGE); }

void handleStatus() {
  int pend = pendingSecs();
  char buf[256];
  snprintf(buf, sizeof(buf),
    "{\"hr\":%d,\"conn\":%d,\"name\":\"%s\",\"mode\":%d,\"manual\":%d,\"fan\":%d,\"z\":[%d,%d,%d],\"pend\":%d}",
    (int)heartRate, hrConnected ? 1 : 0, hrName.c_str(), mode, manualSpeed, fanSpeed,
    zLow, zMed, zHigh, pend);
  server.send(200, "application/json", buf);
}

void handleSet() {
  if (server.hasArg("mode")) {
    int m = server.arg("mode").toInt();
    if (m >= MODE_OFF && m <= MODE_MANUAL) mode = m;
  }
  if (server.hasArg("speed")) {
    int s = server.arg("speed").toInt();
    if (s >= 1 && s <= 3) manualSpeed = s;
  }
  pendingSpeed = -1;
  saveSettings();
  updateFan();
  server.send(200, "text/plain", "ok");
}

void handleZones() {
  int l = server.arg("low").toInt();
  int m = server.arg("med").toInt();
  int h = server.arg("high").toInt();
  if (l < 40 || h > 220 || !(l < m && m < h)) {
    server.send(400, "text/plain", "Use Low < Medium < High, between 40 and 220 bpm.");
    return;
  }
  zLow = l; zMed = m; zHigh = h;
  saveSettings();
  server.send(200, "text/plain", "Zones saved.");
}

// ===== Main =====
void setup() {
  Serial.begin(115200);

  for (int i = 0; i < 3; i++) {
    pinMode(RELAY_PINS[i], OUTPUT);
    relayWrite(i, false);
  }
  loadSettings();
  setupLcd();

  WiFi.mode(WIFI_STA);
  WiFi.setHostname("fan-control");
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("Joining WiFi");
  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 15000) {
    delay(500);
    Serial.print(".");
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("\nControl panel: http://%s\n", WiFi.localIP().toString().c_str());
    if (MDNS.begin("fan")) Serial.println("Also try: http://fan.local");
  } else {
    Serial.println("\nWiFi failed. Fan still runs on heart rate. Check WiFi name and password.");
  }

  server.on("/", handleRoot);
  server.on("/status", handleStatus);
  server.on("/set", handleSet);
  server.on("/zones", handleZones);
  server.begin();

  BLEDevice::init("");
  BLEScan* scan = BLEDevice::getScan();
  scan->setAdvertisedDeviceCallbacks(new AdvCB());
  scan->setInterval(1349);
  scan->setWindow(449);
  scan->setActiveScan(true);
}

void loop() {
  server.handleClient();

  if (doConnect) connectHrm();

  if (!hrConnected && !doConnect) {
    if (scanning && millis() - scanStart > 6000) {
      scanning = false;
      BLEDevice::getScan()->clearResults();
    }
    if (!scanning && (scanStart == 0 || millis() - scanStart > 8000)) startScan();
  }

  static uint32_t lastFan = 0;
  if (millis() - lastFan >= 500) {
    lastFan = millis();
    updateFan();
    lcdUpdate();
  }
}

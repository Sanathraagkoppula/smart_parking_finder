/* =========================================================================
   SMART PARKING SLOT FINDER  -  ESP32 Firmware  v3
   -------------------------------------------------------------------------
   v3 changes:
   - Credentials moved to secrets.h (git-ignored)
   - Signs in with a dedicated device account (no new anonymous user per boot)
   - "updated" uses Firebase server timestamp (ms) - no NTP fallback bug
   - Failed pushes are retried until Firebase accepts them
   - Demo slots 2-6 are only written if missing (not overwritten every boot)
   - WiFi / Firebase connect timeouts -> auto restart instead of hanging
   Hardware: HC-SR04 Echo is 5 V - use a 1k / 2k divider into GPIO 34.
   ========================================================================= */

#include <WiFi.h>
#include <Firebase_ESP_Client.h>
#include "addons/TokenHelper.h"
#include "addons/RTDBHelper.h"
#include "secrets.h"

/* ── PINS ──────────────────────────────────────────── */
#define TRIG_PIN   13
#define ECHO_PIN   34

/* ── DEMO STATES slots 2-6 (index 0 = slot 2) ──────── */
const bool DEMO_FILLED[5]         = { true, false, true, true, false };
const bool DEMO_OVERWRITE_ON_BOOT = false;   // true = reset demo slots every boot

/* ── DETECTION TUNING ───────────────────────────────── */
const float         ENTER_CM      = 20.0;
const float         EXIT_CM       = 25.0;
const int           EXIT_CONFIRM  = 4;
const int           SAMPLES       = 5;
const unsigned long ECHO_TIMEOUT  = 25000;
const unsigned long CYCLE_MS      = 800;

/* ── CONNECTION TIMEOUTS ────────────────────────────── */
const unsigned long WIFI_TIMEOUT_MS     = 30000;
const unsigned long FB_READY_TIMEOUT_MS = 30000;

/* ── GLOBALS ─────────────────────────────────────────── */
FirebaseData   fbdo;
FirebaseAuth   auth;
FirebaseConfig config;

bool slot1Filled = false;
int  pushedState = -1;     // -1 = never pushed, 0 = empty, 1 = filled
int  exitCounter = 0;

/* ── SENSOR ──────────────────────────────────────────── */
float readOnce(){
  digitalWrite(TRIG_PIN, LOW);  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH); delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);
  unsigned long dur = pulseIn(ECHO_PIN, HIGH, ECHO_TIMEOUT);
  if(dur == 0) return -1.0;
  return dur * 0.0343 / 2.0;
}

float readDistance(){
  float v[SAMPLES];
  int   n = 0;
  for(int i=0; i<SAMPLES; i++){
    float d = readOnce();
    if(d > 0) v[n++] = d;
    delay(15);
  }
  if(n == 0) return -1.0;                 // all timeouts - caller holds state
  for(int i=1; i<n; i++){
    float key = v[i]; int j = i-1;
    while(j>=0 && v[j]>key){ v[j+1]=v[j]; j--; }
    v[j+1] = key;
  }
  return v[n/2];
}

bool decideState(bool wasFilled, float dist){
  if(dist < 0) return wasFilled;
  if(dist < ENTER_CM){ exitCounter = 0; return true; }
  if(dist > EXIT_CM){
    if(exitCounter < EXIT_CONFIRM) exitCounter++;
    return (exitCounter >= EXIT_CONFIRM) ? false : wasFilled;
  }
  exitCounter = 0;
  return wasFilled;
}

/* ── FIREBASE ────────────────────────────────────────── */
bool pushSlot(int slotNumber, bool filled, float dist){
  String path = "/parking/slot" + String(slotNumber);
  FirebaseJson json;
  json.set("status",       filled ? "filled" : "empty");
  json.set("distance",     dist > 0 ? (int)dist : -1);
  json.set("slot",         slotNumber);
  json.set("updated/.sv",  "timestamp");   // server time in ms
  if(Firebase.RTDB.setJSON(&fbdo, path.c_str(), &json)){
    Serial.printf("  ✅ slot%d -> %s (%.0f cm)\n", slotNumber, filled ? "FILLED" : "empty", dist);
    return true;
  }
  Serial.printf("  ❌ slot%d push failed: %s\n", slotNumber, fbdo.errorReason().c_str());
  return false;
}

bool slotExists(int slotNumber){
  String path = "/parking/slot" + String(slotNumber) + "/status";
  if(!Firebase.RTDB.get(&fbdo, path.c_str())) return false;
  return fbdo.dataType() != "null";
}

void writeDemoSlots(){
  Serial.println("📋 Checking demo slots 2-6...");
  for(int i=0; i<5; i++){
    int slot = i + 2;
    if(!DEMO_OVERWRITE_ON_BOOT && slotExists(slot)){
      Serial.printf("  slot%d exists - kept\n", slot);
      continue;
    }
    pushSlot(slot, DEMO_FILLED[i], DEMO_FILLED[i] ? 8.0 : 30.0);
    delay(200);
  }
}

/* ── CONNECT ─────────────────────────────────────────── */
void connectWiFi(){
  Serial.printf("🔍 Connecting to WiFi: %s\n", WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  unsigned long start = millis();
  while(WiFi.status() != WL_CONNECTED){
    if(millis() - start > WIFI_TIMEOUT_MS){
      Serial.println("\n❌ WiFi timeout - restarting");
      ESP.restart();
    }
    Serial.print(".");
    delay(400);
  }
  Serial.printf("\n✅ WiFi connected. IP: %s\n", WiFi.localIP().toString().c_str());
}

void connectFirebase(){
  config.api_key       = FIREBASE_API_KEY;
  config.database_url  = FIREBASE_DATABASE_URL;
  auth.user.email      = DEVICE_EMAIL;
  auth.user.password   = DEVICE_PASSWORD;
  config.token_status_callback = tokenStatusCallback;
  Firebase.reconnectWiFi(true);
  Firebase.begin(&config, &auth);

  Serial.print("🔍 Waiting for Firebase");
  unsigned long start = millis();
  while(!Firebase.ready()){
    if(millis() - start > FB_READY_TIMEOUT_MS){
      Serial.println("\n❌ Firebase sign-in timeout - check DEVICE_EMAIL / DEVICE_PASSWORD. Restarting");
      ESP.restart();
    }
    Serial.print(".");
    delay(300);
  }
  Serial.println("\n✅ Firebase ready");
}

/* ── SETUP ───────────────────────────────────────────── */
void setup(){
  Serial.begin(115200);
  delay(300);
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  digitalWrite(TRIG_PIN, LOW);

  connectWiFi();
  connectFirebase();
  writeDemoSlots();
  Serial.println("✅ Live scan starting on Slot 1...");
}

/* ── LOOP ────────────────────────────────────────────── */
void loop(){
  if(WiFi.status() != WL_CONNECTED){ delay(500); return; }   // library reconnects
  if(!Firebase.ready()){ delay(200); return; }

  float dist  = readDistance();
  slot1Filled = decideState(slot1Filled, dist);

  int want = slot1Filled ? 1 : 0;
  if(want != pushedState){
    if(pushSlot(1, slot1Filled, dist)) pushedState = want;   // retried next cycle on failure
  }

  Serial.printf("Slot 1: %.1f cm -> %s\n", dist, slot1Filled ? "FILLED" : "empty");
  delay(CYCLE_MS);
}

/* =========================================================================
   SMART PARKING SLOT FINDER  -  ESP32 Firmware  v2 (fixed)
   -------------------------------------------------------------------------
   Fixes applied:
   - Credentials moved to secrets.h (add to .gitignore)
   - NTP real timestamp instead of millis()
   - Consistent all-timeout handling (holds previous state)
   ========================================================================= */

#include <WiFi.h>
#include <time.h>
#include <Firebase_ESP_Client.h>
#include "addons/TokenHelper.h"
#include "addons/RTDBHelper.h"
#include "secrets.h"   // <-- put credentials there, never commit this file

/* ── PINS ──────────────────────────────────────────── */
#define TRIG_PIN   13
#define ECHO_PIN   34

/* ── DEMO STATES slots 2-6 (index 0 = slot 2) ──────── */
const bool DEMO_FILLED[5] = { true, false, true, true, false };

/* ── DETECTION TUNING ───────────────────────────────── */
const float        ENTER_CM     = 20.0;
const float        EXIT_CM      = 25.0;
const int          EXIT_CONFIRM = 4;
const int          SAMPLES      = 5;
const long         ECHO_TIMEOUT = 25000;
const unsigned long CYCLE_MS   = 800;

/* ── NTP ─────────────────────────────────────────────── */
const char* NTP_SERVER   = "pool.ntp.org";
const long  GMT_OFFSET   = 19800;   // IST = UTC+5:30 in seconds
const int   DAYLIGHT     = 0;

/* ── GLOBALS ─────────────────────────────────────────── */
FirebaseData   fbdo;
FirebaseAuth   auth;
FirebaseConfig config;

bool slot1Filled = false;
int  exitCounter = 0;
bool firstRun    = true;

/* ── HELPERS ─────────────────────────────────────────── */
float readOnce(){
  digitalWrite(TRIG_PIN, LOW);  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH); delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);
  long dur = pulseIn(ECHO_PIN, HIGH, ECHO_TIMEOUT);
  if(dur == 0) return -1.0;
  return dur * 0.0343 / 2.0;
}

// FIX: returns -1 if ALL readings timed out → caller holds previous state
// No more forced-FILLED on timeout — that was wrong behaviour
float readDistance(){
  float v[SAMPLES];
  int   n = 0;
  for(int i=0; i<SAMPLES; i++){
    float d = readOnce();
    if(d > 0) v[n++] = d;
    delay(15);
  }
  if(n == 0) return -1.0;  // FIX: signal "uncertain" — do not force FILLED
  for(int i=1; i<n; i++){
    float key=v[i]; int j=i-1;
    while(j>=0 && v[j]>key){ v[j+1]=v[j]; j--; }
    v[j+1]=key;
  }
  return v[n/2];
}

bool decideState(bool wasFilled, float dist){
  if(dist < 0){
    // All readings timed out — uncertain, hold previous state
    return wasFilled;
  }
  if(dist < ENTER_CM){ exitCounter=0; return true; }
  if(dist > EXIT_CM){
    exitCounter++;
    if(exitCounter >= EXIT_CONFIRM) return false;
    return wasFilled;
  }
  exitCounter=0;
  return wasFilled;
}

// FIX: use real Unix timestamp from NTP, not millis()
long getRealTimestamp(){
  struct tm timeinfo;
  if(!getLocalTime(&timeinfo)) return (long)(millis()/1000);  // fallback if NTP not ready
  return (long)mktime(&timeinfo);
}

void pushSlot(int slotNumber, bool filled, float dist){
  String path = "/parking/slot" + String(slotNumber);
  FirebaseJson json;
  json.set("status",   filled ? "filled" : "empty");
  json.set("distance", dist > 0 ? (int)dist : -1);
  json.set("slot",     slotNumber);
  json.set("updated",  getRealTimestamp());  // FIX: real Unix timestamp
  if(Firebase.RTDB.setJSON(&fbdo, path.c_str(), &json)){
    Serial.printf("  slot%d -> %s (%.0f cm)\n", slotNumber, filled?"FILLED":"empty", dist);
  } else {
    Serial.printf("  slot%d FAILED: %s\n", slotNumber, fbdo.errorReason().c_str());
  }
}

/* ── SETUP ───────────────────────────────────────────── */
void setup(){
  Serial.begin(115200);
  delay(300);
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  digitalWrite(TRIG_PIN, LOW);

  // WiFi — credentials come from secrets.h
  Serial.printf("Connecting to WiFi: %s\n", WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while(WiFi.status() != WL_CONNECTED){ Serial.print("."); delay(400); }
  Serial.printf("\nWiFi connected. IP: %s\n", WiFi.localIP().toString().c_str());

  // NTP sync — FIX: so getRealTimestamp() returns a real time
  configTime(GMT_OFFSET, DAYLIGHT, NTP_SERVER);
  Serial.print("Syncing NTP");
  struct tm timeinfo;
  for(int i=0; i<20; i++){
    if(getLocalTime(&timeinfo)){ Serial.println(" OK"); break; }
    Serial.print("."); delay(500);
  }

  // Firebase
  config.api_key      = FIREBASE_API_KEY;      // from secrets.h
  config.database_url = FIREBASE_DATABASE_URL;  // from secrets.h
  if(Firebase.signUp(&config, &auth, "", "")){
    Serial.println("Firebase anonymous sign-in OK");
  } else {
    Serial.printf("Firebase sign-in error: %s\n", config.signer.signupError.message.c_str());
  }
  config.token_status_callback = tokenStatusCallback;
  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);
  while(!Firebase.ready()) delay(200);

  Serial.println("Writing demo states for Slots 2-6…");
  for(int i=0; i<5; i++){
    float demoDist = DEMO_FILLED[i] ? 8.0 : 30.0;
    pushSlot(i+2, DEMO_FILLED[i], demoDist);
    delay(200);
  }
  Serial.println("Live scan starting on Slot 1…");
}

/* ── LOOP ────────────────────────────────────────────── */
void loop(){
  if(!Firebase.ready()) return;
  float dist     = readDistance();
  bool  newState = decideState(slot1Filled, dist);
  if(firstRun || newState != slot1Filled){
    slot1Filled = newState;
    pushSlot(1, newState, dist);
  }
  Serial.printf("Slot 1: %.1f cm -> %s\n", dist, slot1Filled?"FILLED":"empty");
  firstRun = false;
  delay(CYCLE_MS);
}
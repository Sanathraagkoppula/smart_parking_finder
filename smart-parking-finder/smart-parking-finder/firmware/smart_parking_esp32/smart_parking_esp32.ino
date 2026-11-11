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
   - Logs each completed parking session to /history (for "free by" estimates)
   Wiring (HC-SR04 -> ESP32):
     VCC  -> VIN (5V, board on USB)
     GND  -> GND
     Trig -> D13
     Echo -> D34   (direct; a 1k/2k divider or an HC-SR04P 3.3V sensor is safer long-term)
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
const bool DEMO_OVERWRITE_ON_BOOT = true;    // true = rewrite demo slots (fresh times) every boot

/* ── DETECTION TUNING ───────────────────────────────── */
const float         ENTER_CM      = 5.0;    // closer than this  -> FILLED
const float         EXIT_CM       = 15.0;   // farther than this -> empty (after EXIT_CONFIRM reads)
const int           EXIT_CONFIRM  = 3;      // far readings in a row before "empty" (~1 s)
const int           SAMPLES       = 3;      // median of 3 (odd number)
const unsigned long ECHO_TIMEOUT  = 15000;  // µs; ~2.5 m max range, shorter wait on no echo
const unsigned long SAMPLE_GAP_MS = 10;     // between samples
const unsigned long CYCLE_MS      = 200;    // pause between scans -> ~4 updates/s
const unsigned long HEARTBEAT_MS  = 20000;  // keeps the Firebase connection warm (no reconnect delay)
const unsigned long PRINT_EVERY_MS = 1000;  // Serial status line rate

/* ── CONNECTION TIMEOUTS ────────────────────────────── */
const unsigned long WIFI_TIMEOUT_MS     = 30000;
const unsigned long FB_READY_TIMEOUT_MS = 30000;
const unsigned long SIGNIN_FAIL_WAIT_MS = 60000;   // wait before restarting after a failed sign-in

/* ── GLOBALS ─────────────────────────────────────────── */
FirebaseData   fbdo;
FirebaseAuth   auth;
FirebaseConfig config;

bool slot1Filled = false;
int  pushedState = -1;     // -1 = never pushed, 0 = empty, 1 = filled
int  exitCounter = 0;

int  noEchoCycles = 0;                       // consecutive scans with no echo at all
const int NO_ECHO_AS_EMPTY = 20;             // ~5 s of silence -> treat slot as empty
const int NO_ECHO_WARN_EVERY = 60;           // repeat the wiring hint every ~15 s
unsigned long lastFirebaseIO = 0;            // last successful talk to Firebase
unsigned long lastPrint      = 0;

unsigned long occupiedAtMs       = 0;
bool          occupiedStartKnown = false;   // false if car was already there at boot
const unsigned long MIN_SESSION_SEC = 60;   // ignore stays shorter than 1 min

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
    if(i < SAMPLES-1) delay(SAMPLE_GAP_MS);
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
  unsigned long t0 = millis();
  if(Firebase.RTDB.setJSON(&fbdo, path.c_str(), &json)){
    lastFirebaseIO = millis();
    Serial.printf("  ✅ slot%d -> %s (%.0f cm) in %lu ms\n", slotNumber, filled ? "FILLED" : "empty", dist, millis() - t0);
    return true;
  }
  Serial.printf("  ❌ slot%d push failed: %s\n", slotNumber, fbdo.errorReason().c_str());
  return false;
}

// Small periodic write so the TLS connection never goes idle (idle -> server closes it ->
// the next real update would wait 1-3 s for a new handshake). Also tells the dashboard
// the sensor is online.
void heartbeat(){
  FirebaseJson json;
  json.set("lastSeen/.sv", "timestamp");
  json.set("rssi", WiFi.RSSI());
  json.set("uptimeSec", (int)(millis() / 1000));
  Firebase.RTDB.updateNode(&fbdo, "/devices/slot1", &json);
  lastFirebaseIO = millis();                // also on failure: retry in 20 s, not every loop
}

// Records one completed parking session -> /history/slotN/<pushId>
// The dashboard uses these to estimate when an occupied slot will be free.
void logSession(int slotNumber, unsigned long durationSec){
  if(durationSec < MIN_SESSION_SEC) return;
  String path = "/history/slot" + String(slotNumber);
  FirebaseJson json;
  json.set("durationSec", (int)durationSec);
  json.set("endedAt/.sv", "timestamp");
  if(Firebase.RTDB.pushJSON(&fbdo, path.c_str(), &json)){
    Serial.printf("  📋 slot%d session logged: %lu min\n", slotNumber, durationSec / 60);
  } else {
    Serial.printf("  ❌ slot%d session log failed: %s\n", slotNumber, fbdo.errorReason().c_str());
  }
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
  config.database_url  = FIREBASE_DATABASE_URL;
#ifdef DATABASE_SECRET
  // Database-secret mode: no sign-in step, so no rate limits or lockouts.
  config.signer.tokens.legacy_token = DATABASE_SECRET;
  Serial.println("🔍 Firebase mode: database secret");
#else
  config.api_key       = FIREBASE_API_KEY;
  auth.user.email      = DEVICE_EMAIL;
  auth.user.password   = DEVICE_PASSWORD;
  config.token_status_callback = tokenStatusCallback;
  config.max_token_generation_retry = 3;   // avoid TOO_MANY_ATTEMPTS lockout on wrong password
  Serial.printf("🔍 Firebase mode: device account %s\n", DEVICE_EMAIL);
#endif
  Firebase.reconnectWiFi(true);
  fbdo.setBSSLBufferSize(4096, 1024);   // larger SSL buffers -> fewer "ssl engine closed" drops
  fbdo.setResponseSize(2048);
  config.timeout.serverResponse = 10 * 1000;
  Firebase.begin(&config, &auth);

  Serial.print("🔍 Waiting for Firebase");
  unsigned long start = millis();
  while(!Firebase.ready()){
    if(millis() - start > FB_READY_TIMEOUT_MS){
      Serial.println("\n❌ Firebase sign-in failed.");
      Serial.println("   Check: user DEVICE_EMAIL exists in Firebase Authentication,");
      Serial.println("   its password matches DEVICE_PASSWORD, and Email/Password sign-in is enabled.");
      Serial.println("   Waiting 60 s before retrying (avoids TOO_MANY_ATTEMPTS lockout)...");
      delay(SIGNIN_FAIL_WAIT_MS);
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
  heartbeat();
  Serial.println("✅ Live scan starting on Slot 1...");
}

/* ── LOOP ────────────────────────────────────────────── */
void loop(){
  if(WiFi.status() != WL_CONNECTED){ delay(500); return; }   // library reconnects
  if(!Firebase.ready()){ delay(200); return; }

  float dist  = readDistance();

  // No echo can mean "nothing in range" or a wiring fault. Short gaps keep the
  // last state; a long silence counts as empty so the slot never sticks on FILLED.
  float effective = dist;
  if(dist < 0){
    noEchoCycles++;
    if(noEchoCycles >= NO_ECHO_AS_EMPTY) effective = EXIT_CM + 1;
    if(noEchoCycles == NO_ECHO_AS_EMPTY || noEchoCycles % NO_ECHO_WARN_EVERY == 0){
      Serial.println("⚠️  No echo from HC-SR04. Check: Echo -> D34, Trig -> D13,");
      Serial.println("    VCC -> VIN (5V), GND -> GND, and point it at a surface within 2 m.");
    }
  } else {
    if(noEchoCycles >= NO_ECHO_AS_EMPTY) Serial.println("✅ Echo is back");
    noEchoCycles = 0;
  }
  slot1Filled = decideState(slot1Filled, effective);

  int want = slot1Filled ? 1 : 0;
  if(want != pushedState){
    int prev = pushedState;
    if(pushSlot(1, slot1Filled, dist)){                      // retried next cycle on failure
      if(want == 1){
        occupiedAtMs       = millis();
        occupiedStartKnown = (prev != -1);                   // unknown if already parked at boot
      } else if(prev == 1 && occupiedStartKnown){
        logSession(1, (millis() - occupiedAtMs) / 1000);
        occupiedStartKnown = false;
      }
      pushedState = want;
    }
  }

  if(millis() - lastFirebaseIO > HEARTBEAT_MS) heartbeat();

  if(millis() - lastPrint >= PRINT_EVERY_MS){
    lastPrint = millis();
    Serial.printf("Slot 1: %.1f cm -> %s\n", dist, slot1Filled ? "FILLED" : "empty");
  }
  delay(CYCLE_MS);
}

/* =========================================================================
   SMART PARKING SLOT FINDER  -  ESP32 Firmware  (DEMO MODE)
   -------------------------------------------------------------------------
   1 x HC-SR04 ultrasonic sensor  ->  Slot 1 (live)
   Slots 2-6  ->  hardcoded demo states written to Firebase on boot

   Wiring (no resistors — acceptable for short demo use):
     HC-SR04 VCC  -> ESP32 VIN  (5 V)
     HC-SR04 GND  -> ESP32 GND
     HC-SR04 Trig -> GPIO 13
     HC-SR04 Echo -> GPIO 34   ← NOTE: GPIO34 is input-only; 5 V echo
                                  signal is slightly over spec but works
                                  fine for demo. Add a 1kΩ/2kΩ divider
                                  for a production build.

   LIBRARY REQUIRED (install via Arduino Library Manager):
     "Firebase Arduino Client Library for ESP8266 and ESP32"  by Mobizt
   ========================================================================= */

#include <WiFi.h>
#include <Firebase_ESP_Client.h>
#include "addons/TokenHelper.h"
#include "addons/RTDBHelper.h"


/* ----------------------- 1. USER CONFIGURATION ------------------------- */

#define WIFI_SSID       "sanathraag"
#define WIFI_PASSWORD   "sanathraag"

#define API_KEY         "AIzaSyDa-LhnJNCX_fMFYxfMOUFEMnYJlJwY6eM"
#define DATABASE_URL    "https://smart-parking-60cdf-default-rtdb.asia-southeast1.firebasedatabase.app"


/* ----------------------- 2. REAL SENSOR PINS (Slot 1 only) ------------- */

#define TRIG_PIN   13
#define ECHO_PIN   34


/* ----------------------- 3. DEMO STATES FOR SLOTS 2-6 ------------------ */
//   Index 0 = Slot 2,  Index 1 = Slot 3, ... Index 4 = Slot 6
//   true  = FILLED,  false = empty
const bool DEMO_FILLED[5] = { true, false, true, true, false };
//                             Slot2  Slot3  Slot4  Slot5  Slot6


/* ----------------------- 4. DETECTION TUNING (Slot 1) ------------------ */

const float ENTER_CM      = 20.0;  // below this -> FILLED  (raised for hand detection)
const float EXIT_CM       = 25.0;  // above this counts as "looks empty"
const int   EXIT_CONFIRM  = 4;     // need this many consecutive "empty" readings
                                    // before actually switching to empty — stops
                                    // a few bad bounces from flipping the state

const int   SAMPLES       = 5;     // median filter window (odd)
const long  ECHO_TIMEOUT  = 25000; // µs (~4 m max range)
const unsigned long CYCLE_MS = 800;


/* ----------------------- 5. GLOBALS ------------------------------------ */

FirebaseData   fbdo;
FirebaseAuth   auth;
FirebaseConfig config;

bool slot1Filled  = false;
int  exitCounter  = 0;    // consecutive "looks empty" readings
bool firstRun     = true;


/* ----------------------- 6. HELPERS ------------------------------------ */

// Returns distance in cm, or -1 on timeout.
float readOnce() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);

  long dur = pulseIn(ECHO_PIN, HIGH, ECHO_TIMEOUT);
  if (dur == 0) return -1.0;
  return dur * 0.0343 / 2.0;
}

// Median of valid SAMPLES readings.
// Returns -1 if ALL readings timed out (caller will keep previous state).
float readDistance() {
  float v[SAMPLES];
  int   n = 0;

  for (int i = 0; i < SAMPLES; i++) {
    float d = readOnce();
    if (d > 0) v[n++] = d;
    delay(15);
  }

  if (n == 0) return 1.0;    // all timeouts -> object too close (blind zone) -> treat as FILLED

  // insertion sort
  for (int i = 1; i < n; i++) {
    float key = v[i];
    int   j   = i - 1;
    while (j >= 0 && v[j] > key) { v[j + 1] = v[j]; j--; }
    v[j + 1] = key;
  }
  return v[n / 2];
}

// Returns the new state for Slot 1.
// dist == -1 means uncertain (all readings timed out) -> keep current state.
bool decideState(bool wasFilled, float dist) {
  if (dist < ENTER_CM) {
    // Clearly something close -> FILLED, reset exit counter
    exitCounter = 0;
    return true;
  }

  if (dist > EXIT_CM) {
    // Looks empty — but only commit after EXIT_CONFIRM consecutive readings
    exitCounter++;
    if (exitCounter >= EXIT_CONFIRM) {
      return false;
    }
    return wasFilled;   // not enough evidence yet — hold state
  }

  // In the dead zone between ENTER and EXIT -> hold state, reset counter
  exitCounter = 0;
  return wasFilled;
}

void pushSlot(int slotNumber, bool filled, float dist) {
  String path = "/parking/slot" + String(slotNumber);

  FirebaseJson json;
  json.set("status",   filled ? "filled" : "empty");
  json.set("distance", (int)dist);
  json.set("slot",     slotNumber);
  json.set("updated",  (int)millis());

  if (Firebase.RTDB.setJSON(&fbdo, path.c_str(), &json)) {
    Serial.printf("  slot%d -> %s (%.0f cm)\n",
                  slotNumber, filled ? "FILLED" : "empty", dist);
  } else {
    Serial.printf("  slot%d write FAILED: %s\n",
                  slotNumber, fbdo.errorReason().c_str());
  }
}


/* ----------------------- 7. SETUP -------------------------------------- */

void setup() {
  Serial.begin(115200);
  delay(300);

  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  digitalWrite(TRIG_PIN, LOW);

  // --- WiFi ---
  Serial.printf("Connecting to WiFi: %s\n", WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(400);
  }
  Serial.printf("\nWiFi connected. IP: %s\n", WiFi.localIP().toString().c_str());

  // --- Firebase ---
  config.api_key      = API_KEY;
  config.database_url = DATABASE_URL;

  if (Firebase.signUp(&config, &auth, "", "")) {
    Serial.println("Firebase sign-in OK");
  } else {
    Serial.printf("Firebase sign-in error: %s\n",
                  config.signer.signupError.message.c_str());
  }

  config.token_status_callback = tokenStatusCallback;
  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);

  Serial.println("Waiting for Firebase...");
  while (!Firebase.ready()) delay(200);

  Serial.println("Writing demo states for Slots 2-6...");
  for (int i = 0; i < 5; i++) {
    float demoDist = DEMO_FILLED[i] ? 8.0 : 30.0;
    pushSlot(i + 2, DEMO_FILLED[i], demoDist);
    delay(200);
  }
  Serial.println("Demo states written. Starting live scan of Slot 1...");
}


/* ----------------------- 8. MAIN LOOP ---------------------------------- */

void loop() {
  if (!Firebase.ready()) return;

  float dist     = readDistance();
  bool  newState = decideState(slot1Filled, dist);

  if (firstRun || newState != slot1Filled) {
    slot1Filled = newState;
    pushSlot(1, newState, dist);
  }

  // Serial log every cycle so you can watch in Serial Monitor
  Serial.printf("Slot 1: %.1f cm -> %s\n",
                dist, slot1Filled ? "FILLED" : "empty");

  firstRun = false;
  delay(CYCLE_MS);
}

// Copy this file to secrets.h and fill in your values.
#ifndef SECRETS_H
#define SECRETS_H

#define WIFI_SSID             "YOUR_WIFI_SSID"
#define WIFI_PASSWORD         "YOUR_WIFI_PASSWORD"

#define FIREBASE_API_KEY      "YOUR_FIREBASE_API_KEY"
#define FIREBASE_DATABASE_URL "https://YOUR_PROJECT_ID-default-rtdb.REGION.firebasedatabase.app"

// Device account (Firebase Console -> Authentication -> Add user).
// Must match the email in database.rules.json.
#define DEVICE_EMAIL          "esp32-device@smartparking.app"
#define DEVICE_PASSWORD       "YOUR_DEVICE_PASSWORD"

// OPTIONAL (recommended if sign-in gets blocked): database secret mode.
// Firebase Console -> Project settings -> Service accounts -> Database secrets -> Show.
// Uncomment the line below and the ESP32 skips email/password sign-in entirely.
// Keep this file private - the secret gives full database access.
// #define DATABASE_SECRET     "YOUR_DATABASE_SECRET"

#endif

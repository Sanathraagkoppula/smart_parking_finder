# 🅿️ Smart Parking Slot Finder

An IoT-based real-time parking management system built with **ESP32**, **HC-SR04 ultrasonic sensors**, and **Firebase**. Detects slot occupancy live and displays availability through a web dashboard with user authentication, booking, payments, and countdown timers.

> **Academic Project** — SDLC Subject, B.Tech CSE (Cybersecurity), SR University, Hyderabad

---

## ✨ Features

- 🔴🟢 **Live slot status** — ESP32 reads ultrasonic sensor and pushes filled/empty state to Firebase in real time
- 🔐 **Firebase Authentication** — Email/password login, signup, and guest (anonymous) access
- 📍 **Location filtering** — Users pick a campus area (North Wing, South Wing, Main Gate) to see only relevant slots
- 💳 **Booking & Payment** — Select duration (30 min → Full Day), pay via UPI / Card / Net Banking mock flow
- ⏳ **Live countdowns** — All users can see how much time is left on any reserved slot
- 👤 **User profile** — Edit display name, vehicle number, phone, department, and avatar color (persisted to Firebase)
- 📋 **My Bookings panel** — Full booking history with slot, duration, amount, and vehicle

---

## 🛠️ Tech Stack

| Layer | Technology |
|---|---|
| Microcontroller | ESP32 DevKit V1 |
| Sensor | HC-SR04 Ultrasonic (Slot 1 live; Slots 2–6 demo) |
| Backend / DB | Firebase Realtime Database (asia-southeast1) |
| Auth | Firebase Authentication |
| Frontend | Single-file HTML + Vanilla JS (no build step) |
| Firmware IDE | Arduino IDE |

---

## 📁 Repository Structure

```
smart-parking-finder/
├── firmware/
│   └── smart_parking_esp32.ino   # ESP32 Arduino sketch
├── dashboard/
│   └── smart_parking_dashboard.html  # Web dashboard (open in browser)
└── README.md
```

---

## ⚡ Quick Start

### 1 — Clone the repo

```bash
git clone https://github.com/<your-username>/smart-parking-finder.git
cd smart-parking-finder
```

### 2 — Firebase setup

1. Go to [Firebase Console](https://console.firebase.google.com) → create a project
2. Enable **Realtime Database** (start in test mode)
3. Enable **Authentication** → Email/Password + Anonymous providers
4. Copy your project credentials (API key, database URL)
5. Set these database rules:

```json
{
  "rules": {
    "parking":         { ".read": true,  ".write": true },
    "active_bookings": { ".read": true,  ".write": true },
    "bookings": {
      "$uid": {
        ".read":  "auth.uid === $uid",
        ".write": "auth.uid === $uid"
      }
    },
    "users": {
      "$uid": {
        ".read":  "auth.uid === $uid",
        ".write": "auth.uid === $uid"
      }
    }
  }
}
```

### 3 — Flash the ESP32

**Hardware wiring:**

```
HC-SR04 VCC  → ESP32 VIN  (5V)
HC-SR04 GND  → ESP32 GND
HC-SR04 Trig → GPIO 13
HC-SR04 Echo → GPIO 34
```

> ⚠️ GPIO 34 is input-only and slightly over-spec at 5 V echo. Acceptable for demos; add a 1 kΩ/2 kΩ voltage divider for production.

**Arduino Library (install via Library Manager):**
```
Firebase Arduino Client Library for ESP8266 and ESP32  — by Mobizt
```

**Edit `firmware/smart_parking_esp32.ino` — update these constants:**

```cpp
#define WIFI_SSID       "YOUR_WIFI_SSID"
#define WIFI_PASSWORD   "YOUR_WIFI_PASSWORD"

#define API_KEY         "YOUR_FIREBASE_API_KEY"
#define DATABASE_URL    "https://YOUR_PROJECT_ID-default-rtdb.REGION.firebasedatabase.app"
```

Flash to your ESP32 via Arduino IDE (Board: `ESP32 Dev Module`, Upload Speed: `115200`).

### 4 — Open the dashboard

1. Open `dashboard/smart_parking_dashboard.html` in any browser — **no server needed**
2. Update the Firebase config block near the top of the file:

```js
const firebaseConfig = {
  apiKey:      "YOUR_FIREBASE_API_KEY",
  authDomain:  "YOUR_PROJECT_ID.firebaseapp.com",
  databaseURL: "https://YOUR_PROJECT_ID-default-rtdb.REGION.firebasedatabase.app",
  projectId:   "YOUR_PROJECT_ID",
};
```

3. Sign up / log in → pick a location → monitor and book slots live

---

## 🗄️ Firebase Data Structure

```
/parking/
  slot1/  { status: "filled"|"empty", distance: 8, slot: 1, updated: 1234567 }
  slot2/  { ... }
  ...

/active_bookings/
  slot3/  { uid, name, expiresAt, duration, slotLabel }

/bookings/
  {uid}/
    {bookingId}/  { slot, slotLabel, duration, amount, vehicle, status, bookedAt, expiresAt }

/users/
  {uid}/
    profile/  { displayName, vehicle, phone, department, avatarColor, updatedAt }
```

---

## 🔧 Detection Tuning (ESP32)

| Constant | Default | Meaning |
|---|---|---|
| `ENTER_CM` | 20 cm | Object closer than this → slot **FILLED** |
| `EXIT_CM` | 25 cm | Object farther than this starts exit countdown |
| `EXIT_CONFIRM` | 4 | Consecutive "empty" readings before flipping to empty |
| `SAMPLES` | 5 | Median filter window (odd number) |
| `CYCLE_MS` | 800 ms | Loop delay between readings |

Adjust `ENTER_CM` and `EXIT_CM` based on the height of your sensor mount above the parking surface.

---

## 🖥️ Dashboard Screens

| Screen | Description |
|---|---|
| **Auth** | Login / Signup / Guest access |
| **Location Picker** | Choose campus area to filter visible slots |
| **Main Dashboard** | Live slot grid, filter tabs, summary bar |
| **Booking Modal** | Duration selection (30 min → Full Day, ₹10–₹120) |
| **Payment Modal** | UPI / Card / Net Banking (mock flow) |
| **My Bookings** | Personal booking history |
| **Profile Modal** | Edit name, vehicle, phone, department, avatar color |

---

## 💰 Pricing

| Duration | Price |
|---|---|
| 30 minutes | ₹10 |
| 1 hour | ₹20 |
| 2 hours | ₹35 |
| 3 hours | ₹50 |
| 5 hours | ₹80 |
| Full Day | ₹120 |

---

## 🚧 Limitations / Known Notes

- **Slots 2–6 are hardcoded demo states** in the firmware. Only Slot 1 has a live sensor.
- Payment is a **mock flow** — no real payment gateway is integrated.
- The dashboard is a **single HTML file** with no backend or build system.
- Guest users cannot edit their profile (no Firebase UID persistence across sessions).

---

## 🔮 Possible Improvements

- [ ] Add sensors to all 6 slots (needs 5 more HC-SR04 or a multiplexer)
- [ ] Real payment gateway (Razorpay / PayU)
- [ ] Admin panel to manage slots and view all bookings
- [ ] Booking cancellation / extension
- [ ] SMS/email notification on booking confirmation
- [ ] Mobile app (Flutter / React Native)

---

## 👨‍💻 Author

**Sangeeth Thumuganti**  
B.Tech CSE (Cybersecurity), SR University, Hyderabad  
[LinkedIn](https://www.linkedin.com/in/sangeeth-thumuganti-472bb2334)

---

## 📄 License

This project is for academic purposes. Feel free to fork and build on it.

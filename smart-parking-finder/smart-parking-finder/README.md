# 🅿️ Smart Parking Slot Finder

An IoT-based real-time parking management system built with **ESP32**, **HC-SR04 ultrasonic sensors**, and **Firebase**. Detects slot occupancy live and shows availability on a web dashboard with authentication, slot booking, Razorpay payments, and live countdowns.

> **Academic Project** — SDLC Subject, B.Tech CSE (Cybersecurity), SR University, Hyderabad

---

## ✨ Features

- 🔴🟢 **Live slot status** — ESP32 reads the ultrasonic sensor (median filter + hysteresis) and pushes filled/empty state to Firebase only when it changes
- 🔐 **Firebase Authentication** — Email/password login, signup, and guest (anonymous) access
- 📍 **Location filtering** — Pick a campus area (North Wing, South Wing, Main Gate) to see only its slots
- 💳 **Booking & Razorpay payment** — Slot is **held for 5 minutes** before payment, confirmed atomically after payment (no double booking, no paying for a slot someone else took)
- ⏳ **Live countdowns** — Everyone sees time left on reserved slots; expired reservations free up automatically (server-synced clock)
- ✕ **Cancellation** — Cancel an active booking from *My Bookings* (refunds handled manually by admin)
- 👤 **User profile** — Display name, vehicle number, phone, department, avatar colour (saved to Firebase)
- 🗺️ **Directions** — One tap opens Google Maps to the slot

---

## 🛠️ Tech Stack

| Layer | Technology |
|---|---|
| Microcontroller | ESP32 DevKit V1 |
| Sensor | HC-SR04 Ultrasonic (Slot 1 live; Slots 2–6 demo) |
| Backend / DB | Firebase Realtime Database (asia-southeast1) |
| Auth | Firebase Authentication (Email/Password + Anonymous) |
| Payments | Razorpay Standard Checkout (test mode) |
| Frontend | Single-file HTML + Vanilla JS (no build step) |
| Firmware IDE | Arduino IDE |

---

## 📁 Repository Structure

```
smart-parking-finder/
├── firmware/
│   └── smart_parking_esp32/
│       ├── smart_parking_esp32.ino   # ESP32 sketch
│       ├── secrets.example.h         # template — copy to secrets.h
│       └── secrets.h                 # your credentials (git-ignored)
├── dashboard/
│   └── smart_parking_dashboard.html  # web dashboard
├── database.rules.json               # Firebase RTDB security rules
├── .gitignore
└── README.md
```

---

## ⚡ Quick Start

### 1 — Clone

```bash
git clone https://github.com/<your-username>/smart-parking-finder.git
cd smart-parking-finder
```

### 2 — Firebase setup

1. [Firebase Console](https://console.firebase.google.com) → create a project
2. **Realtime Database** → create (locked mode)
3. **Authentication → Sign-in method** → enable **Email/Password** and **Anonymous**
4. **Authentication → Users → Add user** → create the device account
   `esp32@smartparking.app` with a strong password (the ESP32 signs in with it)
5. **Realtime Database → Rules** → paste the contents of [`database.rules.json`](database.rules.json) → Publish

What the rules enforce:

| Path | Read | Write |
|---|---|---|
| `/parking` | anyone | **only the ESP32 device account**, status must be `filled`/`empty` |
| `/active_bookings/slotN` | signed-in users | create only if free/expired, with `bookedBy` = yourself; modify/delete only your own; anyone may delete an **expired** entry; holds ≤ 6 min, bookings ≤ 12 h |
| `/bookings/{uid}` | owner only | owner only |
| `/users/{uid}` | owner only | owner only |

> If you change the device email, change it in **both** `database.rules.json` and `secrets.h`.

### 3 — Flash the ESP32

**Wiring:**

```
HC-SR04 VCC  → ESP32 VIN (5V)
HC-SR04 GND  → ESP32 GND
HC-SR04 Trig → GPIO 13
HC-SR04 Echo → 1 kΩ → GPIO 34 → 2 kΩ → GND     (voltage divider: 5 V → 3.3 V)
```

> ⚠️ The ESP32 is a 3.3 V chip — don't connect the 5 V Echo pin directly.

**Library** (Arduino Library Manager): *Firebase Arduino Client Library for ESP8266 and ESP32* — by Mobizt

**Credentials:** copy `secrets.example.h` → `secrets.h` and fill in:

```cpp
#define WIFI_SSID             "YOUR_WIFI_SSID"
#define WIFI_PASSWORD         "YOUR_WIFI_PASSWORD"
#define FIREBASE_API_KEY      "YOUR_FIREBASE_API_KEY"
#define FIREBASE_DATABASE_URL "https://YOUR_PROJECT_ID-default-rtdb.REGION.firebasedatabase.app"
#define DEVICE_EMAIL          "esp32@smartparking.app"
#define DEVICE_PASSWORD       "YOUR_DEVICE_PASSWORD"
```

Open `firmware/smart_parking_esp32/smart_parking_esp32.ino`, select **ESP32 Dev Module**, upload, and open Serial Monitor at **115200 baud**.

### 4 — Open the dashboard

1. Update the `firebaseConfig` block and `RAZORPAY_KEY` near the top of the `<script>` in `dashboard/smart_parking_dashboard.html`
2. Open it in a browser (or host on Firebase Hosting / GitHub Pages)
3. Sign up / log in → pick a location → monitor and book slots

> The Firebase web `apiKey` and Razorpay `key_id` are **public identifiers** — they are safe in client code. Security comes from the database rules. Never put the Razorpay **key secret** in the dashboard.

---

## 🔄 Booking Flow

```
Book Slot → choose duration → Pay
   │
   ├─ 1. HOLD     transaction on /active_bookings/slotN  {status:'hold', expires in 5 min}
   │              └─ fails if someone else holds/booked it → "Slot was just taken"
   ├─ 2. PAY      Razorpay Checkout (auto-closes after 4 min, before the hold ends)
   │              └─ closed/cancelled → hold released immediately
   └─ 3. CONFIRM  transaction converts hold → {status:'confirmed', expiresAt}
                  + receipt written to /bookings/{uid}/{bookingId}
                  └─ if the slot was lost anyway → receipt saved as "refund_pending"
                     with the payment ID, user told to contact admin
```

All times use the Firebase server clock (`.info/serverTimeOffset`), so a wrong phone clock can't break countdowns.

---

## 🗄️ Firebase Data Structure

```
/parking/
  slot1/ { status: "filled"|"empty", distance: 8, slot: 1, updated: <server ms> }
  ...

/active_bookings/
  slot3/ { bookedBy, bookingId, status: "hold"|"confirmed", expiresAt,
           duration, durationMins, locationName, txnId? }

/bookings/
  {uid}/
    {bookingId}/ { bookingId, txnId, slot, slotLabel, locationName, duration,
                   durationMins, amount, status: "active"|"cancelled"|"refund_pending",
                   bookedAt, expiresAt, payMethod, vehicle }

/users/
  {uid}/
    profile/ { displayName, vehicle, phone, department, avatarColor, updatedAt }
```

---

## 🔧 Detection Tuning (ESP32)

| Constant | Default | Meaning |
|---|---|---|
| `ENTER_CM` | 20 cm | Closer than this → slot **FILLED** immediately |
| `EXIT_CM` | 25 cm | Farther than this counts toward empty |
| `EXIT_CONFIRM` | 4 | Consecutive far readings needed to flip to empty |
| `SAMPLES` | 5 | Median filter window (odd number) |
| `CYCLE_MS` | 800 ms | Delay between scans |
| `DEMO_OVERWRITE_ON_BOOT` | `false` | `true` resets demo slots 2–6 on every boot |

Readings between 20–25 cm (dead band) and sensor timeouts keep the previous state. Tune `ENTER_CM`/`EXIT_CM` to your sensor mounting height.

---

## 💰 Pricing

| Duration | Price |
|---|---|
| 30 minutes | ₹10 |
| 1 hour | ₹20 |
| 2 hours | ₹35 |
| 3 hours | ₹50 |
| 5 hours | ₹80 |
| Full Day (12 h) | ₹120 |

---

## 🚧 Limitations / Known Notes

- **Slots 2–6 are demo states** written by the firmware; only Slot 1 has a live sensor.
- **Payments are not server-verified.** Razorpay signature verification needs the key secret, which can only live on a server. Without a backend, a technically skilled user could create a booking without paying. Fix: a Firebase Cloud Function (Blaze plan) that verifies `razorpay_signature` and is the only writer of confirmed bookings.
- Refunds are manual (Razorpay dashboard).
- "One active booking per user" is enforced in the UI, not in the database rules.
- Guest accounts can't change their display name, and a guest's bookings are lost after signing out.

---

## 🔮 Possible Improvements

- [ ] Server-side Razorpay verification + automatic refunds (Cloud Functions)
- [ ] Sensors on all 6 slots
- [ ] Device heartbeat so the dashboard can show "sensor offline"
- [ ] Admin panel to manage slots and view all bookings
- [ ] Booking extension
- [ ] SMS/email confirmation
- [ ] Mobile app (Flutter / React Native)

---

## 👨‍💻 Author

**koppula sanath raag **
B.Tech CSE (Cybersecurity), SR University, warangal

---

## 📄 License

Academic project. Feel free to fork and build on it.

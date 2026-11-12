# 🅿️ Smart Parking Slot Finder

An IoT-based real-time parking management system built with **ESP32**, **HC-SR04 ultrasonic sensors**, and **Firebase**. Detects slot occupancy live and shows availability on a web dashboard with authentication, slot booking, Razorpay payments, and live countdowns.

> **Academic Project** — SDLC Subject, B.Tech CSE (Cybersecurity), SR University, Hyderabad

---

## ✨ Features

- 🔴🟢 **Live slot status** — ESP32 reads the ultrasonic sensor (median filter + hysteresis) and pushes filled/empty state to Firebase only when it changes
- 🔐 **Firebase Authentication** — Email/password, **Continue with Google** (one button for login and signup), and guest access
- 🔑 **Passwords** — *Forgot password?* sends a Firebase reset link; users change their password in **Profile → Password** (current password required); admins can send any user a reset email from the Users drawer
- 📍 **Location filtering** — Pick a campus area (North Wing, South Wing, Main Gate) to see only its slots
- 💳 **Booking & Razorpay payment** — Slot is **held for 5 minutes** before payment, confirmed atomically after payment (no double booking, no paying for a slot someone else took)
- 🕒 **When will it be free?** — Reserved slots show the exact time they free up. Occupied slots show how long the car has been parked and an estimate: the median of that slot's last 20 stays, or of all slots' stays, or an assumed 60-minute stay (`DEFAULT_STAY_MIN`) until 3+ stays are recorded. When every slot is full, the summary shows which slot is likely to open next
- ⏳ **Live countdowns** — Everyone sees time left on reserved slots; expired reservations free up automatically (server-synced clock)
- 💳 **Demo checkout (default)** — A built-in card form: the driver enters any card details (e.g. 4111 1111 1111 1111, any future expiry, any CVV), clicks **Make payment** and the slot is booked. No money is charged and only the card brand and last 4 digits are saved on the receipt. Switch to real Razorpay Checkout in **Admin → Settings → Checkout used by drivers** (`config/paymentMode`: `demo` | `razorpay`). Demo refunds are recorded in the admin panel without a Razorpay refund ID
- 🚫 **No-show auto-release** — On a slot with a live sensor, the driver must arrive within **15 minutes** of paying (`config/noShowMin`, admin-adjustable, 0 = off). If the sensor never sees a vehicle in that window, the booking is released for other drivers and marked **No-show** (fee not refunded automatically; the admin can still refund). Once the car is detected the booking is safe until it ends, even if the car leaves early
- ✕ **Cancellation** — Cancel an active booking from *My Bookings* (refunds handled manually by admin)
- 🚗 **Driver details** — Vehicle number is collected at signup; name + vehicle are confirmed on every booking (pre-filled from the profile, validated as an Indian / BH-series plate)
- 👤 **User profile** — Display name, vehicle number, phone, department, avatar colour (saved to Firebase)
- 🗺️ **Directions** — One tap opens Google Maps to the slot
- 🛡️ **Admin panel** (`dashboard/admin.html`) — add/edit/remove parking slots, set manual status for slots without sensors, see every booking, cancel bookings, record refunds, view and edit user details, change the Razorpay key and estimate settings

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
│   ├── smart_parking_dashboard.html  # user dashboard
│   └── admin.html                    # admin panel
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
3. **Authentication → Sign-in method** → enable **Email/Password**, **Anonymous** and **Google** (pick a support email when enabling Google)
4. **Authentication → Users → Add user** → create the device account
   `esp32-device@smartparking.app` with a strong password (the ESP32 signs in with it)
5. **Realtime Database → Rules** → paste the contents of [`database.rules.json`](database.rules.json) → Publish

What the rules enforce:

| Path | Read | Write |
|---|---|---|
| `/admins` | admins (each user can check only their own entry) | nobody — Firebase Console only |
| `/config` | signed-in users | admins |
| `/slots` | signed-in users | admins |
| `/parking` | anyone | the ESP32 device account or an admin, status must be `filled`/`empty` |
| `/history/slotN` | signed-in users | **only the ESP32**, append-only (no edits/deletes) |
| `/devices` | signed-in users | the ESP32 device account or an admin (heartbeat) |
| `/active_bookings/slotN` | signed-in users | create only if free/expired, with `bookedBy` = yourself; modify/delete only your own; anyone may delete an **expired** entry or a **no-show** (confirmed, past `arriveBy`, no `arrivedAt`, live sensor reads empty); the device may set `arrivedAt`; holds ≤ 6 min, bookings ≤ 12 h |
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
#define DEVICE_EMAIL          "esp32-device@smartparking.app"
#define DEVICE_PASSWORD       "YOUR_DEVICE_PASSWORD"
```

> **Sign-in blocked (`TOO_MANY_ATTEMPTS_TRY_LATER`)?** Use database-secret mode instead: Firebase Console → Project settings → Service accounts → **Database secrets** → Show, then add `#define DATABASE_SECRET "..."` to `secrets.h`. The ESP32 then skips email/password sign-in entirely (Serial shows `Firebase mode: database secret`). The secret has full database access, so never commit `secrets.h`.

Open `firmware/smart_parking_esp32/smart_parking_esp32.ino`, select **ESP32 Dev Module**, upload, and open Serial Monitor at **115200 baud**.

### 4 — Open the dashboard

> **Google sign-in needs a web address.** It doesn't work when the HTML file is opened directly (`file:///…`). Use one of:
> - **VS Code → Live Server** extension → right-click `smart_parking_dashboard.html` → *Open with Live Server* (`http://127.0.0.1:5500/…`). `localhost` is allowed by default; add `127.0.0.1` under **Authentication → Settings → Authorized domains** if Google shows *unauthorized domain*.
> - **Firebase Hosting / GitHub Pages / Netlify** — add the site's domain under **Authorized domains**.

1. Update the `firebaseConfig` block near the top of the `<script>` in `dashboard/smart_parking_dashboard.html`
   - **Razorpay Key ID** is read live from Firebase: Realtime Database → Data → add `config` → `razorpayKeyId` = `rzp_test_...`. Change it there any time — no code edit needed. (`RAZORPAY_KEY` in the HTML is only a fallback.)
2. Open it in a browser (or host on Firebase Hosting / GitHub Pages)
3. Sign up / log in → pick a location → monitor and book slots

> The Firebase web `apiKey` and Razorpay `key_id` are **public identifiers** — they are safe in client code. Security comes from the database rules. Never put the Razorpay **key secret** in the dashboard.

---

## 🛡️ Admin Panel

Open `dashboard/admin.html` (keep it in the same folder as the user dashboard).

**Make yourself an admin (one time):**
1. Sign up on the user dashboard with your email, then open `admin.html` and log in. It shows *No admin access* and your **UID** — copy it.
2. Firebase Console → Realtime Database → Data → add `admins` → inside it add your UID as the key with the value `true` (boolean, not text).
3. Reload `admin.html`.

Admins can only be added from Firebase Console — the website can't grant admin rights.

| Section | What the admin can do |
|---|---|
| **Overview** | Free slots, active bookings, refunds waiting, today's revenue, live mini-map of the lot |
| **Slots** | Import the 6 default slots, add a slot (number, zone, coordinates, sensor yes/no), edit, remove, mark manual slots free/occupied |
| **Bookings** | Search and filter every booking; cancel an active booking (optionally adding a refund request); record a refund |
| **Users** | Search users; open a user to edit name, vehicle, phone, department, send a password reset email, and see/act on their bookings |
| **Settings** | Razorpay Key ID, assumed stay for free-time estimates, no-show arrival window |

**Refunds:** a browser page can't move money — Razorpay refunds need the key secret, which must stay on a server. The panel opens the exact payment in the Razorpay dashboard; issue the refund there, paste the `rfnd_…` ID back, and the user sees *Refunded ₹X* in My Bookings. Users who cancel get *Refund requested* automatically, and it appears in the admin's *Refund to process* list.

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
   └─ 4. ARRIVE   live-sensor slot: arriveBy = confirm time + 15 min
                  ├─ sensor sees a car → arrivedAt stamped (by the ESP32, and by the
                  │  driver's own dashboard as a backup) → booking kept until expiresAt
                  └─ arriveBy passes, slot still empty, no arrivedAt → any open dashboard
                     or the admin panel deletes the entry (allowed by the rules only in
                     exactly this case) → booking marked "no_show", slot shows Free
```

The release needs a browser to be open (a user's dashboard after choosing an area, or the admin panel); there is no backend server. Demo slots without a sensor (`live` not `true`) are never auto-released.

All times use the Firebase server clock (`.info/serverTimeOffset`), so a wrong phone clock can't break countdowns.

---

## 🗄️ Firebase Data Structure

```
/admins/
  {uid}: true

/config/
  paymentMode: "demo"|"razorpay"   razorpayKeyId: "rzp_test_…"   defaultStayMin: 60   noShowMin: 15   # 0 = no-show release off

/slots/
  slot1/ { number: 1, zone: "North Wing", zoneNote, lat, lng, sensor: true }

/parking/
  slot1/ { status: "filled"|"empty", distance: 8, slot: 1, live: true, updated: <server ms> }
                                                        # live: true = real sensor (ESP32), false = demo
  ...

/devices/
  slot1/ { lastSeen: <server ms>, rssi: -55, uptimeSec: 3600 }   # ESP32 heartbeat

/history/
  slot1/
    {pushId}/ { durationSec: 3300, endedAt: <server ms> }   # one per completed stay

/active_bookings/
  slot3/ { bookedBy, bookingId, status: "hold"|"confirmed", expiresAt,
           duration, durationMins, locationName, txnId?,
           arriveBy?, arrivedAt? }       # no-show deadline / when the car was detected

/bookings/
  {uid}/
    {bookingId}/ { bookingId, txnId, slot, slotLabel, locationName, duration,
                   durationMins, amount, status: "active"|"cancelled"|"refund_pending"|"no_show",
                   bookedAt, expiresAt, arriveBy?, arrivedAt?, releasedAt?, payMethod, vehicle, driverName,
                   refundStatus: "requested"|"refunded"|"none", refundAmount, refundRef, refundedAt, refundedBy }

/users/
  {uid}/
    profile/ { displayName, email, isGuest, vehicle, phone, department, avatarColor,
               driverName (guests only), createdAt, updatedAt, lastSeen }
```

---

## 🔧 Detection Tuning (ESP32)

| Constant | Default | Meaning |
|---|---|---|
| `ENTER_CM` | 15 cm | Closer than this → slot **FILLED** immediately |
| `EXIT_CM` | 20 cm | Farther than this counts toward empty |
| `EXIT_CONFIRM` | 3 | Consecutive "far" readings before flipping to empty (~1 s) |
| `SAMPLES` | 3 | Median filter window (odd number) |
| `CYCLE_MS` | 200 ms | Pause between scans (~4 scans per second) |
| `ECHO_TIMEOUT` | 15000 µs | Max wait for an echo (~2.5 m range) |
| `HEARTBEAT_MS` | 20 s | Small write to `/devices/slot1` that keeps the Firebase connection open, so updates reach the website in well under a second |
| `DEMO_OVERWRITE_ON_BOOT` | `true` | Rewrites demo slots 2–6 on every boot (fresh timestamps); set `false` to keep manual edits |

Readings between 15–20 cm (dead band) and sensor timeouts keep the previous state. Tune `ENTER_CM`/`EXIT_CM` to your sensor mounting height.

---

## 📱 Works on every screen size

Both pages were checked on every screen (login, forgot password, area picker, lot, booking, payment, success, bookings, profile, and every admin section) at 10 sizes — 320 px and 360–430 px phones, phones in landscape, 768/1024 px tablets, 1366 px laptops, 1920 px and 2560 px monitors — with no sideways scrolling, nothing cut off, no iPhone input zoom, and tap targets ≥ 40 px on touch screens. iPhone notches/home bars (safe areas) and mobile browser address bars (dynamic viewport height) are handled.

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

- [ ] Server-side Razorpay verification + one-click automatic refunds from the admin panel (Cloud Functions)
- [ ] Sensors on all 6 slots
- [ ] Device heartbeat so the dashboard can show "sensor offline"
- [ ] Time-of-day aware estimates (e.g. separate averages for mornings and afternoons)
- [ ] Booking extension
- [ ] SMS/email confirmation
- [ ] Mobile app (Flutter / React Native)

---

## 👨‍💻 Author

**Sangeeth Thumuganti**
B.Tech CSE (Cybersecurity), SR University, Hyderabad
[LinkedIn](https://www.linkedin.com/in/sangeeth-thumuganti-472bb2334)

---

## 📄 License

Academic project. Feel free to fork and build on it.

## Deploying (Firebase Hosting) and installing as an app

```bash
npm install -g firebase-tools
firebase login
firebase deploy            # uploads dashboard/ and publishes database.rules.json
```

- User dashboard: `https://<project-id>.web.app` · Admin: `https://<project-id>.web.app/admin`
- The site is an installable **PWA**: `dashboard/manifest.webmanifest` (name, icons, screenshots, shortcuts) and `dashboard/sw.js` (service worker).
  - Pages load network-first, so users always get the latest version, and fall back to a cached copy or `offline.html` without internet.
  - Firebase, Razorpay and Google requests are never cached, so live slot data is always real time.
- On Android/Chrome: menu → **Install app**. On iPhone/Safari: Share → **Add to Home Screen**.
- After changing `sw.js`, bump `VERSION` (e.g. `sp-v2`) so phones pick up the new cache.


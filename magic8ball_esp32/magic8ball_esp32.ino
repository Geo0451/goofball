/*
  ==========================================================================
  AI Magic 8 Ball / Fortune Teller - ESP32 Firmware
  ==========================================================================
  Hardware:
    - ESP32
    - MPU6050 IMU        (I2C, shares bus with OLED)
    - TTP223 touch sensor (single digital GPIO)
    - 128x64 monochrome OLED, SSD1306 driver (I2C)

  Required Arduino Library Manager installs:
    - Adafruit SSD1306
    - Adafruit GFX Library
    - Adafruit MPU6050
    - Adafruit Unified Sensor
    - ArduinoJson (v6.x)

  Wiring:
    OLED     SDA -> GPIO21   SCL -> GPIO22   (I2C addr 0x3C)
    MPU6050  SDA -> GPIO21   SCL -> GPIO22   (I2C addr 0x68, same bus)
    TTP223   OUT -> GPIO4  (change TOUCH_PIN below if needed)
    Power everything from 3V3 / GND on the ESP32.

  Interaction design:
    - TAP   (short press+release) while an answer/error is shown -> dismiss, back to READY.
    - SHAKE the device in READY               -> ask the oracle (normal persona).
    - HOLD the touch sensor WHILE shaking      -> ask the oracle in its "evil" persona.

  This sketch never stores the AI API key. It only talks to your local
  Flask server over plain HTTP; the server holds the key.
  ==========================================================================
*/

#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>

// ============================== USER CONFIG ==============================
const char* WIFI_SSID     = "YOUR_WIFI_SSID";
const char* WIFI_PASSWORD = "YOUR_WIFI_PASSWORD";

// Point this at your PC's LAN IP where the Flask server is running.
const char* SERVER_URL = "http://192.168.1.100:5000/ask";

#define TOUCH_PIN 27         // TTP223 OUT pin - change freely

#define OLED_SDA   21
#define OLED_SCL   22
#define OLED_ADDR  0x3C
#define SCREEN_WIDTH  128
#define SCREEN_HEIGHT 64

#define MPU_ADDR 0x68
// ===========================================================================

// ============================ TUNABLE BEHAVIOR ============================
#define SHAKE_JERK_THRESHOLD    6.0f   // m/s^2 delta between samples to count as a "jerk"
#define SHAKE_SPIKES_REQUIRED   4      // jerk spikes needed inside the window below
#define SHAKE_WINDOW_MS         800    // window in which spikes must occur
#define SHAKE_COOLDOWN_MS       1500   // ignore new shakes for this long after one fires

#define TOUCH_HOLD_MS           500    // hold duration to count as "held" (evil persona)
#define TAP_MAX_MS              300    // max press duration to still count as a tap

#define ANSWER_DISPLAY_MS       15000  // auto-return to READY after this long
#define ERROR_RETRY_WIFI_MS     4000   // how often to retry WiFi while in NO WIFI error

#define HTTP_TIMEOUT_MS         8000
#define WIFI_CONNECT_TIMEOUT_MS 15000

#define MAX_LINES     6                // usable text lines under the header
#define CHARS_PER_LINE 21              // ~128px / 6px per char at text size 1
// ===========================================================================

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
Adafruit_MPU6050 mpu;

enum State { ST_BOOT, ST_READY, ST_ASKING, ST_SHOW_ANSWER, ST_ERROR };
State state = ST_BOOT;

// ---- Touch tracking ----
bool touchActive = false;
unsigned long touchStartMs = 0;
bool touchHeldFlag = false;   // becomes true once held past TOUCH_HOLD_MS, until release
bool tapEvent = false;        // one-shot flag, set on a short tap release

// ---- Shake tracking ----
bool haveAccelSample = false;
float prevAccelMag = 0.0f;
unsigned long jerkTimestamps[SHAKE_SPIKES_REQUIRED + 4];
int jerkCount = 0;
unsigned long lastShakeFiredMs = 0;
float lastShakeIntensity = 0.0f;
float lastTiltDeg = 0.0f;

// ---- Result state ----
String answerText = "";
String errorText = "";
String currentPersona = "normal";
unsigned long answerShownAt = 0;
unsigned long lastWifiRetryMs = 0;

// ---------------------------------------------------------------------------
// Forward declarations
// ---------------------------------------------------------------------------
void connectWiFi();
void showScreen(const String &title, const String &body);
void showWrappedAnswer(const String &title, const String &text);
int wrapText(const String &text, String outLines[], int maxLines, int maxCharsPerLine);
void handleTouch();
bool detectShake();
void goReady();
void goAsking();
void askServer();
void goShowAnswer(const String &text);
void goError(const String &msg);

// ---------------------------------------------------------------------------
// Setup
// ---------------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  delay(200);

  pinMode(TOUCH_PIN, INPUT);

  Wire.begin(OLED_SDA, OLED_SCL);

  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    Serial.println("FATAL: OLED init failed. Check wiring/address.");
    while (true) { delay(1000); }
  }
  display.clearDisplay();
  display.display();

  showScreen("MAGIC 8 BALL", "BOOTING...");

  if (!mpu.begin(MPU_ADDR)) {
    Serial.println("FATAL: MPU6050 not found. Check wiring/address.");
    showScreen("MAGIC 8 BALL", "MPU6050 ERROR\nCHECK WIRING");
    while (true) { delay(1000); }
  }
  mpu.setAccelerometerRange(MPU6050_RANGE_8_G);
  mpu.setGyroRange(MPU6050_RANGE_500_DEG);
  mpu.setFilterBandwidth(MPU6050_BAND_21_HZ);

  showScreen("MAGIC 8 BALL", "CONNECTING\nWIFI...");
  connectWiFi();

  if (WiFi.status() == WL_CONNECTED) {
    goReady();
  } else {
    goError("NO WIFI");
  }
}

// ---------------------------------------------------------------------------
// Main loop
// ---------------------------------------------------------------------------
void loop() {
  handleTouch();
  bool shook = detectShake();  // always sample, so jerk history stays fresh

  switch (state) {

    case ST_READY: {
      if (WiFi.status() != WL_CONNECTED) {
        goError("NO WIFI");
        break;
      }
      if (shook) {
        currentPersona = touchHeldFlag ? "evil" : "normal";
        goAsking();
      }
      break;
    }

    case ST_ASKING:
      askServer(); // blocking; transitions internally to SHOW_ANSWER or ERROR
      break;

    case ST_SHOW_ANSWER:
      if (tapEvent || shook || (millis() - answerShownAt > ANSWER_DISPLAY_MS)) {
        tapEvent = false;
        goReady();
      }
      break;

    case ST_ERROR:
      if (errorText == "NO WIFI") {
        if (millis() - lastWifiRetryMs > ERROR_RETRY_WIFI_MS) {
          lastWifiRetryMs = millis();
          WiFi.reconnect();
        }
        if (WiFi.status() == WL_CONNECTED) {
          goReady();
          break;
        }
      }
      if (tapEvent || shook) {
        tapEvent = false;
        goReady();
      }
      break;

    case ST_BOOT:
    default:
      goReady();
      break;
  }

  delay(20);
}

// ---------------------------------------------------------------------------
// WiFi
// ---------------------------------------------------------------------------
void connectWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < WIFI_CONNECT_TIMEOUT_MS) {
    delay(250);
    Serial.print(".");
  }
  Serial.println();
  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("WiFi connected. IP: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("WiFi connect failed / timed out.");
  }
}

// ---------------------------------------------------------------------------
// Touch handling (edge + hold + tap detection)
// ---------------------------------------------------------------------------
void handleTouch() {
  bool raw = digitalRead(TOUCH_PIN) == HIGH;

  if (raw && !touchActive) {
    // rising edge
    touchActive = true;
    touchStartMs = millis();
    touchHeldFlag = false;
  } else if (raw && touchActive) {
    // still held
    if (!touchHeldFlag && (millis() - touchStartMs >= TOUCH_HOLD_MS)) {
      touchHeldFlag = true;
    }
  } else if (!raw && touchActive) {
    // falling edge / release
    unsigned long duration = millis() - touchStartMs;
    if (duration <= TAP_MAX_MS) {
      tapEvent = true;
    }
    touchActive = false;
    touchHeldFlag = false;
  }
}

// ---------------------------------------------------------------------------
// Shake detection via jerk (rate of change of acceleration magnitude),
// rather than a single fragile absolute-acceleration threshold.
// ---------------------------------------------------------------------------
bool detectShake() {
  sensors_event_t accel, gyro, temp;
  mpu.getEvent(&accel, &gyro, &temp);

  float ax = accel.acceleration.x;
  float ay = accel.acceleration.y;
  float az = accel.acceleration.z;
  float mag = sqrtf(ax * ax + ay * ay + az * az);

  // rough tilt angle, just used as descriptive telemetry sent to the server
  lastTiltDeg = atan2f(ax, az) * 180.0f / PI;

  bool fired = false;
  unsigned long now = millis();

  if (haveAccelSample) {
    float jerk = fabsf(mag - prevAccelMag);
    if (jerk > lastShakeIntensity) lastShakeIntensity = jerk;

    if (jerk > SHAKE_JERK_THRESHOLD) {
      // record this spike, dropping any spikes older than the window
      if (jerkCount < (int)(sizeof(jerkTimestamps) / sizeof(jerkTimestamps[0]))) {
        jerkTimestamps[jerkCount++] = now;
      }
    }

    // drop stale spikes
    int keep = 0;
    for (int i = 0; i < jerkCount; i++) {
      if (now - jerkTimestamps[i] <= SHAKE_WINDOW_MS) {
        jerkTimestamps[keep++] = jerkTimestamps[i];
      }
    }
    jerkCount = keep;

    if (jerkCount >= SHAKE_SPIKES_REQUIRED && (now - lastShakeFiredMs > SHAKE_COOLDOWN_MS)) {
      fired = true;
      lastShakeFiredMs = now;
      jerkCount = 0;
    }
  }

  prevAccelMag = mag;
  haveAccelSample = true;

  return fired;
}

// ---------------------------------------------------------------------------
// State transitions
// ---------------------------------------------------------------------------
void goReady() {
  state = ST_READY;
  lastShakeIntensity = 0;
  showScreen("READY", "SHAKE ME\n\nHOLD+SHAKE\nFOR EVIL MODE");
}

void goAsking() {
  state = ST_ASKING;
  String title = (currentPersona == "evil") ? "SUMMONING..." : "ASKING FATE...";
  showScreen(title, "PLEASE WAIT");
}

void goShowAnswer(const String &text) {
  state = ST_SHOW_ANSWER;
  answerText = text;
  answerShownAt = millis();
  String title = (currentPersona == "evil") ? "THE DARK ORACLE" : "THE ORACLE SAYS";
  showWrappedAnswer(title, text);
}

void goError(const String &msg) {
  state = ST_ERROR;
  errorText = msg;
  showScreen("ERROR", msg + "\n\nTAP OR SHAKE\nTO RETRY");
}

// ---------------------------------------------------------------------------
// Server communication
// ---------------------------------------------------------------------------
void askServer() {
  HTTPClient http;
  http.setTimeout(HTTP_TIMEOUT_MS);

  if (!http.begin(SERVER_URL)) {
    goError("SERVER ERROR");
    return;
  }
  http.addHeader("Content-Type", "application/json");

  StaticJsonDocument<256> reqDoc;
  reqDoc["mode"] = "magic8";
  reqDoc["interaction"] = "shake";
  reqDoc["shake_intensity"] = lastShakeIntensity;
  reqDoc["rotation"] = lastTiltDeg;
  reqDoc["persona"] = currentPersona;

  String body;
  serializeJson(reqDoc, body);

  int code = http.POST(body);

  if (code <= 0) {
    // connection-level failure: no route to host, refused, timeout, etc.
    Serial.printf("HTTP POST failed: %s\n", http.errorToString(code).c_str());
    http.end();
    goError("SERVER ERROR");
    return;
  }

  String payload = http.getString();
  http.end();

  if (code != 200) {
    Serial.printf("HTTP status %d: %s\n", code, payload.c_str());
    goError("SERVER ERROR");
    return;
  }

  if (payload.length() == 0) {
    goError("EMPTY REPLY");
    return;
  }

  StaticJsonDocument<512> respDoc;
  DeserializationError err = deserializeJson(respDoc, payload);
  if (err) {
    Serial.printf("JSON parse failed: %s\n", err.c_str());
    goError("BAD RESPONSE");
    return;
  }

  if (respDoc.containsKey("error")) {
    Serial.printf("Server reported error: %s\n", respDoc["error"].as<const char*>());
    goError("AI ERROR");
    return;
  }

  if (!respDoc.containsKey("answer")) {
    goError("BAD RESPONSE");
    return;
  }

  String answer = respDoc["answer"].as<String>();
  answer.trim();
  if (answer.length() == 0) {
    goError("EMPTY REPLY");
    return;
  }

  goShowAnswer(answer);
}

// ---------------------------------------------------------------------------
// Display helpers
// ---------------------------------------------------------------------------
void showScreen(const String &title, const String &body) {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println(title);
  display.drawFastHLine(0, 10, SCREEN_WIDTH, SSD1306_WHITE);

  display.setCursor(0, 16);
  display.println(body);

  display.display();
}

void showWrappedAnswer(const String &title, const String &text) {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);

  display.setCursor(0, 0);
  display.println(title);
  display.drawFastHLine(0, 10, SCREEN_WIDTH, SSD1306_WHITE);

  String lines[MAX_LINES];
  int count = wrapText(text, lines, MAX_LINES, CHARS_PER_LINE);

  int y = 16;
  for (int i = 0; i < count; i++) {
    display.setCursor(0, y);
    display.println(lines[i]);
    y += 8;
  }

  display.display();
}

// Greedy word-wrap. Splits on existing newlines first, then wraps each
// paragraph on word boundaries to maxCharsPerLine. If the text still
// doesn't fit in maxLines, the last visible line is truncated with "..."
// so nothing overflows the 128x64 screen.
int wrapText(const String &text, String outLines[], int maxLines, int maxCharsPerLine) {
  int lineCount = 0;

  int start = 0;
  int textLen = text.length();

  while (start < textLen && lineCount < maxLines) {
    int nlIdx = text.indexOf('\n', start);
    String paragraph = (nlIdx == -1) ? text.substring(start) : text.substring(start, nlIdx);

    int pStart = 0;
    int pLen = paragraph.length();

    if (pLen == 0) {
      outLines[lineCount++] = "";
    }

    while (pStart < pLen && lineCount < maxLines) {
      int remaining = pLen - pStart;
      int takeLen = min(remaining, maxCharsPerLine);
      String candidate = paragraph.substring(pStart, pStart + takeLen);

      if (takeLen == remaining) {
        // fits with no more text in this paragraph
        outLines[lineCount++] = candidate;
        pStart += takeLen;
      } else {
        // try to break on the last space within the candidate
        int breakAt = candidate.lastIndexOf(' ');
        if (breakAt <= 0) {
          // no good space to break on, hard-cut
          outLines[lineCount++] = candidate;
          pStart += takeLen;
        } else {
          outLines[lineCount++] = candidate.substring(0, breakAt);
          pStart += breakAt + 1; // skip the space
        }
      }
    }

    start = (nlIdx == -1) ? textLen : nlIdx + 1;
  }

  // If there's leftover text we couldn't fit, mark the last line with "..."
  if (start < textLen && lineCount > 0) {
    String &last = outLines[lineCount - 1];
    if (last.length() > maxCharsPerLine - 3) {
      last = last.substring(0, maxCharsPerLine - 3);
    }
    last += "...";
  }

  return lineCount;
}

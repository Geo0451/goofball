#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

// --- CONFIGURATION ---
const char* WIFI_SSID = "fedora";
const char* WIFI_PASS = "password12";
const char* OLLAMA_URL = "http://10.42.0.1:11434/api/generate"; 

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_ADDR 0x3C
#define MPU_ADDR  0x68
#define TOUCH_PIN 27

#define SHAKE_THRESHOLD 1.7

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

int16_t ax, ay, az;
bool systemArmed = false;
bool lastTouchState = LOW;
String currentFortune = "SHAKE ME!";

// Eye Icon Bitmaps (8x8 pixels)
const unsigned char PROGMEM eye_open_bmp[] = {
  0b00000000,
  0b00111100,
  0b01000010,
  0b01001010, // Center pupil
  0b01000010,
  0b00111100,
  0b00000000,
  0b00000000
};

const unsigned char PROGMEM eye_closed_bmp[] = {
  0b00000000,
  0b00000000,
  0b01000010,
  0b00111100, // Closed arc line
  0b01010101, // Eyelashes
  0b00000000,
  0b00000000,
  0b00000000
};

// Function Declarations
void initMPU();
void readMPU();
void checkTouchToggle();
bool checkShake();
void playShakingAnimation();
void drawStatusBar();
void drawWrappedText(String text, int startY);
void renderScreen();
String fetchQwenFortune();

// -----------------------------------------------------------------------------
// SETUP
// -----------------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  Wire.begin(21, 22);
  pinMode(TOUCH_PIN, INPUT);

  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    while (true); // Freeze on OLED failure
  }

  // Set rotation to match your breadboard orientation:
  // 0 = Landscape, 1 = Portrait (90°), 2 = Inverted Landscape, 3 = Portrait (270°)
  display.setRotation(1); 

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 20);
  display.print("Connecting WiFi...");
  display.display();

  while (WiFi.status() != WL_CONNECTED) {
    delay(250);
  }

  initMPU();
  renderScreen();
}

// -----------------------------------------------------------------------------
// MAIN LOOP
// -----------------------------------------------------------------------------
void loop() {
  checkTouchToggle();
  readMPU();

  if (systemArmed && checkShake()) {
    playShakingAnimation();
    currentFortune = fetchQwenFortune();
    renderScreen();
    delay(2000); // Cooldown delay
  }

  delay(30);
}

// -----------------------------------------------------------------------------
// DISPLAY & GRAPHICS FUNCTIONS
// -----------------------------------------------------------------------------

void drawStatusBar() {
  int w = display.width();
  display.drawLine(0, 9, w - 1, 9, SSD1306_WHITE); 

  if (systemArmed) {
    display.drawBitmap(w - 10, 0, eye_open_bmp, 8, 8, SSD1306_WHITE);
  } else {
    display.drawBitmap(w - 10, 0, eye_closed_bmp, 8, 8, SSD1306_WHITE);
  }
}

void drawWrappedText(String text, int startY) {
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setTextWrap(false); // Handle word wrapping manually

  // Clean LLM formatting artifacts
  text.replace("\"", "");

  // Sanitize to printable ASCII
  String cleanText = "";
  for (int i = 0; i < text.length(); i++) {
    char c = text.charAt(i);
    if (c >= 32 && c <= 126) cleanText += c;
  }

  int cursorX = 0;
  int cursorY = startY;
  int i = 0;
  int screenW = display.width();
  int screenH = display.height();

  while (i < cleanText.length()) {
    int nextSpace = cleanText.indexOf(' ', i);
    if (nextSpace == -1) nextSpace = cleanText.length();

    String word = cleanText.substring(i, nextSpace);
    int wordPixelWidth = word.length() * 6; // 6 pixels per character at size 1

    // Wrap to next line if word exceeds width
    if (cursorX + wordPixelWidth > screenW) {
      cursorX = 0;
      cursorY += 10;
    }

    if (cursorY > screenH - 10) break; // Screen bottom boundary check

    display.setCursor(cursorX, cursorY);
    display.print(word);
    cursorX += wordPixelWidth; // Shift cursor position right by word width

    if (nextSpace < cleanText.length()) {
      display.print(" ");
      cursorX += 6; // Shift cursor position right by space width
    }

    i = nextSpace + 1;
  }
}

void renderScreen() {
  display.clearDisplay();
  drawStatusBar();
  drawWrappedText(currentFortune, 13); 
  display.display();
}

void playShakingAnimation() {
  unsigned long startTime = millis();
  int w = display.width();
  int h = display.height();

  while (millis() - startTime < 1200) {
    display.clearDisplay();
    drawStatusBar();

    int ballX = random(16, w - 16);
    int ballY = random(20, h - 16);

    display.drawCircle(ballX, ballY, 14, SSD1306_WHITE);
    display.fillCircle(ballX, ballY, 6, SSD1306_WHITE);
    display.setTextColor(SSD1306_BLACK);
    display.setCursor(ballX - 2, ballY - 3);
    display.print("8");

    display.display();
    delay(40);
  }
}

// -----------------------------------------------------------------------------
// OLLAMA API COMMUNICATION
// -----------------------------------------------------------------------------
String fetchQwenFortune() {
  if (WiFi.status() != WL_CONNECTED) return "WiFi Offline!";

  HTTPClient http;
  http.setTimeout(15000);
  http.begin(OLLAMA_URL);
  http.addHeader("Content-Type", "application/json");

  StaticJsonDocument<512> doc;
  doc["model"] = "qwen2.5:3b-instruct";
  doc["system"] = "You are a brutally honest, sarcastic, edgy Magic 8-Ball. Rules: Answer in 1 short sentence ONLY (Maximum 10 words). Be witty, spicy, unhinged, or roast-heavy. NEVER add quotes or emojis.";
  doc["prompt"] = "Give a random fortune or answer.";
  doc["stream"] = false;

  JsonObject options = doc.createNestedObject("options");
  options["temperature"] = 0.9;
  options["repeat_penalty"] = 1.18;
  options["num_predict"] = 25;

  String jsonPayload;
  serializeJson(doc, jsonPayload);

  int httpCode = http.POST(jsonPayload);

  Serial.print("Ollama HTTP Code: ");
  Serial.println(httpCode);

  String responseText = "Spirits silent...";

  if (httpCode == 200) {
    String response = http.getString();
    StaticJsonDocument<1024> responseDoc;
    deserializeJson(responseDoc, response);
    responseText = responseDoc["response"].as<String>();
    responseText.trim();
  } else {
    Serial.printf("HTTP Error: %s\n", http.errorToString(httpCode).c_str());
  }

  http.end();
  delay(100);
  return responseText;
}

// -----------------------------------------------------------------------------
// HARDWARE CONTROLS
// -----------------------------------------------------------------------------
void checkTouchToggle() {
  bool currentTouch = digitalRead(TOUCH_PIN);
  if (currentTouch == HIGH && lastTouchState == LOW) {
    systemArmed = !systemArmed;
    renderScreen();
    delay(300);
  }
  lastTouchState = currentTouch;
}

bool checkShake() {
  float x = ax / 16384.0;
  float y = ay / 16384.0;
  float z = az / 16384.0;
  return (sqrt(x * x + y * y + z * z) > SHAKE_THRESHOLD);
}

void initMPU() {
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x6B);
  Wire.write(0x00);
  Wire.endTransmission();
}

void readMPU() {
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x3B);
  Wire.endTransmission(false);
  Wire.requestFrom(MPU_ADDR, 6);
  if (Wire.available() >= 6) {
    ax = Wire.read() << 8 | Wire.read();
    ay = Wire.read() << 8 | Wire.read();
    az = Wire.read() << 8 | Wire.read();
  }
}
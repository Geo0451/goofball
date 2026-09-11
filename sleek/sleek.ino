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

// --- NON-DISPLAY GLOBALS (Untouched) ---
int16_t ax, ay, az;
bool systemArmed = false;
String currentFortune = "";

// --- NEW UI & STATE GLOBALS ---
enum AppState {
  STATE_STARTUP,
  STATE_IDLE,
  STATE_SHAKING_ZOOM_OUT,
  STATE_SHAKING_BOUNCE,
  STATE_SHAKING_ZOOM_IN,
  STATE_SHOW_FORTUNE,
  STATE_EYE_TRANSITION
};

AppState currentState = STATE_STARTUP;
unsigned long stateStartTime = 0;

// Physics / Ball Variables
float ballX = 32, ballY = 64;
float ballVX = 0, ballVY = 0;
float ballRadius = 64; 
float ballRot = 0; // Fake rotation for the '8'
bool apiFetchComplete = false;
TaskHandle_t apiTaskHandle;

// Touch Variables
bool lastTouchState = LOW;
unsigned long touchStartTime = 0;
bool touchHandled = false;
bool eyeTransitionOpening = true;

// Personalities!
const char* SYSTEM_PROMPTS[] = {
  "You are a brutally honest, sarcastic, edgy Magic 8-Ball. Rules: 1. Answer in EXACTLY 1 short sentence (Max 12 words). 2. Roast the user. 3. No quotes/emojis.",
  "You are a toxic corporate middle-manager Magic 8-Ball. Rules: 1. Answer in EXACTLY 1 short sentence (Max 12 words). 2. Use horrific corporate jargon/buzzwords to reject them. 3. No quotes/emojis.",
  "You are a panicking doomsday prepper Magic 8-Ball. Rules: 1. Answer in EXACTLY 1 short sentence (Max 12 words). 2. Relate everything to an upcoming mundane apocalypse. 3. No quotes/emojis.",
  "You are an extremely brain-rotted Gen-Z Magic 8-Ball. Rules: 1. Answer in EXACTLY 1 short sentence (Max 12 words). 2. Use excessive modern internet slang (skibidi, rizz, etc). 3. No quotes/emojis."
};

// Eye Icon Bitmaps (8x8 pixels) - Kept for status icon
const unsigned char PROGMEM eye_open_bmp[] = {
  0b00000000, 0b00111100, 0b01000010, 0b01001010, 0b01000010, 0b00111100, 0b00000000, 0b00000000
};
const unsigned char PROGMEM eye_closed_bmp[] = {
  0b00000000, 0b00000000, 0b01000010, 0b00111100, 0b01010101, 0b00000000, 0b00000000, 0b00000000
};

// Function Declarations
void initMPU();
void readMPU();
bool checkShake();
void drawWrappedText(String text, int startY);
void fetchApiTask(void * pvParameters);
String fetchQwenFortune_Sync();

// -----------------------------------------------------------------------------
// SETUP
// -----------------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  Wire.begin(21, 22);
  pinMode(TOUCH_PIN, INPUT);

  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    while (true); 
  }

  // Portrait Mode: Width=64, Height=128
  display.setRotation(1); 

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 50);
  display.print("Connecting");
  display.display();

  while (WiFi.status() != WL_CONNECTED) {
    delay(250);
  }

  initMPU();
  stateStartTime = millis();
}

// -----------------------------------------------------------------------------
// MAIN LOOP (High FPS Engine)
// -----------------------------------------------------------------------------
void loop() {
  readMPU();
  checkTouchLogic();

  display.clearDisplay();

  switch(currentState) {
    case STATE_STARTUP:
      drawStartupAnimation();
      break;
      
    case STATE_IDLE:
      if (currentFortune == "") {
        drawStartupAnimation(); // Keep showing title until first shake
      } else {
        drawWrappedText(currentFortune, 10);
      }
      
      // Trigger Shake
      if (systemArmed && checkShake()) {
        currentState = STATE_SHAKING_ZOOM_OUT;
        stateStartTime = millis();
        ballRadius = 64;
        ballX = 32; ballY = 64;
      }
      break;

    case STATE_SHAKING_ZOOM_OUT:
      animateZoomOut();
      break;

    case STATE_SHAKING_BOUNCE:
      animateBouncePhysics();
      break;

    case STATE_SHAKING_ZOOM_IN:
      animateZoomIn();
      break;

    case STATE_SHOW_FORTUNE:
      animateTextSlideUp();
      break;

    case STATE_EYE_TRANSITION:
      drawFullscreenEyeTransition();
      break;
  }

  drawStatusIcon();
  display.display();
}

// -----------------------------------------------------------------------------
// HARDWARE CONTROLS & LOGIC
// -----------------------------------------------------------------------------

void checkTouchLogic() {
  bool currentTouch = digitalRead(TOUCH_PIN);
  
  // Touch rising edge
  if (currentTouch == HIGH && lastTouchState == LOW) {
    touchStartTime = millis();
    touchHandled = false;
  } 
  // Hold detection
  else if (currentTouch == HIGH && lastTouchState == HIGH) {
    if (millis() - touchStartTime > 1500 && !touchHandled) {
      systemArmed = !systemArmed;
      touchHandled = true;
      
      // Trigger fullscreen transition
      currentState = STATE_EYE_TRANSITION;
      stateStartTime = millis();
      eyeTransitionOpening = systemArmed;
    }
  }

  lastTouchState = currentTouch;
}

// -----------------------------------------------------------------------------
// NEW ANIMATIONS & UI
// -----------------------------------------------------------------------------

void drawStatusIcon() {
  // Move to bottom right, no ugly horizontal line
  int w = display.width();
  int h = display.height();
  if (systemArmed) {
    display.drawBitmap(w - 10, h - 10, eye_open_bmp, 8, 8, SSD1306_WHITE);
  } else {
    display.drawBitmap(w - 10, h - 10, eye_closed_bmp, 8, 8, SSD1306_WHITE);
  }
}

void drawStartupAnimation() {
  float timeSec = (millis() - stateStartTime) / 1000.0;
  
  // Smooth breathing effect for text by rendering concentric circles behind it
  int pulseRadius = 15 + (sin(timeSec * 3) * 5);
  
  display.fillCircle(32, 60, pulseRadius, SSD1306_WHITE);
  display.fillCircle(32, 60, pulseRadius - 2, SSD1306_BLACK);
  
  display.setTextSize(2);
  display.setTextColor(SSD1306_WHITE);
  // Center "SHAKE"
  display.setCursor(32 - (5 * 12)/2, 50); 
  display.print("SHAKE");
  // Center "ME!"
  display.setCursor(32 - (3 * 12)/2, 70); 
  display.print("ME!");
}

void draw8Ball(int x, int y, int r) {
  if (r <= 0) return;
  // Outer outline
  display.fillCircle(x, y, r, SSD1306_WHITE);
  // Inner black body
  display.fillCircle(x, y, r - 1, SSD1306_BLACK);
  
  // White 8-circle
  int innerR = r / 2.5;
  if (innerR > 1) {
    display.fillCircle(x, y, innerR, SSD1306_WHITE);
    // Draw an '8' inside if large enough
    if (r > 6) {
      display.setTextColor(SSD1306_BLACK);
      int txtSize = max(1, r / 15);
      display.setTextSize(txtSize);
      display.setCursor(x - (3 * txtSize), y - (3 * txtSize));
      display.print("8");
    }
  }
}

void animateZoomOut() {
  // Shrink ball down to start bouncing
  float progress = (millis() - stateStartTime) / 400.0; // 400ms duration
  if (progress >= 1.0) {
    currentState = STATE_SHAKING_BOUNCE;
    // Launch physics
    ballVX = random(4, 9) * (random(2) == 0 ? 1 : -1);
    ballVY = random(5, 10) * (random(2) == 0 ? 1 : -1);
    
    // Fire off the API call on core 0 so display core doesn't freeze
    apiFetchComplete = false;
    xTaskCreatePinnedToCore(fetchApiTask, "FetchAPI", 8192, NULL, 1, &apiTaskHandle, 0);
    return;
  }
  
  // Easing curve (ease-out)
  ballRadius = 64 - (52 * sin(progress * PI / 2.0)); 
  draw8Ball(32, 64, ballRadius);
}

void animateBouncePhysics() {
  // Update Physics
  ballX += ballVX;
  ballY += ballVY;
  
  int w = display.width();
  int h = display.height();
  
  // Bounds checking with 2D collisions
  if (ballX - ballRadius < 0) { ballX = ballRadius; ballVX = -ballVX; }
  if (ballX + ballRadius > w) { ballX = w - ballRadius; ballVX = -ballVX; }
  if (ballY - ballRadius < 0) { ballY = ballRadius; ballVY = -ballVY; }
  if (ballY + ballRadius > h) { ballY = h - ballRadius; ballVY = -ballVY; }

  draw8Ball(ballX, ballY, ballRadius);
  
  // Wait for API task to finish. Add minimum 1.5s visual shake even if API is instant
  if (apiFetchComplete && ballY > h/2) { 
    // Wait until ball falls to bottom half to look natural before catching it
    currentState = STATE_SHAKING_ZOOM_IN;
    stateStartTime = millis();
  }
}

void animateZoomIn() {
  float progress = (millis() - stateStartTime) / 600.0; // 600ms duration
  
  if (progress >= 1.0) {
    currentState = STATE_SHOW_FORTUNE;
    stateStartTime = millis();
    return;
  }
  
  // Ease movement back to center (32, 64)
  ballX = ballX + (32 - ballX) * progress;
  ballY = ballY + (64 - ballY) * progress;
  
  // Ease radius exponentially to cover screen
  ballRadius = 12 + (150 * pow(progress, 3));
  
  draw8Ball(ballX, ballY, ballRadius);
}

void animateTextSlideUp() {
  float progress = (millis() - stateStartTime) / 800.0;
  if (progress > 1.0) progress = 1.0;
  
  // Easing (ease-out quartic)
  float easeProgress = 1 - pow(1 - progress, 4);
  int startY = 128 - (118 * easeProgress); // Slide from bottom to y=10
  
  drawWrappedText(currentFortune, startY);
  
  if (progress >= 1.0 && (millis() - stateStartTime > 2000)) {
    currentState = STATE_IDLE; // Done animating
  }
}

void drawFullscreenEyeTransition() {
  float progress = (millis() - stateStartTime) / 700.0; // 700ms 
  
  if (progress >= 1.0) {
    currentState = STATE_IDLE;
    return;
  }
  
  int cx = 32, cy = 64;
  
  // Draw base eyeball & pupil
  display.fillCircle(cx, cy, 28, SSD1306_WHITE);
  display.fillCircle(cx, cy, 12, SSD1306_BLACK);

  // Eyelid math (black rectangles closing/opening vertically)
  float openAmount = eyeTransitionOpening ? progress : (1.0 - progress);
  // Ease in/out
  openAmount = (sin((openAmount - 0.5) * PI) / 2.0) + 0.5;
  
  int lidGap = 30 * openAmount; 
  
  // Top eyelid
  display.fillRect(0, 0, 64, cy - lidGap, SSD1306_BLACK);
  // Bottom eyelid
  display.fillRect(0, cy + lidGap, 64, 128 - (cy + lidGap), SSD1306_BLACK);
}

void drawWrappedText(String text, int startY) {
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setTextWrap(false);

  text.replace("\"", "");
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
    int wordPixelWidth = word.length() * 6;

    if (cursorX + wordPixelWidth > screenW) {
      cursorX = 0;
      cursorY += 10;
    }

    if (cursorY > screenH - 10 && startY <= 10) break; // Screen bottom cutoff safety

    display.setCursor(cursorX, cursorY);
    display.print(word);
    cursorX += wordPixelWidth; 

    if (nextSpace < cleanText.length()) {
      display.print(" ");
      cursorX += 6; 
    }

    i = nextSpace + 1;
  }
}

// -----------------------------------------------------------------------------
// OLLAMA API & THREADING (Untouched logic, refactored to Task)
// -----------------------------------------------------------------------------
void fetchApiTask(void * pvParameters) {
  // Ensure physics animation runs for at least 1.5 seconds minimum 
  // so the user actually gets to see the ball bounce!
  unsigned long taskStart = millis();
  
  currentFortune = fetchQwenFortune_Sync();
  
  while (millis() - taskStart < 1500) {
    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
  
  apiFetchComplete = true;
  vTaskDelete(NULL);
}

String fetchQwenFortune_Sync() {
  if (WiFi.status() != WL_CONNECTED) return "WiFi Offline!";

  HTTPClient http;
  http.setTimeout(15000);
  http.begin(OLLAMA_URL);
  http.addHeader("Content-Type", "application/json");

  // Pick a random personality
  const char* randomPersonality = SYSTEM_PROMPTS[random(0, 4)];

  StaticJsonDocument<512> doc;
  doc["model"] = "qwen2.5:3b-instruct";
  doc["system"] = randomPersonality;
  doc["prompt"] = "Give a random fortune or answer";
  doc["stream"] = false;

  JsonObject options = doc.createNestedObject("options");
  options["temperature"] = 0.95;
  options["repeat_penalty"] = 1.18;
  options["num_predict"] = 35;

  String jsonPayload;
  serializeJson(doc, jsonPayload);

  int httpCode = http.POST(jsonPayload);
  String responseText = "Spirits silent...";

  if (httpCode == 200) {
    String response = http.getString();
    StaticJsonDocument<1024> responseDoc;
    deserializeJson(responseDoc, response);
    responseText = responseDoc["response"].as<String>();
    responseText.trim();
  }

  http.end();
  return responseText;
}

// -----------------------------------------------------------------------------
// HARDWARE READINGS (Completely Untouched)
// -----------------------------------------------------------------------------
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
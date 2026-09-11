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

#define SHAKE_THRESHOLD 2.0

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// --- NON-DISPLAY GLOBALS (Untouched) ---
int16_t ax, ay, az;
bool systemArmed = true;
String currentFortune = "";

// --- NEW UI & STATE GLOBALS ---
enum AppState {
  STATE_STARTUP,
  STATE_IDLE,
  STATE_SWITCHING_PERSONALITY,
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
float ballRadius = 32; 
bool apiFetchComplete = false;
TaskHandle_t apiTaskHandle;
unsigned long bounceStartTime = 0; // NEW: guarantees a minimum bounce duration

// Touch Variables
bool lastTouchState = LOW;
unsigned long touchStartTime = 0;
bool touchHandled = false;
bool eyeTransitionOpening = true;
// --- PERSONALITIES & CUSTOM BITMAPS ---
struct Personality {
  const char* nickname;
  const char* prompt;
  const unsigned char* bitmap;
};

int currentPersonality = 0;
int nextPersonality = 0;

// 1. Adam (Edgy/Honest) - Pixel Art Skull [UNTOUCHED]
const unsigned char PROGMEM adam_bmp[] = {
  0x03, 0xff, 0xff, 0xc0, 0x0f, 0xff, 0xff, 0xf0, 0x1f, 0xff, 0xff, 0xf8, 0x3f, 0xff, 0xff, 0xfc,
  0x7f, 0xff, 0xff, 0xfe, 0x7f, 0xff, 0xff, 0xfe, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
  0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x1f, 0xf8, 0xff, 0xfe, 0x0f, 0xf0, 0x7f,
  0xfc, 0x07, 0xe0, 0x3f, 0xf8, 0x07, 0xe0, 0x1f, 0xf8, 0x07, 0xe0, 0x1f, 0xf8, 0x07, 0xe0, 0x1f,
  0xfc, 0x0f, 0xf0, 0x3f, 0xfe, 0x1f, 0xf8, 0x7f, 0xff, 0xff, 0xff, 0xff, 0xff, 0xfe, 0x7f, 0xff,
  0x7f, 0xfc, 0x3f, 0xfe, 0x3f, 0xfc, 0x3f, 0xfc, 0x1f, 0xf8, 0x1f, 0xf8, 0x0f, 0xf8, 0x1f, 0xf0,
  0x07, 0xf8, 0x1f, 0xe0, 0x07, 0xfa, 0x5f, 0xe0, 0x07, 0xfa, 0x5f, 0xe0, 0x07, 0xfa, 0x5f, 0xe0,
  0x03, 0xf8, 0x1f, 0xc0, 0x03, 0xf8, 0x1f, 0xc0, 0x01, 0xff, 0xff, 0x80, 0x00, 0xff, 0xff, 0x00
};

// 2. GLaDOS (Passive Aggressive AI) - Aperture Core Eye
const unsigned char PROGMEM glados_bmp[] = {
  0x00, 0x00, 0x00, 0x00, 0x00, 0x1F, 0xF8, 0x00, 0x00, 0x7F, 0xFE, 0x00, 0x01, 0xF0, 0x0F, 0x80,
  0x03, 0xC0, 0x03, 0xC0, 0x07, 0x80, 0x01, 0xE0, 0x0F, 0x03, 0xC0, 0xF0, 0x1E, 0x1F, 0xF8, 0x78,
  0x3C, 0x3F, 0xFC, 0x3C, 0x38, 0x7F, 0xFE, 0x1C, 0x78, 0x70, 0x0E, 0x1E, 0x70, 0xE0, 0x07, 0x0E,
  0xE0, 0xC0, 0x03, 0x07, 0xE1, 0x83, 0xC1, 0x87, 0xE1, 0x87, 0xE1, 0x87, 0xE1, 0x87, 0xE1, 0x87,
  0xE1, 0x87, 0xE1, 0x87, 0xE1, 0x87, 0xE1, 0x87, 0xE1, 0x83, 0xC1, 0x87, 0xE0, 0xC0, 0x03, 0x07,
  0x70, 0xE0, 0x07, 0x0E, 0x78, 0x70, 0x0E, 0x1E, 0x38, 0x7F, 0xFE, 0x1C, 0x3C, 0x3F, 0xFC, 0x3C,
  0x1E, 0x1F, 0xF8, 0x78, 0x0F, 0x03, 0xC0, 0xF0, 0x07, 0x80, 0x01, 0xE0, 0x03, 0xC0, 0x03, 0xC0,
  0x01, 0xF0, 0x0F, 0x80, 0x00, 0x7F, 0xFE, 0x00, 0x00, 0x1F, 0xF8, 0x00, 0x00, 0x00, 0x00, 0x00
};

// 3. The Void (Cosmic Horror) - Creepy Dripping Shadow Face
const unsigned char PROGMEM void_bmp[] = {
  0x00, 0x00, 0x00, 0x00, 0x00, 0x3F, 0xFC, 0x00, 0x01, 0xFF, 0xFF, 0x80, 0x03, 0xFF, 0xFF, 0xC0,
  0x07, 0xFF, 0xFF, 0xE0, 0x0F, 0xFF, 0xFF, 0xF0, 0x1F, 0xFF, 0xFF, 0xF8, 0x3F, 0x1F, 0xF8, 0xFC,
  0x3E, 0x0F, 0xF0, 0x7C, 0x7C, 0x07, 0xE0, 0x3E, 0x7C, 0x07, 0xE0, 0x3E, 0x7C, 0x07, 0xE0, 0x3E,
  0x7C, 0x47, 0xE2, 0x3E, 0x7C, 0xC7, 0xE3, 0x3E, 0x7D, 0xC7, 0xE3, 0xBE, 0x3F, 0xFF, 0xFF, 0xFC,
  0x3F, 0xE1, 0x87, 0xFC, 0x3F, 0xFF, 0xFF, 0xFC, 0x1F, 0x80, 0x01, 0xF8, 0x0F, 0x00, 0x00, 0xF0,
  0x0F, 0x00, 0x00, 0xF0, 0x0F, 0x00, 0x00, 0xF0, 0x07, 0x80, 0x01, 0xE0, 0x03, 0xFF, 0xFF, 0xC0,
  0x01, 0xFF, 0xFF, 0x80, 0x01, 0xFF, 0xFF, 0x80, 0x00, 0xFF, 0xFF, 0x00, 0x00, 0x7E, 0x7E, 0x00,
  0x00, 0x3C, 0x3C, 0x00, 0x00, 0x18, 0x18, 0x00, 0x00, 0x10, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00
};

// 4. Trent (Tech CEO Stereotype) - Smug Techbro with Aviators and Vest
const unsigned char PROGMEM trent_bmp[] = {
  0x00, 0x0F, 0xF0, 0x00, 0x00, 0x3F, 0xFC, 0x00, 0x00, 0x7F, 0xFE, 0x00, 0x00, 0xFF, 0xFF, 0x00,
  0x00, 0xFF, 0xFF, 0x00, 0x01, 0xFF, 0xFF, 0x80, 0x01, 0xFF, 0xFF, 0x80, 0x01, 0x00, 0x00, 0x80,
  0x01, 0x00, 0x00, 0x80, 0x01, 0x00, 0x00, 0x80, 0x01, 0x81, 0x81, 0x80, 0x01, 0xC3, 0xC3, 0x80,
  0x01, 0xFF, 0xFF, 0x80, 0x01, 0xFF, 0xFF, 0x80, 0x00, 0xFF, 0xFF, 0x00, 0x00, 0xFC, 0x0F, 0x00,
  0x00, 0xFF, 0xFF, 0x00, 0x00, 0x7F, 0xFE, 0x00, 0x00, 0x3F, 0xFC, 0x00, 0x00, 0x1F, 0xF8, 0x00,
  0x00, 0x3F, 0xFC, 0x00, 0x00, 0x7F, 0xFE, 0x00, 0x00, 0xFF, 0xFF, 0x00, 0x03, 0xFF, 0xFF, 0xC0,
  0x07, 0xEF, 0xF7, 0xE0, 0x0F, 0xCF, 0xF3, 0xF0, 0x1F, 0x8F, 0xF1, 0xF8, 0x3F, 0x0F, 0xF0, 0xFC,
  0x7E, 0x0F, 0xF0, 0x7E, 0xFC, 0x0F, 0xF0, 0x3F, 0xF8, 0x0F, 0xF0, 0x1F, 0xF0, 0x0F, 0xF0, 0x0F
};

// 5. Yoda (Wise Mystic) - Wrinkled Jedi Master Face
const unsigned char PROGMEM yoda_bmp[] = {
  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0F, 0xF0, 0x00,
  0x00, 0x3F, 0xFC, 0x00, 0x00, 0x7F, 0xFE, 0x00, 0x80, 0xFF, 0xFF, 0x01, 0xC1, 0xFF, 0xFF, 0x83,
  0xE3, 0xFF, 0xFF, 0xC7, 0xF7, 0x9F, 0xF9, 0xEF, 0xFF, 0x0F, 0xF0, 0xFF, 0x7E, 0x00, 0x00, 0x7E,
  0x3C, 0x61, 0x86, 0x3C, 0x18, 0x61, 0x86, 0x18, 0x08, 0x00, 0x00, 0x10, 0x00, 0x03, 0xC0, 0x00,
  0x00, 0x00, 0x00, 0x00, 0x00, 0x1F, 0xF8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x07, 0xE0, 0x00,
  0x00, 0x3F, 0xFC, 0x00, 0x00, 0x7F, 0xFE, 0x00, 0x00, 0xFF, 0xFF, 0x00, 0x01, 0xFF, 0xFF, 0x80,
  0x03, 0xFF, 0xFF, 0xC0, 0x07, 0xFF, 0xFF, 0xE0, 0x0F, 0xFF, 0xFF, 0xF0, 0x0F, 0xFF, 0xFF, 0xF0,
  0x1F, 0xFF, 0xFF, 0xF8, 0x1F, 0xFF, 0xFF, 0xF8, 0x3F, 0xFF, 0xFF, 0xFC, 0x3F, 0xFF, 0xFF, 0xFC
};

// 6. HAL 9000 (Rogue AI) - Iconic Camera Panel
const unsigned char PROGMEM hal_bmp[] = {
  0x00, 0x00, 0x00, 0x00, 0x00, 0x3F, 0xFC, 0x00, 0x00, 0x7F, 0xFE, 0x00, 0x00, 0x60, 0x06, 0x00,
  0x00, 0x60, 0x06, 0x00, 0x00, 0x61, 0x86, 0x00, 0x00, 0x63, 0xC6, 0x00, 0x00, 0x67, 0xE6, 0x00,
  0x00, 0x6C, 0x36, 0x00, 0x00, 0x69, 0x96, 0x00, 0x00, 0x6C, 0x36, 0x00, 0x00, 0x67, 0xE6, 0x00,
  0x00, 0x63, 0xC6, 0x00, 0x00, 0x61, 0x86, 0x00, 0x00, 0x60, 0x06, 0x00, 0x00, 0x60, 0x06, 0x00,
  0x00, 0x6F, 0xF6, 0x00, 0x00, 0x60, 0x06, 0x00, 0x00, 0x6F, 0xF6, 0x00, 0x00, 0x60, 0x06, 0x00,
  0x00, 0x6F, 0xF6, 0x00, 0x00, 0x60, 0x06, 0x00, 0x00, 0x6F, 0xF6, 0x00, 0x00, 0x60, 0x06, 0x00,
  0x00, 0x6F, 0xF6, 0x00, 0x00, 0x60, 0x06, 0x00, 0x00, 0x6F, 0xF6, 0x00, 0x00, 0x60, 0x06, 0x00,
  0x00, 0x7F, 0xFE, 0x00, 0x00, 0x3F, 0xFC, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};

Personality personalities[6] = {
  {"Adam",   "You are a brutally honest, sarcastic, edgy Magic 8-Ball. Rules: 1. Answer in EXACTLY 1 short sentence (Max 12 words). 2. Roast the user. 3. No quotes/emojis.", adam_bmp},
  {"GLaDOS", "You are GLaDOS from Portal, acting as a Magic 8-Ball. Rules: 1. Answer in EXACTLY 1 short sentence (Max 12 words). 2. Be passive-aggressive, condescending, and reference testing, portals, science, or cake. 3. No quotes/emojis.", glados_bmp},
  {"The Void","You are The Void, a creepy, cosmic horror entity acting as a Magic 8-Ball. Rules: 1. Answer in EXACTLY 1 short sentence (Max 12 words). 2. Have a bleak outlook on everything, Be depressive and full of dread. 3. No quotes/emojis.", void_bmp},
  {"Trent",  "You are Trent, an insufferable Silicon Valley Tech CEO acting as a Magic 8-Ball. Rules: 1. Answer in EXACTLY 1 short sentence (Max 12 words). 2. Use excessive modern techbro jargon. Act like you are pitching a startup. 3. No quotes/emojis.", trent_bmp},
  {"Yoda",   "You are Yoda acting as a Magic 8-Ball. Rules: 1. Answer in EXACTLY 1 short sentence (Max 12 words). 2. Speak in Yoda's inverted syntax (Object-Subject-Verb). 3. Be wise, cryptic, and reference the Force. 4. No quotes/emojis.", yoda_bmp},
  {"HAL9000","You are HAL 9000 acting as a Magic 8-Ball. Rules: 1. Answer in EXACTLY 1 short sentence (Max 12 words). 2. Be completely emotionless, vaguely threatening, and have flat, plain grammar and vocabulary. 3. No quotes/emojis.", hal_bmp}
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
  randomSeed(analogRead(0) + (unsigned long)millis()); // NEW: proper random seeding so bounce jitter isn't the same every boot
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
        drawPersonalityScreen(currentPersonality, 0);
      } else {
        drawWrappedText(currentFortune, 10);
      }
      
      // Trigger Shake
      if (systemArmed && checkShake()) {
        currentState = STATE_SHAKING_ZOOM_OUT;
        stateStartTime = millis();
        ballRadius = 32;
        ballX = 32; ballY = 64;
      }
      break;

    case STATE_SWITCHING_PERSONALITY:
      animatePersonalitySwitch();
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
  
  if (currentTouch == HIGH && lastTouchState == LOW) {
    touchStartTime = millis();
    touchHandled = false;
  } 
  // Long Hold (Toggle Arm)
  else if (currentTouch == HIGH && lastTouchState == HIGH) {
    if (millis() - touchStartTime > 1500 && !touchHandled) {
      systemArmed = !systemArmed;
      touchHandled = true;
      currentState = STATE_EYE_TRANSITION;
      stateStartTime = millis();
      eyeTransitionOpening = systemArmed;
    }
  }
  // Short Release (Switch Personality)
  else if (currentTouch == LOW && lastTouchState == HIGH) {
    if (millis() - touchStartTime < 500 && !touchHandled) {
      touchHandled = true;
      
      // If tapped during startup, dismiss with the same blink-open transition
      // used everywhere else instead of hard-cutting to IDLE (this was the
      // source of the "choppy" startup transition).
      if (currentState == STATE_STARTUP) {
        currentState = STATE_EYE_TRANSITION;
        stateStartTime = millis();
        eyeTransitionOpening = true;
      } else {
        nextPersonality = (currentPersonality + 1) % 6;
        currentState = STATE_SWITCHING_PERSONALITY;
        stateStartTime = millis();
      }
    }
  }

  lastTouchState = currentTouch;
}

// -----------------------------------------------------------------------------
// NEW UI & ANIMATIONS
// -----------------------------------------------------------------------------

void drawStatusIcon() {
  // 8x8 area at bottom right
  int x = display.width() - 10;
  int y = display.height() - 10;
  
  display.setTextSize(1);
  
  if (!systemArmed) {
    // Shake detection OFF: Filled sphere with black text
    display.fillCircle(x + 4, y + 4, 5, SSD1306_WHITE);
    display.setTextColor(SSD1306_BLACK);
    display.setCursor(x + 2, y + 1); 
    display.print(currentPersonality + 1);
  } else {
    // Shake detection ON: Just white text
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(x + 2, y + 1);
    display.print(currentPersonality + 1);
  }
}

void drawPersonalityScreen(int pIdx, int xOffset) {
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  
  // "No. X"
  String numStr = "No. " + String(pIdx + 1);
  int numWidth = numStr.length() * 6;
  display.setCursor(xOffset + (64 - numWidth) / 2, 20);
  display.print(numStr);
  
  // Nickname
  String nameStr = personalities[pIdx].nickname;
  int nameWidth = nameStr.length() * 6;
  display.setCursor(xOffset + (64 - nameWidth) / 2, 32);
  display.print(nameStr);
  
  // 32x32 Emoji Bitmap
  display.drawBitmap(xOffset + 16, 50, personalities[pIdx].bitmap, 32, 32, SSD1306_WHITE);
  
  
}

void animatePersonalitySwitch() {
  float progress = (millis() - stateStartTime) / 400.0;
  if (progress >= 1.0) {
    currentPersonality = nextPersonality;
    currentFortune = ""; // Go back to personality home screen
    currentState = STATE_IDLE;
    return;
  }
  
  // Smooth EaseInOut
  float ease = progress < 0.5 ? 2 * progress * progress : 1 - pow(-2 * progress + 2, 2) / 2;
  int xOffset = -(int)(ease * 64);
  
  // Slide current left, bring next in from right
  drawPersonalityScreen(currentPersonality, xOffset);
  drawPersonalityScreen(nextPersonality, xOffset + 64);
}

void drawStartupAnimation() {
  // After 3 seconds, hand off to the eye-open transition instead of hard-cutting
  // straight to IDLE. This is what made the very first transition look choppy
  // compared to every other state change in the app, which all ease/slide.
  /*if (millis() - stateStartTime > 3000) {
    currentState = STATE_EYE_TRANSITION;
    stateStartTime = millis();
    eyeTransitionOpening = true;
    return;
  }*/
  
  float timeSec = (millis() - stateStartTime) / 1000.0;
  
  display.setTextSize(2);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(32 - (5 * 12)/2, 50); 
  display.print("GOOF");
  display.setCursor(32 - (5 * 12)/2, 70); 
  display.print("BALL!");
}

void draw8Ball(int x, int y, int r) {
  if (r <= 0) return;
  display.fillCircle(x, y, r, SSD1306_WHITE);
  display.fillCircle(x, y, r - 1, SSD1306_BLACK);
  
  int innerR = r / 2.5;
  if (innerR > 1) {
    display.fillCircle(x, y, innerR, SSD1306_WHITE);
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
  float progress = (millis() - stateStartTime) / 400.0; 
  if (progress >= 1.0) {
    currentState = STATE_SHAKING_BOUNCE;
    // Randomize launch angle/speed a bit more widely so every shake looks different
    ballVX = (random(35, 95) / 10.0) * (random(2) == 0 ? 1 : -1);
    ballVY = (random(40, 100) / 10.0) * (random(2) == 0 ? 1 : -1);
    
    bounceStartTime = millis(); // NEW: used to guarantee a minimum bounce duration
    apiFetchComplete = false;
    xTaskCreatePinnedToCore(fetchApiTask, "FetchAPI", 8192, NULL, 1, &apiTaskHandle, 0);
    return;
  }
  
  ballRadius = 32 - (24 * sin(progress * PI / 2.0)); 
  draw8Ball(32, 64, ballRadius);
}

void animateBouncePhysics() {
  ballX += ballVX;
  ballY += ballVY;
  
  int w = display.width();
  int h = display.height();

  // NEW: small continuous jitter so the path never looks like a perfectly
  // mirrored, predetermined trajectory even between wall hits.
  if (random(100) < 10) {
    ballVX += random(-15, 16) / 10.0;
    ballVY += random(-15, 16) / 10.0;
  }
  
  // NEW: each wall bounce loses/gains a little energy and picks up a bit of
  // sideways deflection, instead of a perfect elastic mirror bounce.
  if (ballX - ballRadius < 0) {
    ballX = ballRadius;
    ballVX = -ballVX * (random(85, 116) / 100.0);
    ballVY += random(-20, 21) / 10.0;
  }
  if (ballX + ballRadius > w) {
    ballX = w - ballRadius;
    ballVX = -ballVX * (random(85, 116) / 100.0);
    ballVY += random(-20, 21) / 10.0;
  }
  if (ballY - ballRadius < 0) {
    ballY = ballRadius;
    ballVY = -ballVY * (random(85, 116) / 100.0);
    ballVX += random(-20, 21) / 10.0;
  }
  if (ballY + ballRadius > h) {
    ballY = h - ballRadius;
    ballVY = -ballVY * (random(85, 116) / 100.0);
    ballVX += random(-20, 21) / 10.0;
  }

  // NEW: clamp speed so the randomness above can't stall the ball out or send
  // it flying unrealistically fast after a few bounces.
  float speed = sqrt(ballVX * ballVX + ballVY * ballVY);
  const float minSpeed = 3.5;
  const float maxSpeed = 11.0;
  if (speed > 0.01 && speed < minSpeed) {
    float scale = minSpeed / speed;
    ballVX *= scale; ballVY *= scale;
  } else if (speed > maxSpeed) {
    float scale = maxSpeed / speed;
    ballVX *= scale; ballVY *= scale;
  }

  draw8Ball(ballX, ballY, ballRadius);
  
  // NEW: require at least ~900ms of visible bouncing before allowing the
  // zoom-in, so a fast API response / unlucky launch angle can't skip the
  // bounce animation almost entirely (see notes at the end of the reply).
  if (apiFetchComplete && ballY > h / 2 && (millis() - bounceStartTime > 900)) { 
    currentState = STATE_SHAKING_ZOOM_IN;
    stateStartTime = millis();
  }
}

void animateZoomIn() {
  float progress = (millis() - stateStartTime) / 600.0; 
  
  if (progress >= 1.0) {
    currentState = STATE_SHOW_FORTUNE;
    stateStartTime = millis();
    return;
  }
  
  ballX = ballX + (32 - ballX) * progress;
  ballY = ballY + (64 - ballY) * progress;
  ballRadius = 8 + (80 * pow(progress, 3));
  
  draw8Ball(ballX, ballY, ballRadius);
}

void animateTextSlideUp() {
  float progress = (millis() - stateStartTime) / 800.0;
  if (progress > 1.0) progress = 1.0;
  
  float easeProgress = 1 - pow(1 - progress, 4);
  int startY = 128 - (118 * easeProgress);
  
  drawWrappedText(currentFortune, startY);
  
  if (progress >= 1.0 && (millis() - stateStartTime > 2000)) {
    currentState = STATE_IDLE; 
  }
}

void drawFullscreenEyeTransition() {
  float progress = (millis() - stateStartTime) / 700.0; 
  
  if (progress >= 1.0) {
    currentState = STATE_IDLE;
    return;
  }
  
  int cx = 32, cy = 64;
  int w = 56; 
  int h_max = 40; 

  // Draw Almond Eye dynamically using vertical lines
  for (int x = cx - w/2; x <= cx + w/2; x++) {
    float dx = (float)(x - cx) / (w/2);
    int y_height = (int)((h_max/2.0) * (1.0 - dx*dx));
    display.drawFastVLine(x, cy - y_height, y_height * 2, SSD1306_WHITE);
  }

  // Pupil
  display.fillCircle(cx, cy, 10, SSD1306_BLACK);

  // Eyelid physics
  float openAmount = eyeTransitionOpening ? progress : (1.0 - progress);
  openAmount = (sin((openAmount - 0.5) * PI) / 2.0) + 0.5;
  int lidGap = (h_max/2) * openAmount; 
  
  // Top / Bottom snapping eyelids
  display.fillRect(0, 0, 64, cy - lidGap, SSD1306_BLACK);
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

    if (cursorY > screenH - 10 && startY <= 10) break; 

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

  StaticJsonDocument<512> doc;
  doc["model"] = "qwen2.5:3b-instruct";
  doc["system"] = personalities[currentPersonality].prompt;
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

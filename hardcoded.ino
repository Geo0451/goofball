#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

#define OLED_ADDR 0x3C
#define MPU_ADDR  0x68
#define TOUCH_PIN 27

// Shake sensitivity threshold (in G-force magnitude). Normal gravity = ~1.0g.
#define SHAKE_THRESHOLD 2.3

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// MPU6050 raw values
int16_t ax, ay, az;

// State management
bool systemArmed = false;
bool lastTouchState = LOW;
String currentFortune = "TOUCH TO ARM";

// Hardcoded Magic 8-Ball Responses (12 Total)
const char* fortunes[] = {
  "It is certain",
  "Without a doubt",
  "Yes definitely",
  "You may rely on it",
  "Reply hazy try again",
  "Ask again later",
  "Better not tell now",
  "Cannot predict now",
  "Don't count on it",
  "My reply is no",
  "My sources say no",
  "Outlook not so good"
};
const int TOTAL_FORTUNES = 12;

// Function declarations
void initMPU();
void readMPU();
void checkTouchToggle();
bool checkShake();
void playShakingAnimation();
void displayCenteredText(String text);

// -----------------------------------------------------------------------------
// SETUP
// -----------------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  Wire.begin(21, 22);
  pinMode(TOUCH_PIN, INPUT);

  // Seed random number generator using noise from unconnected ADC pin
  randomSeed(analogRead(34) + micros());

  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    Serial.println("OLED Initialization Failed");
    while (true);
  }

  initMPU();

  // Initial Welcome Screen
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setTextSize(1);
  displayCenteredText("MAGIC 8-BALL\n\nTouch sensor\nto Arm/Disarm");
  display.display();
}

// -----------------------------------------------------------------------------
// MAIN LOOP
// -----------------------------------------------------------------------------
void loop() {
  checkTouchToggle();
  readMPU();

  // If system is armed, evaluate for shake impulse
  if (systemArmed) {
    if (checkShake()) {
      // 1. Play 8-Ball Shaking Animation
      playShakingAnimation();

      // 2. Select Random Fortune
      int randomIndex = random(0, TOTAL_FORTUNES);
      currentFortune = fortunes[randomIndex];

      // 3. Render Answer Screen
      display.clearDisplay();
      
      // Header status bar
      display.setTextSize(1);
      display.setCursor(0, 0);
      display.print("[ARMED] - SHAKE!");
      display.drawLine(0, 10, 127, 10, SSD1306_WHITE);

      // Display selected fortune inside canvas bounds (Y: 16 to 63)
      display.setCursor(0, 22);
      display.setTextSize(1);
      display.println(currentFortune);
      display.display();

      // Cooldown delay to prevent double triggering
      delay(1500);
    }
  }

  delay(30); // ~30Hz polling loop
}

// -----------------------------------------------------------------------------
// HARDWARE INTERACTION FUNCTIONS
// -----------------------------------------------------------------------------

// Toggle Armed state on TTP223 touch (with edge detection debounce)
void checkTouchToggle() {
  bool currentTouch = digitalRead(TOUCH_PIN);

  if (currentTouch == HIGH && lastTouchState == LOW) {
    systemArmed = !systemArmed; // Toggle state

    display.clearDisplay();
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.print("STATUS CHANGE");
    display.drawLine(0, 10, 127, 10, SSD1306_WHITE);

    display.setTextSize(2);
    display.setCursor(10, 30);
    if (systemArmed) {
      display.print("ARMED!");
    } else {
      display.print("DISARMED");
    }
    display.display();

    delay(800); // UI feedback pause & touch debounce

    // Redraw current idle view
    display.clearDisplay();
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.print(systemArmed ? "[ARMED] Ready..." : "[OFF] Touched");
    display.drawLine(0, 10, 127, 10, SSD1306_WHITE);
    display.setCursor(0, 22);
    display.println(currentFortune);
    display.display();
  }

  lastTouchState = currentTouch;
}

// Calculate 3D acceleration vector magnitude to detect sudden motion
bool checkShake() {
  float x = ax / 16384.0;
  float y = ay / 16384.0;
  float z = az / 16384.0;

  float magnitude = sqrt(x * x + y * y + z * z);
  return (magnitude > SHAKE_THRESHOLD);
}

// -----------------------------------------------------------------------------
// 128x64 GRAPHICS ANIMATION
// -----------------------------------------------------------------------------
void playShakingAnimation() {
  unsigned long startTime = millis();

  while (millis() - startTime < 1500) { // Shake for 1.5 seconds
    display.clearDisplay();

    // Randomize 8-Ball center position within canvas bounds (keeping ball onscreen)
    int radius = 20;
    int ballX = random(radius + 2, SCREEN_WIDTH - radius - 2);
    int ballY = random(radius + 2, SCREEN_HEIGHT - radius - 2);

    // Draw 8-Ball Outer Shell
    display.drawCircle(ballX, ballY, radius, SSD1306_WHITE);

    // Inner White Triangle/Circle
    display.fillCircle(ballX, ballY, 9, SSD1306_WHITE);

    // Inner '8' Symbol (Drawn in inverted black color)
    display.setTextColor(SSD1306_BLACK);
    display.setTextSize(1);
    display.setCursor(ballX - 3, ballY - 3);
    display.print("8");

    // Reset text color back to normal
    display.setTextColor(SSD1306_WHITE);

    // Display "Consulting..." status text along top edge
    display.setCursor(20, 0);
    display.print("Consulting...");

    display.display();
    delay(40); // Control animation frame rate (~25 fps)
  }
}

// Helper to center simple strings
void displayCenteredText(String text) {
  display.setCursor(0, 20);
  display.println(text);
}

// MPU Registers Setup
void initMPU() {
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x6B); // Power Management 1
  Wire.write(0x00); // Wake up MPU
  Wire.endTransmission();
}

// Read raw MPU accelerometer registers
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
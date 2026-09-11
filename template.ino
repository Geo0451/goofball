#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

#define OLED_ADDR 0x3C
#define MPU_ADDR  0x68

#define TOUCH_PIN 27

Adafruit_SSD1306 display(
  SCREEN_WIDTH,
  SCREEN_HEIGHT,
  &Wire,
  -1
);


// MPU6050 raw values
int16_t ax, ay, az;
int16_t gx, gy, gz;


// Dot position
float dotX = 64;
float dotY = 45;


// Play area
const int AREA_TOP = 24;
const int AREA_BOTTOM = 63;
const int AREA_LEFT = 0;
const int AREA_RIGHT = 127;


// Dot sizes
const int NORMAL_RADIUS = 3;
const int TOUCH_RADIUS = 7;


// ------------------------------------------------
// SETUP
// ------------------------------------------------

void setup() {

  Serial.begin(115200);

  Wire.begin(21, 22);

  pinMode(TOUCH_PIN, INPUT);


  // OLED
  if (!display.begin(
        SSD1306_SWITCHCAPVCC,
        OLED_ADDR
      )) {

    Serial.println("OLED FAILED");

    while (true);
  }


  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  display.setTextSize(1);

  display.setCursor(0, 0);
  display.println("SENSOR TEST");

  display.setCursor(0, 10);
  display.println("Starting...");

  display.display();

  delay(1000);


  // MPU6050
  initMPU();

  Serial.println("READY");
}


// ------------------------------------------------
// LOOP
// ------------------------------------------------

void loop() {

  readMPU();


  // ----------------------------------------------
  // TOUCH
  // ----------------------------------------------

  bool touched = digitalRead(TOUCH_PIN);


  // ----------------------------------------------
  // ACCELERATION
  // ----------------------------------------------

  float x = ax / 16384.0;
  float y = ay / 16384.0;
  float z = az / 16384.0;


  // ----------------------------------------------
  // MAP TILT TO DOT POSITION
  // ----------------------------------------------

  // Change these signs if movement is reversed

  dotX = mapFloat(
    x,
    -1.0,
    1.0,
    AREA_LEFT + 6,
    AREA_RIGHT - 6
  );

  dotY = mapFloat(
    y,
    -1.0,
    1.0,
    AREA_BOTTOM - 6,
    AREA_TOP + 6
  );


  // Keep dot inside screen

  dotX = constrain(
    dotX,
    AREA_LEFT + 6,
    AREA_RIGHT - 6
  );

  dotY = constrain(
    dotY,
    AREA_TOP + 6,
    AREA_BOTTOM - 6
  );


  // ----------------------------------------------
  // SERIAL DEBUG
  // ----------------------------------------------

  Serial.print("ACC X: ");
  Serial.print(x, 2);

  Serial.print("  Y: ");
  Serial.print(y, 2);

  Serial.print("  Z: ");
  Serial.print(z, 2);

  Serial.print("  TOUCH: ");

  if (touched)
    Serial.println("YES");
  else
    Serial.println("NO");


  // ----------------------------------------------
  // DRAW OLED
  // ----------------------------------------------

  display.clearDisplay();


  // Title
  display.setTextSize(1);

  display.setCursor(0, 0);
  display.print("MPU6050 + TTP223");


  // Touch status
  display.setCursor(90, 0);

  if (touched)
    display.print("ON");
  else
    display.print("OFF");


  // Accelerometer values

  display.setCursor(0, 9);
  display.print("X:");
  display.print(x, 1);

  display.setCursor(42, 9);
  display.print("Y:");
  display.print(y, 1);

  display.setCursor(84, 9);
  display.print("Z:");
  display.print(z, 1);


  // Separator line

  display.drawLine(
    0,
    20,
    127,
    20,
    SSD1306_WHITE
  );


  // Play area border

  display.drawRect(
    AREA_LEFT,
    AREA_TOP,
    AREA_RIGHT + 1,
    AREA_BOTTOM - AREA_TOP + 1,
    SSD1306_WHITE
  );


  // ----------------------------------------------
  // DOT
  // ----------------------------------------------

  int radius;

  if (touched)
    radius = TOUCH_RADIUS;
  else
    radius = NORMAL_RADIUS;


  display.fillCircle(
    (int)dotX,
    (int)dotY,
    radius,
    SSD1306_WHITE
  );


  // ----------------------------------------------
  // TOUCH INDICATOR
  // ----------------------------------------------

  if (touched) {

    display.setCursor(
      40,
      25
    );

    display.print("TOUCHED");
  }


  display.display();


  delay(30);
}


// ------------------------------------------------
// MPU6050 INITIALIZATION
// ------------------------------------------------

void initMPU() {

  // Wake sensor

  Wire.beginTransmission(MPU_ADDR);

  Wire.write(0x6B);
  Wire.write(0x00);

  Wire.endTransmission();


  // Accelerometer ±2g

  Wire.beginTransmission(MPU_ADDR);

  Wire.write(0x1C);
  Wire.write(0x00);

  Wire.endTransmission();


  // Gyroscope ±250 deg/s

  Wire.beginTransmission(MPU_ADDR);

  Wire.write(0x1B);
  Wire.write(0x00);

  Wire.endTransmission();


  Serial.println(
    "MPU6050 initialized"
  );
}


// ------------------------------------------------
// READ MPU6050
// ------------------------------------------------

void readMPU() {

  Wire.beginTransmission(MPU_ADDR);

  Wire.write(0x3B);

  Wire.endTransmission(false);

  Wire.requestFrom(
    MPU_ADDR,
    14
  );


  if (Wire.available() >= 14) {

    ax = Wire.read() << 8 | Wire.read();

    ay = Wire.read() << 8 | Wire.read();

    az = Wire.read() << 8 | Wire.read();


    // Temperature
    Wire.read();
    Wire.read();


    gx = Wire.read() << 8 | Wire.read();

    gy = Wire.read() << 8 | Wire.read();

    gz = Wire.read() << 8 | Wire.read();
  }
}


// ------------------------------------------------
// FLOAT MAP
// ------------------------------------------------

float mapFloat(
  float value,
  float fromLow,
  float fromHigh,
  float toLow,
  float toHigh
) {

  return (
    (value - fromLow) *
    (toHigh - toLow) /
    (fromHigh - fromLow)
  ) + toLow;
}

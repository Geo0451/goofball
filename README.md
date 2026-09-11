<img width="1280" height="640" alt="git (1)" src="https://github.com/user-attachments/assets/8920b256-2ba8-4988-b824-5351134eb4bd" />

# Goofball 🎯

## Basic Details

### Team Name: NameError

### Team Members

- Member 1: Geo K J - Sahrdaya College of Engineering and Technology
- Member 2: Harikrishna O. R- Sahrdaya College of Engineering and Technology

### Project Description

Magic 8-Ball w/ LLM stuffs

### The Problem (that doesn't exist)

The lack of certainty in life.

### The Solution (that nobody asked for)

![example](./example.jpg)


## Technical Details

### Technologies/Components Used

For Software:

- C++
- Adafruit GFX, Sensors, Json
- VSCode, Github, Arduino IDE

For Hardware:

- Breadboard, ESP32-WROOM Dev Kit, MPU6050 IMU Sensor, TTP223 Digital Touch Sensor, 0.96 Inch OLED Display Module SPI/I2C 4pin 
- **Breadboard:** 830 tie-points, 0.1" pitch\
   **ESP32-WROOM:** 240MHz dual-core, Wi-Fi/BLE, 4MB Flash\
   **MPU6050:** 6-axis gyro/accelerometer, 16-bit ADC, I2C\
   **TTP223:** Single-channel capacitive touch, 2–5.5V\
   **0.96" OLED:** 128x64 SSD1306 display, 4-pin I2C (`0x3C`).\
- Wirecutters, USB-C Cable(Power & data transfer for compiled .ino code), A wifi network to connect to

### Implementation

The ESP32 Magic 8-Ball combines edge-sensor event handling, local network API communication, and a custom OLED graphics engine into a single unified event loop.

### Core Workflow

1. **Arming State Machine (TTP223):**
   - The capacitive touch sensor acts as a toggle switch on GPIO 27.
   - The device status is drawn to the screen buffer via custom 8x8 PROGMEM bitmaps in the status bar (Open Eye = Armed, Closed Eye = Disarmed).

2. **Motion Detection (MPU6050):**
   - The accelerometer streams raw X, Y, and Z axis readings over I2C.
   - Combined vector acceleration is calculated: $\text{G} = \sqrt{x^2 + y^2 + z^2}$.
   - If the system is **Armed** and acceleration exceeds `1.7G`, the shake sequence triggers.

3. **Loading State & API Payload:**
   - A 1.2-second randomized 8-ball physics animation plays on screen to cover network latency.
   - An HTTP POST request containing system prompt rules, context length parameters, and `stream: false` is serialized via `ArduinoJson` and sent to the local Ollama backend over Wi-Fi.

4. **Display Engine & String Sanitization:**
   - The response payload is extracted and stripped of dynamic formatting artifacts (double quotes and non-ASCII characters).
   - The sanitized string passes into a custom word-wrapping function that calculates precise pixel offsets per word (`length * 6px`) to prevent visual stacking glitches on the SSD1306 display.


### Installation

1. **Install Ollama on Host Machine (Fedora/Linux):**
   ```bash
   curl -fsSL https://ollama.com/install.sh | sh
   ```

2. **Pull the Qwen LLM Model:**
   ```bash
   ollama pull qwen2.5:3b-instruct
   ```

3. **Configure Ollama for External Network Binding:**
   ```bash
   sudo systemctl edit ollama.service
   ```
   Add the following block under the `[Service]` section:
   ```ini
   [Service]
   Environment="OLLAMA_HOST=0.0.0.0"
   ```

4. **Allow Port 11434 Through Fedora Firewall:**
   ```bash
   sudo firewall-cmd --zone=trusted --add-port=11434/tcp --permanent
   sudo firewall-cmd --reload
   ```

5. **Flash Firmware to ESP32:**
   - Open Arduino IDE or PlatformIO.
   - Install required dependencies via Library Manager:
     - `Adafruit SSD1306`
     - `Adafruit GFX Library`
     - `ArduinoJson` (by Benoit Blanchon)
   - Update `WIFI_SSID`, `WIFI_PASS`, and `OLLAMA_URL` in the `.ino` file with your hotspot and host PC details.
   - Compile and flash to your ESP32 board.


### Run

1. **Restart Ollama Service:**
   ```bash
   sudo systemctl daemon-reload
   sudo systemctl restart ollama.service
   ```

2. **Verify Host Network Binding:**
   ```bash
   ss -tulpn | grep 11434
   ```
   *(Ensure output shows `*:11434` or `0.0.0.0:11434`)*

3. **Power On ESP32 Device:**
   - Connect ESP32 to power; it will automatically join the Wi-Fi hotspot.
   - Tap the **TTP223 Touch Pad** to **ARM** the device (the top-right eye icon will open).
   - **Shake** the hardware to generate a new sarcastic AI fortune.

# Screenshots (Add at least 3)

![Screenshot1](Add screenshot 1 here with proper name)
_Add caption explaining what this shows_

![Screenshot2](Add screenshot 2 here with proper name)
_Add caption explaining what this shows_

![Screenshot3](Add screenshot 3 here with proper name)
_Add caption explaining what this shows_

# Diagrams


```text
[ ESP32 Handheld Device ]
  │
  ├─► TTP223 Touch Pin (GPIO 27) ────► Toggles Armed/Disarmed state
  ├─► MPU6050 (I2C 0x68) ─────────────► Calculates total G-force (Threshold > 1.7G)
  │                                           │
  │                                      (Shake Event Triggered)
  │                                           │
  ├─► SSD1306 OLED (I2C 0x3C) ◄───────── Plays 8-Ball Loading Animation
  │                                           │
  └─► HTTP Client (WIFI_STA) ───────────────┼─► POST http://<HOST_IP>:11434/api/generate
                                              │   Payload: JSON { model, system prompt, stream: false }
                                              │
                                              ▼
                                     [ Local Ollama Instance ]
                                     (Model: qwen2.5:3b-instruct)
                                              │
  ┌─◄ Raw JSON String Response ───────────────┘
  │
  ├─► ArduinoJson ────────────────────► Parses response payload
  ├─► Text Renderer ──────────────────► Strips quotes, sanitizes ASCII, wraps text
  └─► SSD1306 OLED Display ───────────► Renders final text output
```

# Schematic & Circuit

![Circuit](Add your circuit diagram here)
_Add caption explaining connections_

![Schematic](Add your schematic diagram here)
_Add caption explaining the schematic_

# Build Photos

![Components](Add photo of your components here)
_List out all components shown_

![Build](Add photos of build process here)
_Explain the build steps_

![Final](Add photo of final product here)
_Explain the final build_

### Project Demo

# Video

[![YouTube Demo](https://img.youtube.com/vi/U24JJsOnAx4/0.jpg)](https://youtube.com/shorts/U24JJsOnAx4)

# Additional Demos

[Add any extra demo materials/links]

## Team Contributions

- [Name 1]: [Specific contributions]
- [Name 2]: [Specific contributions]

---

Made with ❤️ at TinkerHub Useless Projects

![Static Badge](https://img.shields.io/badge/TinkerHub-24?color=%23000000&link=https%3A%2F%2Fwww.tinkerhub.org%2F)
![Static Badge](https://img.shields.io/badge/UselessProjects--26-26?link=https%3A%2F%2Ftinkerhub.org%2Fevents%2F1M8ORET9A1%2Fuseless-projects-3.0)

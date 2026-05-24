# USERMANUAL.md

## ESP32 FreeRTOS Motor Control – Detailed User Manual

### Table of Contents
1. [Introduction](#introduction)
2. [Hardware Requirements](#hardware-requirements)
3. [Software Setup](#software-setup)
4. [Project Overview](#project-overview)
5. [File Structure](#file-structure)
6. [Building and Flashing the Firmware](#building-and-flashing-the-firmware)
7. [Running the Application](#running-the-application)
8. [Understanding the FreeRTOS Tasks](#understanding-the-freertos-tasks)
9. [Customising the Motor Control Logic](#customising-the-motor-control-logic)
10. [Troubleshooting](#troubleshooting)
11. [FAQ](#faq)
12. [Appendix – Code Reference](#appendix-code-reference)

---

### 1. Introduction

This manual provides a step‑by‑step guide for using the **ESP32 FreeRTOS Motor Control** example. It explains how to set up the hardware, compile the code, flash the firmware, and modify the motor‑control behaviour. The application demonstrates the power of FreeRTOS on the ESP32 for real‑time motor management using multitasking, inter‑task communication, and safe shutdown procedures.

---

### 2. Hardware Requirements

| Component | Recommended Model | Notes |
|-----------|-------------------|-------|
| ESP32 Development Board | ESP‑WROOM‑32, ESP‑32S, etc. | Any ESP32 with enough GPIO pins |
| Motor Driver | L298N, DRV8871, or similar | Must support PWM speed control |
| DC Motor | 5 V‑12 V brushed motor | Ensure driver rating matches motor |
| Power Supply | 5 V‑12 V for motor, 3.3 V for ESP32 | Separate supply for motor recommended |
| Push‑Button (optional) | Momentary tactile switch | Used to start/stop the motor |
| Connecting Wires, Breadboard | – | – |

**Pin Connections (default):**
- `MOTOR_PWM_PIN` → GPIO 18 (PWM output to driver EN pin)
- `MOTOR_IN1_PIN` → GPIO 19 (direction pin 1)
- `MOTOR_IN2_PIN` → GPIO 21 (direction pin 2)
- `BUTTON_PIN` → GPIO 4 (optional start/stop button)
- `FAULT_PIN` → GPIO 5 (optional fault input from driver)

> **Tip** – All pin definitions are located at the top of `ESP32_FreeRTOS_motorcontrol.ino`. Change them to match your wiring.

---

### 3. Software Setup

#### 3.1 Arduino IDE (recommended)
1. Install **Arduino IDE 2.x** (or the latest stable version).
2. Open **Boards Manager** and install **ESP32 by Espressif Systems**.
3. Ensure **FreeRTOS** is available – it ships with the ESP32 core, so no extra library is needed.

#### 3.2 PlatformIO (alternative)
```bash
# Install PlatformIO extension for VS Code (or via pip)
pip install platformio
# Create a new project targeting ESP32
pio init --board esp-wroom-32
```
Add the source file `ESP32_FreeRTOS_motorcontrol.ino` to the `src/` folder.

---

### 4. Project Overview

The sketch creates three FreeRTOS tasks:
1. **ControllerTask** – Generates a PWM duty‑cycle ramp, handles button presses, and commands the motor driver.
2. **MonitorTask** – Periodically prints the current PWM value, stack high‑water marks, and basic system statistics to the Serial console.
3. **CleanupTask** – Executes when the motor is stopped, gently ramps the PWM down to zero and disables the driver to avoid abrupt braking.

All tasks communicate via a **binary semaphore** (`xMotorControlSemaphore`) that protects the shared `motorSpeed` variable.

---

### 5. File Structure
```
ESP32_FreeRTOS_motorcontrol/
├─ ESP32_FreeRTOS_motorcontrol.ino   # Main sketch (contains tasks)
├─ README.md                        # Quick project overview
├─ USERMANUAL.md                    # This detailed manual
├─ LICENSE                          # MIT License text
└─ .gitignore (optional)            # Files ignored by Git
```

---

### 6. Building and Flashing the Firmware
#### 6.1 Using Arduino IDE
1. Open `ESP32_FreeRTOS_motorcontrol.ino`.
2. Set **Tools → Board → ESP32 Dev Module** (or the exact board you own).
3. Choose the correct **Port**.
4. Click **Upload** (⌘ + U).

#### 6.2 Using PlatformIO
```bash
# From the project root directory
pio run      # Build
pio run -t upload   # Flash
```
The build output will show the compiled binary size and any warnings.

---

### 7. Running the Application
1. Open the Serial Monitor at **115200 baud**.
2. Power the motor driver and ensure the motor is connected correctly.
3. Press the **button** (or send a command via Serial) to start the motor.
4. Observe the monitor task printing lines like:
```
[Monitor] PWM Duty: 128  |  FreeRTOS Heap: 31200 bytes
```
5. Press the button again to stop – the cleanup task will ramp down the PWM smoothly.

> **Note** – If you do not have a physical button, you can trigger start/stop by sending `s` or `x` characters from the Serial terminal (the sketch includes a simple Serial command parser).

---

### 8. Understanding the FreeRTOS Tasks
| Task | Priority | Stack (bytes) | Description |
|------|----------|----------------|-------------|
| `ControllerTask` | 2 (medium) | 2048 | Handles PWM generation and button debouncing. |
| `MonitorTask` | 1 (low) | 1024 | Periodically prints system stats. |
| `CleanupTask` | 3 (high) | 1024 | Executes safe shutdown when motor stop is requested. |

**Inter‑Task Communication:**
- **Semaphore** – `xMotorControlSemaphore` protects the shared `motorSpeed` variable.
- **Queue (optional)** – You can replace the semaphore with a queue to send detailed commands (speed setpoints, direction changes).

---

### 9. Customising the Motor Control Logic
#### 9.1 Changing PWM Pin
Edit the macro at the top of the sketch:
```cpp
#define MOTOR_PWM_PIN 18   // <‑‑ change to your chosen GPIO
```
#### 9.2 Modifying Speed Ramp
The function `speedRamp(uint8_t targetSpeed, uint16_t stepDelayMs)` defines how the motor accelerates. Adjust `stepDelayMs` for faster/slower ramps or change the increment step size.
#### 9.3 Adding PID Control
1. Include the **Arduino PID library** (`#include <PID_v1.h>`).
2. Declare `PID motorPID(&input, &output, &setpoint, Kp, Ki, Kd, DIRECT);`
3. Call `motorPID.Compute();` inside `ControllerTask` to calculate the PWM duty based on a measured speed sensor.
#### 9.4 Adding Additional Sensors
Place ADC reads (e.g., current sense) inside `MonitorTask` and send the values through a **queue** to the controller for closed‑loop regulation.

---

### 10. Troubleshooting
| Symptom | Possible Cause | Fix |
|---------|----------------|-----|
| Motor does not spin | PWM pin not connected or wrong GPIO | Verify wiring and `MOTOR_PWM_PIN` definition |
| PWM flickers / erratic speed | Button bounce not debounced | Increase `DEBOUNCE_MS` constant or use hardware RC filter |
| Serial output garbled | Baud rate mismatch | Set both IDE and `Serial.begin(115200);` to 115200 |
| FreeRTOS watchdog reset | Task stack overflow | Increase stack size in `xTaskCreatePinnedToCore` calls |
| No output after start | Fault pin pulled low, driver disabled | Check `FAULT_PIN` wiring or comment out fault handling |

---

### 11. FAQ
**Q:** *Can I run more than one motor?*  
**A:** Yes. Duplicate the PWM, direction pins, and create separate controller tasks for each motor. Use separate semaphores or a queue to coordinate them.

**Q:** *Is FreeRTOS mandatory?*  
**A:** Not strictly – you could run a simple loop. FreeRTOS provides deterministic task scheduling, which is beneficial for real‑time motor control.

**Q:** *How do I change the default speed?*  
**A:** Modify the `DEFAULT_SPEED` macro (value 128) near the top of the file.

---

### 12. Appendix – Code Reference
Below is a concise reference of the most important functions and macros. For the full source, see `ESP32_FreeRTOS_motorcontrol.ino`.
```cpp
// Pin definitions – edit as needed
#define MOTOR_PWM_PIN   18
#define MOTOR_IN1_PIN   19
#define MOTOR_IN2_PIN   21
#define BUTTON_PIN      4
#define FAULT_PIN       5

// Default parameters
#define DEFAULT_SPEED   128   // 0‑255 PWM duty
#define DEBOUNCE_MS     50

// Global semaphore protecting motorSpeed
SemaphoreHandle_t xMotorControlSemaphore;
volatile uint8_t motorSpeed = 0;

// Forward declarations
void ControllerTask(void *pvParameters);
void MonitorTask(void *pvParameters);
void CleanupTask(void *pvParameters);
void speedRamp(uint8_t target, uint16_t stepDelay);

// ---- Task implementations (see source) ----
```

---

### End of Manual

For further details, refer to the source code comments and the inline documentation within the sketch.

---

*Last updated: 2026‑05‑24*

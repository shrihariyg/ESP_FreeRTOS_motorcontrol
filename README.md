# README.md

## ESP32 FreeRTOS Motor Control

A lightweight Arduino/PlatformIO project that demonstrates how to use **FreeRTOS** on an **ESP32** to control a motor via PWM. The example includes task creation, inter‑task communication, and safe shutdown handling for real‑time motor control applications.

---

### Features

- **FreeRTOS** based multitasking (controller task, monitor task, and cleanup task).
- Motor speed control using PWM with **GPIO 18** (default – can be re‑configured).
- Safe start/stop logic with button debouncing.
- Comprehensive comments throughout the source code.
- Compatible with Arduino IDE and PlatformIO.

---

### Prerequisites

- **ESP32 development board** (e.g., ESP‑WROOM‑32) with a motor driver (L298N, DRV8871, etc.).
- Arduino IDE **2.x** or **PlatformIO** set up for ESP32.
- Basic knowledge of FreeRTOS concepts (tasks, queues, semaphores).

---

### Installation

1. **Clone or download** the repository.
2. Open the project folder in Arduino IDE (`File → Open`) or import it into PlatformIO.
3. Ensure the **FreeRTOS** library is installed (it ships with the ESP32 core). No extra dependencies are required.
4. Connect your motor driver to the ESP32:
   - PWM output → `GPIO 18` (or modify `MOTOR_PWM_PIN` in the sketch).
   - Direction pins → as required by your driver.
   - Optional enable / fault pins can be wired to `GPIO 4` / `GPIO 5`.
5. Verify the hardware connections and power the driver appropriately.

---

### Usage

1. **Upload** the sketch `ESP32_FreeRTOS_motorcontrol.ino` to your ESP32.
2. Open the Serial Monitor (115200 baud) to view debug messages.
3. Use the on‑board **button** (or your own input) to start/stop the motor:
   - Press once: motor ramps up to the default speed.
   - Press again: motor slows down and stops safely.
4. The **monitor task** prints current PWM duty cycle and task statistics every second.

---

### Project Structure

- `ESP32_FreeRTOS_motorcontrol.ino` – Main sketch containing FreeRTOS task definitions.
- `README.md` – This file – quick overview.
- `USERMANUAL.md` – Detailed usage guide (see below).

---

### Customisation

- **PWM pin** – edit `#define MOTOR_PWM_PIN 18`.
- **Speed profile** – modify the `speedRamp()` function in the controller task.
- **Task priorities** – adjust the `uxPriority` values when creating tasks.
- **Additional sensors** – you can add ADC reads inside the monitor task and share data via queues.



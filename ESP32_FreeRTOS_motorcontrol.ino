#include <Arduino.h>
#include <Wire.h>
#include <MPU6050.h>
#include "I2Cdev.h"
#include <DHT.h>
#include <SD.h>
#include <SPI.h>
#include <Adafruit_SH110X.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

//═══════════════════════════════════════
// PIN DEFINITIONS
//═══════════════════════════════════════
#define DHT11_PIN         13
#define ACS712_PIN        34
#define VOLTAGE_PIN        4
#define IN1_PIN           26
#define IN2_PIN           27
#define ENA_PIN           14
#define SD_CS_PIN          5
#define SDA_PIN           21
#define SCL_PIN           22
#define BUTTON_PIN        25
#define ENABLE_SD         0
//═══════════════════════════════════════
// OLED CONFIG
//═══════════════════════════════════════
#define OLED_ADDRESS    0x3C
#define SCREEN_WIDTH     128
#define SCREEN_HEIGHT     64

//═══════════════════════════════════════
// SAMPLING CONFIG
//═══════════════════════════════════════
#define SAMPLE_RATE_MS    10   // 100Hz (10)
#define QUEUE_SIZE        100

//═══════════════════════════════════════
// ACS712 CONFIG
// 5A  → 0.185
// 20A → 0.100
// 30A → 0.066
//═══════════════════════════════════════
#define ACS712_SENSITIVITY  0.185f
#define ACS712_ZERO_OFFSET  1.65f

//═══════════════════════════════════════
// VOLTAGE SENSOR CONFIG
//═══════════════════════════════════════
#define VOLTAGE_RATIO       5.0f

//═══════════════════════════════════════
// MOTOR CONFIG
//═══════════════════════════════════════
#define MOTOR_SPEED         180
#define PWM_CHANNEL           0
#define PWM_FREQ           1000
#define PWM_RES               8

//═══════════════════════════════════════
// LABEL — CHANGE BEFORE EACH RUN
// 0 = Normal
// 1 = Bearing Fault
// 2 = Overload
// 3 = Misalignment
//═══════════════════════════════════════
#define CURRENT_LABEL         0  // change

const char* LABEL_NAMES[] = {
  "NORMAL",
  "BRG FAULT",
  "OVERLOAD",
  "MISALIGN"
};

//═══════════════════════════════════════
// OBJECTS
//═══════════════════════════════════════
MPU6050          mpu;
DHT              dht(DHT11_PIN, DHT11);
Adafruit_SH1106G display(SCREEN_WIDTH,
                          SCREEN_HEIGHT,
                          &Wire, -1);

//═══════════════════════════════════════
// DATA STRUCTURES
//═══════════════════════════════════════
struct SensorFrame {
  unsigned long timestamp;
  float accel_x;
  float accel_y;
  float accel_z;
  float gyro_x;
  float gyro_y;
  float gyro_z;
  float temperature;
  float humidity;
  float current;
  float motorVoltage;
  int   label;
};

struct DisplayData {
  float accel_x;
  float accel_y;
  float accel_z;
  float gyro_x;
  float gyro_y;
  float gyro_z;
  float temperature;
  float humidity;
  float current;
  float motorVoltage;
  unsigned long samples;
  unsigned long dropped;
  int   label;
  bool  sdPresent;
};

//═══════════════════════════════════════
// GLOBAL FLAGS + STATS
//═══════════════════════════════════════
bool sdAvailable                 = false;
volatile unsigned long totalSamples  = 0;
volatile unsigned long droppedFrames = 0;

//═══════════════════════════════════════
// FREERTOS HANDLES
//═══════════════════════════════════════
QueueHandle_t     sensorQueue;
QueueHandle_t     displayQueue;
SemaphoreHandle_t sdMutex;
QueueHandle_t serialQueue;
QueueHandle_t sdQueue;

//═══════════════════════════════════════════════
// HELPER: Read ACS712
//═══════════════════════════════════════════════
float readCurrent() {
  long sum = 0;
  for (int i = 0; i < 500; i++) {
    sum += analogRead(ACS712_PIN);
    delayMicroseconds(50);
  }
  float avg     = sum / 500.0f;
  float voltage = avg * (3.3f / 4095.0f);
  float current = (voltage - ACS712_ZERO_OFFSET)
                  / ACS712_SENSITIVITY;
  return abs(current);
}

//═══════════════════════════════════════════════
// HELPER: Read Motor Voltage
//═══════════════════════════════════════════════
float readMotorVoltage() {
  long sum = 0;
  for (int i = 0; i < 500; i++) {
    sum += analogRead(VOLTAGE_PIN);
    delayMicroseconds(50);
  }
  float avg        = sum / 500.0f;
  float adcVoltage = avg * (3.3f / 4095.0f);
  return abs(adcVoltage * VOLTAGE_RATIO);
}

//═══════════════════════════════════════════════
// MOTOR HELPERS
//═══════════════════════════════════════════════
void setupMotor() {
  pinMode(IN1_PIN, OUTPUT);
  pinMode(IN2_PIN, OUTPUT);
  ledcSetup(PWM_CHANNEL, PWM_FREQ, PWM_RES);
  ledcAttachPin(ENA_PIN, PWM_CHANNEL);
}

void motorForward(int speed) {
  digitalWrite(IN1_PIN, HIGH);
  digitalWrite(IN2_PIN, LOW);
  ledcWrite(PWM_CHANNEL, speed);
}

void motorStop() {
  digitalWrite(IN1_PIN, LOW);
  digitalWrite(IN2_PIN, LOW);
  ledcWrite(PWM_CHANNEL, 0);
}

//═══════════════════════════════════════════════
// SD INIT — Returns false if no card
//═══════════════════════════════════════════════
bool initSD() {
  Serial.println("Innitializating SD...");

  if (!SD.begin(SD_CS_PIN)) {
    Serial.println("SD init FAILED ");
    return false;
  }

  Serial.println("SD init SUCCESS ");

  // Write header only if file doesnt exist
  if (!SD.exists("/data.csv")) {
    File f = SD.open("/data.csv", FILE_WRITE);
    if (f) {
      // f.println(
      //   "timestamp,"
      //   "accel_x,accel_y,accel_z,"
      //   "gyro_x,gyro_y,gyro_z,"
      //   "temperature,humidity,"
      //   "current,motorVoltage,"
      //   "label"
      // );
      f.println("timestamp,accel_x,accel_y,accel_z,gyro_x,gyro_y,gyro_z,temperature,humidity,current,motorVoltage,label");
      f.close();
      Serial.println("[SD] New file created");
    }
   else {
    Serial.println("[SD] Appending to existing file");
   }
  }
  return true;
}

//═══════════════════════════════════════════════
// OLED BOOT SCREEN HELPER
//═══════════════════════════════════════════════
void showBootScreen(const char* msg, bool ok) {
  display.setTextSize(1);
  display.setTextColor(SH110X_WHITE);
  display.printf("%s %s\n",
    ok ? "[OK] " : "[ERR]", msg);
  display.display();
}

//═══════════════════════════════════════════════
// TASK 1: Sensor Sampling — Priority 4
//═══════════════════════════════════════════════
void sensorTask(void *pvParams) {
  TickType_t lastWake = xTaskGetTickCount();
  int16_t ax, ay, az, gx, gy, gz;

  unsigned long lastDHTRead = 0;
  float lastTemp = 0.0f;
  float lastHumi = 0.0f;

  while (1) {
    SensorFrame frame;
    frame.timestamp = millis();
    frame.label     = CURRENT_LABEL;

    // MPU6050
    mpu.getMotion6(&ax, &ay, &az,
                   &gx, &gy, &gz);
    frame.accel_x = ax / 16384.0f;
    frame.accel_y = ay / 16384.0f;
    frame.accel_z = az / 16384.0f;
    frame.gyro_x  = gx / 131.0f;
    frame.gyro_y  = gy / 131.0f;
    frame.gyro_z  = gz / 131.0f;

    // DHT11 — reuse last value between reads
    if (millis() - lastDHTRead >= 2000) {
      float t = dht.readTemperature();
      float h = dht.readHumidity();
      if (!isnan(t) && !isnan(h)) {
        lastTemp    = t;
        lastHumi    = h;
        lastDHTRead = millis();
      }
    }
    frame.temperature = lastTemp;
    frame.humidity    = lastHumi;

    // Analog sensors
    frame.current      = readCurrent();
    frame.motorVoltage = readMotorVoltage();

    frame.motorVoltage = readMotorVoltage();


    char dataString[150];

    sprintf(dataString,"%lu,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.2f,%.2f,%.3f,%.3f,%d\n",
    frame.timestamp,
    frame.accel_x,
    frame.accel_y,
    frame.accel_z,
    frame.gyro_x,
    frame.gyro_y,
    frame.gyro_z,
    frame.temperature,
    frame.humidity,
    frame.current,
    frame.motorVoltage,
    frame.label
);

Serial2.print(dataString);

    // Disabled for optional SD logging
    // // Push to logger queue
    // if (xQueueSend(sensorQueue, &frame, 0)
    //     != pdTRUE) {
    //   droppedFrames++;
    // } else {
    //   totalSamples++;
    // }

    // SERIAL QUEUE
    if (xQueueSend(serialQueue, &frame, 0) != pdTRUE) {
        droppedFrames++;
      } else {
        totalSamples++;  // ← now increments correctly
      }

    // SD QUEUE (optional)
    #if ENABLE_SD
    if (xQueueSend(sdQueue, &frame, 0) != pdTRUE) {
    // drop if full
   }
    #endif

    // Update display queue
    // Always overwrite stale display data
    DisplayData disp;
    disp.accel_x      = frame.accel_x;
    disp.accel_y      = frame.accel_y;
    disp.accel_z      = frame.accel_z;
    disp.gyro_x       = frame.gyro_x;
    disp.gyro_y       = frame.gyro_y;
    disp.gyro_z       = frame.gyro_z;
    disp.temperature  = frame.temperature;
    disp.humidity     = frame.humidity;
    disp.current      = frame.current;
    disp.motorVoltage = frame.motorVoltage;
    disp.samples      = totalSamples;
    disp.dropped      = droppedFrames;
    disp.label        = CURRENT_LABEL;
    disp.sdPresent    = sdAvailable;

    // Drain old display data before sending new
    if (uxQueueSpacesAvailable(displayQueue) == 0) {
      DisplayData dummy;
      xQueueReceive(displayQueue, &dummy, 0);
    }
    xQueueSend(displayQueue, &disp, 0);

    vTaskDelayUntil(&lastWake,
                    pdMS_TO_TICKS(SAMPLE_RATE_MS));
  }
  
}

//═══════════════════════════════════════════════
// TASK 2: SD Logger — Priority 3
// Skips write gracefully if no SD card
//═══════════════════════════════════════════════
#if ENABLE_SD

void loggerTask(void *pvParams) {
  SensorFrame frame;

  while (1) {
    if (xQueueReceive(sdQueue, &frame, portMAX_DELAY) == pdTRUE) {

      if (!sdAvailable) {
        vTaskDelay(pdMS_TO_TICKS(10));
        continue;
      }

      if (xSemaphoreTake(sdMutex, pdMS_TO_TICKS(100)) == pdTRUE) {

        File f = SD.open("/data.csv", FILE_APPEND);

        if (f) {
          f.printf(
            "%lu,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.2f,%.2f,%.4f,%.4f,%d\n",
            frame.timestamp,
            frame.accel_x,
            frame.accel_y,
            frame.accel_z,
            frame.gyro_x,
            frame.gyro_y,
            frame.gyro_z,
            frame.temperature,
            frame.humidity,
            frame.current,
            frame.motorVoltage,
            frame.label
          );
          f.close();
        } else {
          sdAvailable = false;
          Serial.println("[WARN] SD removed!");
        }

        xSemaphoreGive(sdMutex);
      }
    }
  }
}

#endif
//===================================================
// Serial Logger for python
//===================================================
void serialLoggerTask(void *pvParams) {
  SensorFrame frame;

  while (1) {
    if (xQueueReceive(serialQueue, &frame, portMAX_DELAY) == pdTRUE) {

      Serial.printf(
        "%lu,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.2f,%.2f,%.4f,%.4f,%d\n",
        frame.timestamp,
        frame.accel_x,
        frame.accel_y,
        frame.accel_z,
        frame.gyro_x,
        frame.gyro_y,
        frame.gyro_z,
        frame.temperature,
        frame.humidity,
        frame.current,
        frame.motorVoltage,
        frame.label
      );
    }
  }
}

//═══════════════════════════════════════════════
// TASK 3: OLED Display — Priority 2
// Screen 1: IMU + DHT11 readings
// Screen 2: Current + Voltage + Stats
// Screen 3: Collection info + SD status
//═══════════════════════════════════════════════
void displayTask(void *pvParams) {
  DisplayData disp;
  int screen = 0;
  TickType_t lastSwitch = xTaskGetTickCount();

  while (1) {
    if (xQueueReceive(displayQueue, &disp,
                      pdMS_TO_TICKS(600))
        == pdTRUE) {

      // Rotate screen every 2.5 seconds
      // if (xTaskGetTickCount() - lastSwitch
      //     >= pdMS_TO_TICKS(2500)) {
      //   screen = (screen + 1) % 3;
      //   lastSwitch = xTaskGetTickCount();
      // }
      static int screen = 0;
      static bool lastButtonState = HIGH;

      bool currentButton = digitalRead(BUTTON_PIN);

      // Detect button press (falling edge)
      if (lastButtonState == HIGH && currentButton == LOW) {
        screen = (screen + 1) % 3;
        vTaskDelay(pdMS_TO_TICKS(200));  // debounce
      }

      lastButtonState = currentButton;

      display.clearDisplay();
      display.setTextColor(SH110X_WHITE);
      display.setTextSize(1);

      switch (screen) {

        //───────────────────────────────
        // Screen 1: IMU Data
        //───────────────────────────────
        case 0:
          display.setCursor(0, 0);
          display.println(" IMU DATA ");

          display.setCursor(0, 12);
          display.printf("AX: %+.3f g", disp.accel_x);

          display.setCursor(0, 22);
          display.printf("AY: %+.3f g", disp.accel_y);

          display.setCursor(0, 32);
          display.printf("AZ: %+.3f g", disp.accel_z);

          display.setCursor(0, 42);
          display.printf("GX: %+.2f d/s",
                          disp.gyro_x);

          display.setCursor(0, 52);
          display.printf("GY: %+.2f d/s",
                          disp.gyro_y);
          break;

        //───────────────────────────────
        // Screen 2: Power + Environment
        //───────────────────────────────
        case 1:
          display.setCursor(0, 0);
          display.println(" POWER+ENV ");

          display.setCursor(0, 12);
          display.printf("Curr : %.3f A",
                          disp.current);

          display.setCursor(0, 22);
          display.printf("Vmot : %.2f V",
                          disp.motorVoltage);

          display.setCursor(0, 32);
          display.printf("Temp : %.1f C",
                          disp.temperature);

          display.setCursor(0, 42);
          display.printf("Humi : %.1f %%",
                          disp.humidity);

          display.setCursor(0, 52);
          // Show motor power
          display.printf("Pwr  : %.2f W",
            disp.current * disp.motorVoltage);
          break;

        //───────────────────────────────
        // Screen 3: Collection Stats
        //───────────────────────────────
        case 2:
          display.setCursor(0, 0);
          display.println(" COLLECTION ");

          display.setCursor(0, 12);
          display.printf("Label: %d-%s",
            disp.label,
            LABEL_NAMES[disp.label]);

          display.setCursor(0, 22);
          display.printf("Smpl : %lu",
                          disp.samples);

          display.setCursor(0, 32);
          display.printf("Drop : %lu",
                          disp.dropped);

          display.setCursor(0, 42);
          display.printf("RAM  : %dB",
                          ESP.getFreeHeap());

          // SD status with icon
          display.setCursor(0, 52);
          if (disp.sdPresent) {
            display.printf("SD   : Logging [Y]");
          } else {
            display.printf("SD   : Not found [N]");
          }
          break;
      }

      display.display();
    }
  }
}

//═══════════════════════════════════════════════
// TASK 4: Serial Status — Priority 1
//═══════════════════════════════════════════════
void statusTask(void *pvParams) {
  while (1) {
    vTaskDelay(pdMS_TO_TICKS(5000));

    Serial.println("\n══════════════════════════");
    Serial.printf("Label  : %d (%s)\n",
      CURRENT_LABEL,
      LABEL_NAMES[CURRENT_LABEL]);
    Serial.printf("Samples: %lu\n",  totalSamples);
    Serial.printf("Dropped: %lu\n",  droppedFrames);
    Serial.printf("RAM    : %d B\n", ESP.getFreeHeap());
    Serial.printf("SD     : %s\n",
      sdAvailable ? "Logging" : "Not present");
    Serial.printf("Queue  : %d/%d\n",
      uxQueueMessagesWaiting(sensorQueue),
      QUEUE_SIZE);
    Serial.println("══════════════════════════");
  }
}

//═══════════════════════════════════════════════
// SETUP
//═══════════════════════════════════════════════
void setup() {
  Serial.begin(921600);
  delay(1000);

  Serial2.begin(115200, SERIAL_8N1, 16, 17);

  //Button_pin
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  // Init I2C
  Wire.begin(SDA_PIN, SCL_PIN);
  delay(500);
          
  // Init OLED first for boot progress
  display.begin(OLED_ADDRESS, true);
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SH110X_WHITE);
  display.setCursor(0, 0);
  display.println(" Pred. Maintenance");
  display.println(" Data Collection");
  display.println("-----------------");
  display.display();
  delay(1000);

  // Init MPU6050
  mpu.initialize();
  mpu.initialize();
  mpu.setSleepEnabled(false);  // keep falses
  delay(100);


  // Try reading once to verify
  int16_t ax, ay, az, gx, gy, gz;
  mpu.getMotion6(&ax, &ay, &az, &gx, &gy, &gz);

  // Simple check (not blocking system)
  bool mpuOK = !(ax == 0 && ay == 0 && az == 0);

  Serial.printf("[%s] MPU6050\n", mpuOK ? "OK" : "WARN");

  // Init DHT11
  dht.begin();
  showBootScreen("DHT11", true);

  // Init SPI + SD — non blocking
  SPI.begin();
  sdAvailable = initSD();
  showBootScreen("SD Card", sdAvailable);
  if (!sdAvailable) {
    Serial.println("[WARN] No SD card detected");
    Serial.println("[WARN] Logging disabled");
    Serial.println("[WARN] All other functions OK");
  }

  // Init motor
  setupMotor();
  showBootScreen("L298N", true);

  // Show current label on boot screen
  display.setCursor(0, 52);
  display.printf("Label: %d-%s",CURRENT_LABEL,LABEL_NAMES[CURRENT_LABEL]);
  display.display();
  delay(2000);

  // Create FreeRTOS objects
  sensorQueue  = xQueueCreate(QUEUE_SIZE,
                               sizeof(SensorFrame));
  displayQueue = xQueueCreate(2,
                               sizeof(DisplayData));
  sdMutex      = xSemaphoreCreateMutex();

  serialQueue = xQueueCreate(QUEUE_SIZE, sizeof(SensorFrame));

  sdQueue     = xQueueCreate(QUEUE_SIZE, sizeof(SensorFrame));

  if (!sensorQueue || !displayQueue || !sdMutex) {
    Serial.println("[ERROR] FreeRTOS init failed");
    display.clearDisplay();
    display.setCursor(0, 0);
    display.println("RTOS INIT FAILED");
    display.println("Restart device");
    display.display();
    while (1);
  }

  // Create all tasks
  xTaskCreate(sensorTask,       "Sensor",       4096, NULL, 4, NULL);
  xTaskCreate(serialLoggerTask, "SerialLogger", 4096, NULL, 2, NULL);

  #if ENABLE_SD
  xTaskCreate(sdLoggerTask,     "SDLogger",     4096, NULL, 3, NULL);
  #endif

  xTaskCreate(displayTask,      "Display",      4096, NULL, 2, NULL);
  xTaskCreate(statusTask,       "Status",       2048, NULL, 1, NULL);

  Serial.println("\n[START] System running");
  Serial.printf("[INFO]  Label: %d - %s\n",
    CURRENT_LABEL,
    LABEL_NAMES[CURRENT_LABEL]);
  Serial.printf("[INFO]  SD logging: %s\n",
    sdAvailable ? "Enabled" : "Disabled");

  // Start motor after short delay
  delay(2000);
  motorForward(MOTOR_SPEED);
  Serial.println("[INFO]  Motor started");
}

void loop() {
  vTaskDelay(portMAX_DELAY);
}
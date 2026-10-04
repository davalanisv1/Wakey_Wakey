#include <Adafruit_GFX.h> 
#include <Adafruit_ST7789.h> 
#include <SPI.h>

// Screen Pins (Standard SPI setup for your 8-pin header)
#define TFT_SCLK 0 
#define TFT_MOSI 1 
#define TFT_RST  2
#define TFT_DC   3
#define TFT_CS   4
#define TFT_BL   5

// Audio Output Pin
#define BUZZER_PIN 10 // Maps to physical Pin D10 on XIAO ESP32-C3

// Direct-Input Key Pins
#define BTN_MENU    6   // Cycles modes: Normal -> Set Hour -> Set Min -> Set Month -> Set Day -> Set Alarm H -> Set Alarm M -> Set Alarm On/Off
#define BTN_UP      7   // Increments configuration numbers
#define BTN_DOWN    8   // Decrements configuration numbers
#define BTN_DISMISS 9   // Shuts off active buzzing tone

// Custom driver wrapper handling unique screen panel resolution offsets
class MyST7789 : public Adafruit_ST7789 {
public:
  MyST7789(int8_t cs, int8_t dc, int8_t mosi, int8_t sclk, int8_t rst)
    : Adafruit_ST7789(cs, dc, mosi, sclk, rst) {}
  void setOffsets(uint8_t col, uint8_t row) {
    _colstart = _colstart2 = col;
    _rowstart = _rowstart2 = row;
  }
};

MyST7789 tft(TFT_CS, TFT_DC, TFT_MOSI, TFT_SCLK, TFT_RST);

// System UI & State Layout Definitions
enum Mode { NORMAL, SET_HOUR, SET_MIN, SET_MONTH, SET_DAY, SET_ALARM_H, SET_ALARM_M, SET_ALARM_TOGGLE };
Mode currentMode = NORMAL;

int hrs = 12, mins = 0, secs = 0;
int month = 10, day = 3;
int alarmHrs = 7, alarmMins = 0;
bool alarmEnabled = false;
bool alarmTriggered = false;

unsigned long lastTickTime = 0;
unsigned long lastUIRefresh = 0;
unsigned long lastBuzzerToggle = 0;
bool buzzerState = false;

// Simple Debounce helper for discrete key pushes
bool isButtonPressed(int pin) {
  if (digitalRead(pin) == LOW) { // Switch shorts to GND when pressed
    delay(200); // Debounce latch hold
    return true;
  }
  return false;
}

// Helper to determine days in current month
int getDaysInMonth(int m) {
  if (m == 4 || m == 6 || m == 9 || m == 11) return 30;
  if (m == 2) return 28; 
  return 31;
}

void setup() {
  Serial.begin(115200);

  // Initialize Hardware Buttons as Direct Input Pullups
  pinMode(BTN_MENU, INPUT_PULLUP);
  pinMode(BTN_UP, INPUT_PULLUP);
  pinMode(BTN_DOWN, INPUT_PULLUP);
  pinMode(BTN_DISMISS, INPUT_PULLUP);

  // Initialize Piezo Buzzer Pin Output
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);

  // Initialize Display Backlight Controls
  pinMode(TFT_BL, OUTPUT); 
  digitalWrite(TFT_BL, LOW); // Active-Low panel activation

  // Init TFT Screen Metrics
  tft.init(76, 284); 
  tft.setOffsets(82, 18); 
  tft.invertDisplay(false); 
  tft.setRotation(1); 
  
  tft.fillScreen(ST77XX_BLACK);
  lastTickTime = millis();
}

void loop() {
  unsigned long currentMillis = millis();

  // 1. Precise 1-Second Time Tracking Real-Time Rollover Engine
  if (currentMillis - lastTickTime >= 1000) {
    lastTickTime += 1000;
    secs++;
    if (secs >= 60) {
      secs = 0;
      mins++;
      if (mins >= 60) {
        mins = 0;
        hrs++;
        if (hrs >= 24) {
          hrs = 0;
          day++; 
          if (day > getDaysInMonth(month)) { day = 1; month++; if (month > 12) month = 1; }
        }
      }
    }

    if (alarmEnabled && hrs == alarmHrs && mins == alarmMins && secs == 0) {
      alarmTriggered = true;
    }
  }

  // 2. Handle Button Inputs & Menu Traversal
  if (isButtonPressed(BTN_MENU)) {
    tft.fillScreen(ST77XX_BLACK); 
    if (currentMode == NORMAL) currentMode = SET_HOUR;
    else if (currentMode == SET_HOUR) currentMode = SET_MIN;
    else if (currentMode == SET_MIN) currentMode = SET_MONTH;
    else if (currentMode == SET_MONTH) currentMode = SET_DAY;
    else if (currentMode == SET_DAY) currentMode = SET_ALARM_H;
    else if (currentMode == SET_ALARM_H) currentMode = SET_ALARM_M;
    else if (currentMode == SET_ALARM_M) currentMode = SET_ALARM_TOGGLE;
    else if (currentMode == SET_ALARM_TOGGLE) currentMode = NORMAL;
  }

  if (isButtonPressed(BTN_UP)) {
    if (currentMode == SET_HOUR) { hrs = (hrs + 1) % 24; }
    else if (currentMode == SET_MIN) { mins = (mins + 1) % 60; secs = 0; }
    else if (currentMode == SET_MONTH) { month = (month % 12) + 1; day = 1; }
    else if (currentMode == SET_DAY) { day = (day % getDaysInMonth(month)) + 1; }
    else if (currentMode == SET_ALARM_H) { alarmHrs = (alarmHrs + 1) % 24; }
    else if (currentMode == SET_ALARM_M) { alarmMins = (alarmMins + 1) % 60; }
    else if (currentMode == SET_ALARM_TOGGLE) { alarmEnabled = !alarmEnabled; }
  }

  if (isButtonPressed(BTN_DOWN)) {
    if (currentMode == SET_HOUR) { hrs = (hrs == 0) ? 23 : hrs - 1; }
    else if (currentMode == SET_MIN) { mins = (mins == 0) ? 59 : mins - 1; secs = 0; }
    else if (currentMode == SET_MONTH) { month = (month == 1) ? 12 : month - 1; day = 1; }
    else if (currentMode == SET_DAY) { day = (day == 1) ? getDaysInMonth(month) : day - 1; }
    else if (currentMode == SET_ALARM_H) { alarmHrs = (alarmHrs == 0) ? 23 : alarmHrs - 1; }
    else if (currentMode == SET_ALARM_M) { alarmMins = (alarmMins == 0) ? 59 : alarmMins - 1; }
    else if (currentMode == SET_ALARM_TOGGLE) { alarmEnabled = !alarmEnabled; }
  }

  if (isButtonPressed(BTN_DISMISS)) {
    alarmTriggered = false;
    digitalWrite(BUZZER_PIN, LOW);
  }

  // 3. Audio Alarm Driver Generation Engine (Manual Square Wave)
  if (alarmTriggered) {
    // Generate a clean beep pattern (on for 500ms, off for 500ms)
    if ((currentMillis / 500) % 2 == 0) {
      // Toggle the buzzer pin quickly every 1ms to create a ~500Hz audible tone
      if (currentMillis - lastBuzzerToggle >= 1) {
        lastBuzzerToggle = currentMillis;
        buzzerState = !buzzerState;
        digitalWrite(BUZZER_PIN, buzzerState ? HIGH : LOW);
      }
    } else {
      digitalWrite(BUZZER_PIN, LOW);
    }
  } else {
    digitalWrite(BUZZER_PIN, LOW);
  }

  // 4. UI Screen Context Refresh Step
  if (currentMillis - lastUIRefresh >= 250) {
    lastUIRefresh = currentMillis;
    updateDisplay();
  }
}

void updateDisplay() {
  tft.setTextSize(1);
  tft.setTextColor(ST77XX_YELLOW, ST77XX_BLACK);
  
  tft.setCursor(15, 6);
  switch(currentMode) {
    case NORMAL:            tft.print(" [ CLOCK MODE ]   "); break;
    case SET_HOUR:          tft.print(" [ EDIT TIME H ]  "); break;
    case SET_MIN:           tft.print(" [ EDIT TIME M ]  "); break;
    case SET_MONTH:         tft.print(" [ EDIT DATE M ]  "); break;
    case SET_DAY:           tft.print(" [ EDIT DATE D ]  "); break;
    case SET_ALARM_H:       tft.print(" [ EDIT ALARM H ] "); break;
    case SET_ALARM_M:       tft.print(" [ EDIT ALARM M ] "); break;
    case SET_ALARM_TOGGLE:  tft.print(" [ TOGGLE ALARM ] "); break;
  }

  tft.setCursor(20, 22);
  tft.setTextSize(4);
  if (alarmTriggered && (millis() / 250) % 2 == 0) {
    tft.setTextColor(ST77XX_RED, ST77XX_BLACK);
  } else {
    tft.setTextColor(ST77XX_WHITE, ST77XX_BLACK);
  }
  
  char timeBuf[16]; 
  sprintf(timeBuf, "%02d:%02d:%02d", hrs, mins, secs);
  tft.print(timeBuf);

  tft.setTextSize(1);
  tft.setTextColor(ST77XX_GREEN, ST77XX_BLACK);
  
  char dateBuf[16]; 
  sprintf(dateBuf, "Date: %02d/%02d", month, day);
  tft.setCursor(20, 64);
  tft.print(dateBuf);

  char alarmBuf[24]; 
  sprintf(alarmBuf, "Alarm: %02d:%02d [%s]", alarmHrs, alarmMins, alarmEnabled ? "ON" : "OFF");
  tft.setCursor(120, 64);
  tft.print(alarmBuf);
}

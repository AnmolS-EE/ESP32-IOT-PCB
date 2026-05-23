#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <SPI.h>
#include <DHT.h>

// --- Pin Definitions ---
#define TFT_CS    5
#define TFT_RST   16
#define TFT_DC    17

#define DHTPIN    4
#define DHTTYPE   DHT11 
#define LDR_PIN   34
#define MIC_PIN   35

// --- Rotary Encoder Pins ---
#define ENCODER_CLK 27
#define ENCODER_DT  26
#define ENCODER_SW  25 // Reusing your old button pin for the encoder's button

// LEDs
const int numLEDs = 4;
int ledPins[numLEDs] = {15, 21, 22, 2};

// --- Objects ---
Adafruit_ST7735 tft = Adafruit_ST7735(TFT_CS, TFT_DC, TFT_RST);
DHT dht(DHTPIN, DHTTYPE);

// --- State Machine Variables ---
int currentMode = 0; 

// --- Encoder Variables ---
volatile int encoderCount = 0;       // Volatile because it changes inside the Interrupt
volatile int lastClkState = HIGH;
const int stepsPerPage = 10;         // 10 steps = half a cycle on a standard 20-step encoder

int buttonState = HIGH;             
int lastButtonState = HIGH;         
unsigned long lastDebounceTime = 0;
const unsigned long debounceDelay = 50; 

// --- Mode 0 (Weather) Variables ---
unsigned long lastWeatherUpdate = 0;
const int WEATHER_INTERVAL = 2000;
const int DARK_THRESHOLD = 1000;

// --- Mode 1 (Audio Graph) Variables ---
int xPos = 0;
const int CLAP_THRESHOLD = 400;     // Tweak this to change mic sensitivity!
unsigned long ledFlashTimer = 0;    
bool ledsFlashing = false;          

// --- Interrupt Service Routine for Encoder ---
// This runs instantly in the background whenever the dial is turned
void IRAM_ATTR updateEncoderISR() {
  int clkState = digitalRead(ENCODER_CLK);
  // Only trigger on the rising edge of the CLK signal
  if (clkState != lastClkState && clkState == HIGH) {
    if (digitalRead(ENCODER_DT) == LOW) {
      encoderCount++; // Clockwise
    } else {
      encoderCount--; // Counter-Clockwise
    }
  }
  lastClkState = clkState;
}

void setup() {
  Serial.begin(115200);

  // Initialize Encoder Pins
  pinMode(ENCODER_CLK, INPUT_PULLUP);
  pinMode(ENCODER_DT, INPUT_PULLUP);
  pinMode(ENCODER_SW, INPUT_PULLUP);

  // Attach the interrupt to the CLK pin
  attachInterrupt(digitalPinToInterrupt(ENCODER_CLK), updateEncoderISR, CHANGE);

  for (int i = 0; i < numLEDs; i++) {
    pinMode(ledPins[i], OUTPUT);
    digitalWrite(ledPins[i], LOW);
  }

  dht.begin();
  tft.initR(INITR_BLACKTAB); 
  tft.setRotation(1); 
  tft.fillScreen(ST77XX_BLACK);
  
  drawWeatherUI();
}

void loop() {
  // ---------------------------------------------------------
  // 1. PAGE NAVIGATION CHECK (Spinning or Clicking)
  // ---------------------------------------------------------
  bool changePageTriggered = false;

  // Check 1: Did we spin a half cycle (10 steps in either direction)?
  if (abs(encoderCount) >= stepsPerPage) {
    encoderCount = 0; // Reset counter for the next half-cycle
    changePageTriggered = true;
  }

  // Check 2: Did we click the encoder button down?
  int reading = digitalRead(ENCODER_SW);
  if (reading != lastButtonState) {
    lastDebounceTime = millis();
  }
  if ((millis() - lastDebounceTime) > debounceDelay) {
    if (reading != buttonState) {
      buttonState = reading;
      if (buttonState == LOW) {
        changePageTriggered = true;
      }
    }
  }
  lastButtonState = reading;

  // If either action happened, swap the page
  if (changePageTriggered) {
    currentMode = (currentMode == 0) ? 1 : 0;
    tft.fillScreen(ST77XX_BLACK);
    
    if (currentMode == 0) {
      drawWeatherUI();
      lastWeatherUpdate = 0; 
    } else if (currentMode == 1) {
      for (int i = 0; i < numLEDs; i++) digitalWrite(ledPins[i], LOW);
      xPos = 0; 
      ledsFlashing = false; 
    }
  }

  // ---------------------------------------------------------
  // 2. RUN THE ACTIVE MODE
  // ---------------------------------------------------------
  if (currentMode == 0) {
    runWeatherMode();
  } else if (currentMode == 1) {
    runAudioMode();
  }
}

// --- Helper Functions ---

void drawWeatherUI() {
  tft.setTextColor(ST77XX_CYAN);
  tft.setTextSize(2);
  tft.setCursor(10, 10);
  tft.print("Sensor Dash");
  
  tft.setTextColor(ST77XX_WHITE);
  tft.setTextSize(1);
  tft.setCursor(10, 50);
  tft.print("Temp:");
  tft.setCursor(10, 80);
  tft.print("Humidity:");
  tft.setCursor(10, 110);
  tft.print("Light Lvl:");
}

void runWeatherMode() {
  int lightLevel = analogRead(LDR_PIN);
  bool isDark = (lightLevel < DARK_THRESHOLD);
  for (int i = 0; i < numLEDs; i++) {
    digitalWrite(ledPins[i], isDark ? HIGH : LOW);
  }

  if (millis() - lastWeatherUpdate > WEATHER_INTERVAL) {
    lastWeatherUpdate = millis(); 

    float h = dht.readHumidity();
    float t = dht.readTemperature(); 

    tft.fillRect(70, 50, 80, 15, ST77XX_BLACK);
    tft.setCursor(70, 50);
    if (isnan(t)) tft.print("Err"); else { tft.print(t); tft.print(" C"); }

    tft.fillRect(70, 80, 80, 15, ST77XX_BLACK);
    tft.setCursor(70, 80);
    if (isnan(h)) tft.print("Err"); else { tft.print(h); tft.print(" %"); }

    tft.fillRect(80, 110, 50, 15, ST77XX_BLACK);
    tft.setCursor(80, 110);
    tft.print(lightLevel);
  }
}

void runAudioMode() {
  int maxVal = 0;
  int minVal = 4095; 

  for(int i = 0; i < 20; i++) {
    int val = analogRead(MIC_PIN);
    if (val > maxVal) maxVal = val;
    if (val < minVal) minVal = val;
  }

  int noiseLevel = maxVal - minVal;

  int yPos = map(maxVal, 0, 4095, 120, 10);
  tft.drawPixel(xPos, yPos, ST77XX_GREEN);
  xPos++;

  if (xPos >= 160) { 
    xPos = 0;
    tft.fillScreen(ST77XX_BLACK); 
  }
  
  if (noiseLevel > CLAP_THRESHOLD) {
    for (int i = 0; i < numLEDs; i++) digitalWrite(ledPins[i], HIGH);
    ledsFlashing = true;
    ledFlashTimer = millis(); 
  }

  if (ledsFlashing && (millis() - ledFlashTimer > 100)) {
    for (int i = 0; i < numLEDs; i++) digitalWrite(ledPins[i], LOW);
    ledsFlashing = false;
  }
  
  delay(5); 
}
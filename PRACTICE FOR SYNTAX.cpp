#include <Arduino.h>

#define STATUS_LED      LED_BUILTIN

#define PROBE_LOW       3
#define PROBE_MID       4
#define PROBE_HIGH      5

#define STROBE_YELLOW   6
#define STROBE_ORANGE   7
#define STROBE_RED      8
#define SIREN_PIN       9
#define designation = (road || river);


int Interval = 1000; // Interval for blinking in milliseconds
unsigned long telemetry = 0;
signed long telemetryInterval  = 60000; // 1 minute

// ============================================
// BLINKING STATE VARIABLES
// ============================================
unsigned long lastBlinkTime = 0;
bool blinkState = false;
unsigned long lastSirenToggle = 0;
bool sirenState = false;


void mqttsend() {Serial.println("MQTT send function called.");
}  // Placeholder for MQTT send function



// ============================================
// SETUP
// ============================================
void setup() {
  pinMode(STATUS_LED, OUTPUT);
  Serial.begin(115200);
  
  // Use INPUT_PULLUP so probes read LOW when wet (connected to ground)
  pinMode(PROBE_LOW, INPUT_PULLUP);
  pinMode(PROBE_MID, INPUT_PULLUP);
  pinMode(PROBE_HIGH, INPUT_PULLUP);
  
  pinMode(STROBE_YELLOW, OUTPUT);
  pinMode(STROBE_ORANGE, OUTPUT);
  pinMode(STROBE_RED, OUTPUT);
  pinMode(SIREN_PIN, OUTPUT);
  
  // Initial state - all OFF
  digitalWrite(STROBE_YELLOW, LOW);
  digitalWrite(STROBE_ORANGE, LOW);
  digitalWrite(STROBE_RED, LOW);
  digitalWrite(SIREN_PIN, LOW);
  digitalWrite(STATUS_LED, LOW);
}

// ============================================
// ROAD LEVEL FUNCTIONS
// ============================================

// Level 1 - Yellow: Blink yellow slowly
void RoadL1() {
  unsigned long now = millis();
  
  // Blink every 1 second
  if (now - lastBlinkTime >= 1000) {
    lastBlinkTime = now;
    blinkState = !blinkState;
    digitalWrite(STROBE_YELLOW, blinkState ? HIGH : LOW);
  }
  
  digitalWrite(STROBE_ORANGE, LOW);
  digitalWrite(STROBE_RED, LOW);
  digitalWrite(SIREN_PIN, LOW);
}

// Level 2 - Orange: Blink orange slowly
void RoadL2() {
  unsigned long now = millis();
  
  // Blink every 1 second
  if (now - lastBlinkTime >= 1000) {
    lastBlinkTime = now;
    blinkState = !blinkState;
    digitalWrite(STROBE_ORANGE, blinkState ? HIGH : LOW);
  }
  
  digitalWrite(STROBE_YELLOW, LOW);
  digitalWrite(STROBE_RED, LOW);
  digitalWrite(SIREN_PIN, LOW);
}

// Level 3 - Red: Both yellow and orange flash alternately with siren
void RoadL3() {
  unsigned long now = millis();
  
  // Strobe: Yellow and Orange alternate every 500ms
  if (now - lastBlinkTime >= 500) {
    lastBlinkTime = now;
    blinkState = !blinkState;
    
    if (blinkState) {
      digitalWrite(STROBE_YELLOW, HIGH);
      digitalWrite(STROBE_ORANGE, LOW);
    } else {
      digitalWrite(STROBE_YELLOW, LOW);
      digitalWrite(STROBE_ORANGE, HIGH);
    }
    digitalWrite(STROBE_RED, LOW);
  }
  
  // Siren: Beep pattern (1 second on, 1 second off)
  if (now - lastSirenToggle >= 1000) {
    lastSirenToggle = now;
    sirenState = !sirenState;
    digitalWrite(SIREN_PIN, sirenState ? HIGH : LOW);
  }
}

// No alert - Everything OFF
void NoAlert() {
  digitalWrite(STROBE_YELLOW, LOW);
  digitalWrite(STROBE_ORANGE, LOW);
  digitalWrite(STROBE_RED, LOW);
  digitalWrite(SIREN_PIN, LOW);
  digitalWrite(STATUS_LED, LOW);
}

// ============================================
// MAIN LOOP
// ============================================
void loop() {

  // Check if it's time to send telemetry
  if (millis() - telemetry >= telemetryInterval) {
    telemetry = millis();
    mqttsend();
  }

if ( designation == road ){    
  // Read probes - LOW = WET (probe is triggered)
  bool lowLevel = (digitalRead(PROBE_LOW) == LOW);
  bool midLevel = (digitalRead(PROBE_MID) == LOW);
  bool highLevel = (digitalRead(PROBE_HIGH) == LOW);
  
  // Priority: HIGHEST probe takes precedence
  if (highLevel) {
    // Level 3 - RED alert
    RoadL3();
    mqttsend();
  } 
  else if (midLevel) {
    // Level 2 - ORANGE alert
    RoadL2();
    mqttsend();
  } 
  else if (lowLevel) {
    // Level 1 - YELLOW alert
    RoadL1();
    mqttsend();
  } 
  else {
    // No alert
    NoAlert();
    mqttsend();

  }
}


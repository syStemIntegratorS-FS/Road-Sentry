#include <Arduino.h>
const int ledPin = 13; // Pin connected to the LED
const int buttonPin = 2; // Pin connected to the button


void ledOn() {
  digitalWrite(ledPin, HIGH); // Turn the LED on
}

void ledOff() {
  digitalWrite(ledPin, LOW); // Turn the LED off
}

void setup() {
  pinMode(ledPin, OUTPUT); // Set the LED pin as an output
  pinMode(buttonPin, INPUT_PULLUP); // Set the button pin as an input with pull-up resistor
}

void loop() {
  if (digitalRead(buttonPin) == LOW) { // Check if the button is pressed
    ledOn(); // Turn the LED on
  } else {
    ledOff(); // Turn the LED off
  }
}
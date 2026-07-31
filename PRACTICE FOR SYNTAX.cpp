  #include <Arduino.h>
  #include <WiFi.h>
  #include <FastLED.h>
  const char ssid[] = "AP";
  const char password[] = "12345678";

  #define RGB_LED_PIN 48
  #define NUM_LEDS 1
  #define LED_TYPE WS2812B
  #define COLOR_ORDER RGB
  #define RGB_BRIGHTNESS 30


  void setRGB(CRGB color)
  {
    FastLED.addLeds<LED_TYPE, RGB_LED_PIN, COLOR_ORDER>(leds, NUM_LEDS);
    FastLED.setBrightness(RGB_BRIGHTNESS);
    leds[0] = color;
    FastLED.show();
  }


  String getIP() {
    if (WiFi.status() == WL_CONNECTED) {
      return WiFi.localIP().toString();
    }
    return "Not Connected";
  }

  void setup() {
    Serial.begin(115200);
    while (!Serial) {

    }
    
    Serial.println("Hello, World!");
    WiFi.begin(ssid, password);
  }

  void loop() {
    if (WiFi.status() != WL_CONNECTED) {
      Serial.print("."); // Print dots while connecting
      delay(500);
    } else {
      Serial.println("\nConnected to WiFi!");
      
      // Get and print the IP
      Serial.print("IP Address: ");
      Serial.println(getIP());
      
      // Optional: Print TX Power (in dBm * 4, usually)
      // Note: getTxPower() returns float in some cores, int in others.
      // It usually returns the power in 0.25 dBm steps.
      float power = WiFi.getTxPower();
      Serial.print("WiFi TX Power: ");
      Serial.println(power);

      // Add a delay to avoid flooding the serial monitor
      delay(5000); 
    }
  }
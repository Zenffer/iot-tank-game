#include <Adafruit_NeoPixel.h>

#define LED_PIN    5    // Change to your LED data pin
#define LED_COUNT 10    // Number of LEDs in the strip or module

Adafruit_NeoPixel strip(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ800);

void setup() {
  strip.begin();
  strip.show();          // Initialize all pixels to 'off'
  setStaticBlue();       // Set a static blue color at startup
}

void loop() {
  // Keep the LED static blue.
  // If you want a different animation, uncomment one of the options below.
  
  // blinkBlue();
  // pulseBlue();
  // rainbowCycle();
  // theaterChaseBlue();
  
  delay(100);
}

void setStaticBlue() {
  strip.fill(strip.Color(0, 0, 255));
  strip.show();
}

// Optional animation examples:

void blinkBlue() {
  strip.fill(strip.Color(0, 0, 255));
  strip.show();
  delay(500);
  strip.clear();
  strip.show();
  delay(500);
}

void pulseBlue() {
  for (int brightness = 0; brightness <= 255; brightness += 5) {
    strip.fill(strip.Color(0, 0, brightness));
    strip.show();
    delay(20);
  }
  for (int brightness = 255; brightness >= 0; brightness -= 5) {
    strip.fill(strip.Color(0, 0, brightness));
    strip.show();
    delay(20);
  }
}

void rainbowCycle() {
  for (int j = 0; j < 256; j++) {
    for (int i = 0; i < strip.numPixels(); i++) {
      strip.setPixelColor(i, strip.ColorHSV((i * 256 / strip.numPixels() + j) & 0xFF));
    }
    strip.show();
    delay(20);
  }
}

void theaterChaseBlue() {
  for (int a = 0; a < 10; a++) {
    for (int b = 0; b < 3; b++) {
      strip.clear();
      for (int c = b; c < strip.numPixels(); c += 3) {
        strip.setPixelColor(c, strip.Color(0, 0, 255));
      }
      strip.show();
      delay(100);
    }
  }
}

#include <Arduino.h>
#include <Adafruit_NeoPixel.h>
#include "neo_pixel.h"

#define LED_COUNT  1

Adafruit_NeoPixel neo(LED_COUNT, NEO_PIN, NEO_GRB + NEO_KHZ800);

void update_neo_pixel(int dev) {
    if ((dev) > 2758) {
          neo.setPixelColor(0,25,0,0);    // RED for overdeviation
          }
      else
      if ((dev) < 1854) {
          neo.setPixelColor(0,0,0,50);    // BLUE for underdeviation
          }
      else
          neo.setPixelColor(0,0,25,0);    // GREEN for overdeviation
      neo.show();
}

void init_neo_pixel() {
    pinMode(NEO_PIN, OUTPUT);
    // initialize the neo pixel
    neo.begin();
    neo.setPixelColor(0,0,0,0);
    neo.show();
}
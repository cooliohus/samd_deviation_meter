#include <Arduino.h>
#include "LedController.hpp"
#include <SPI.h>              // SPI bus drivers

void udate_8digit_display(float maxDev, float avgDC);
void init_8digit_display();
#include <Arduino.h>
#include <Wire.h>             // I2C bus drivers
#include <Adafruit_GFX.h>     // Graphics library
#include <Adafruit_SSD1306.h> // Mono OLED driver
#include "oled_mono.h"


#define SCREEN_WIDTH 128      // OLED display width, in pixels
#define SCREEN_HEIGHT 64      // OLED display height, in pixels
#define OLED_RESET     -1     // Reset pin # (or -1 if sharing Feather reset pin)
#define SCREEN_ADDRESS 0x3C   // Current I2C address of Amazon OLED displays
#define TEXT_HEADER_SIZE  1   // Size of heading text to display
#define VALUE_TEXT_SIZE  2    // Size of deviation data value text to display

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET); // Paramter setup for OLED device

char buff1[9];

void update_oled_mono(float maxDev, float avgDC) {
// All the display commands below just populate the display buffer
 // Nothing diaplays until the display.display() command is executed

      //sprintf(buff1,"%04d",int(((winMax) * ADC_SCALE / ADC_BITS +.0005)*1000));  // Assemble print buffer for the -> TM-V71
      sprintf(buff1,"%04d",int((maxDev+0.0005)*1000));  // Assemble print buffer for the -> TM-V71

      #ifdef SERIAL_DEBUG // Print to console the value of the deviation level      
      Serial.print(F("Deviation:  "));
      Serial.print(buff1[0]);
      Serial.print(buff1[1]);
      Serial.print(buff1[2]);
      Serial.print(buff1[3]);
      Serial.print(F("   "));
      #endif

      display.setTextSize(VALUE_TEXT_SIZE); // Change font size to 2x for deviation value display
      display.setCursor(26,22);  // Position deviation value to look centered on screen with units following

      // Print the deviation level to the display buffer
      display.print(buff1[0]);
      display.print(buff1[1]);
      display.print(buff1[2]);
      display.print(buff1[3]);

      display.setTextSize(TEXT_HEADER_SIZE); // Change font size to 1x for average value display
      display.setCursor(44,52);  // Position deviation value to look centered on screen with units following

      //sprintf(buff1,"%3d",int(((winAccumulator / winCount) * 3.3 / ADC_BITS +.005)*100));  // Assemble print buffer
      sprintf(buff1,"%3d",int((avgDC+0.005)*100));  // Assemble print buffer

      #ifdef SERIAL_DEBUG // Print to console the value of the signal average level
      Serial.print(F("Average:  "));
      Serial.print(buff1[0]);
      Serial.print(F("."));
      Serial.print(buff1[1]);
      Serial.print(buff1[2]);
      Serial.println(F(" "));
      #endif

      // Print the signal average level to the display buffer
      display.print(buff1[0]);
      display.print(".");
      display.print(buff1[1]);
      display.print(buff1[2]);

      display.display();  // Send buffer to display unit  
}


void init_oled_mono(){
/*****************************************************************************
 * The following Set up the Monochrome OLED with a basic screen              *
 * including a frame and units headings for the deviation and                *
 *  for the average level.                                                   *
 * NOTES: - all commands below will only populate the display buffer         *
 *          nothing displays until the display.display() command is executed *
 *        - it is important to specify the TextColor                         *
 *          with (foreground,background)values. Otherwise, the background is *
 *          transparent and the screen clutters up as values change          *
 *****************************************************************************/

      display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS);  // Initialize OLED interface
      display.setTextColor(WHITE,BLACK);  // Currently only have a monochrome display
      display.clearDisplay(); // Start out with a cleared buffer

      display.drawRoundRect(0, 0, 127, 64, 4, WHITE); // Put up a pretty border around the screen...
  
      display.setCursor(34,6);  // "Deviation" header text centered at top of screen
      display.setTextSize(TEXT_HEADER_SIZE);  // Set up for smaller font for text header
      display.println(F("Deviation:")); // Display header for deviation value

      display.setTextSize(VALUE_TEXT_SIZE); // Change font size to 2x for deviation value display
      display.setCursor(78,22);  // Position to display units (Hz.) after deviation value
      display.print("Hz"); // Display units (Hz)

      display.setTextSize(TEXT_HEADER_SIZE);  // Set up for smaller font
      display.setCursor(44,42);  // Position to display average value header
      display.println(F("Average:")); // Display DC average value header
      display.setCursor(70,52);  // Position to display units
      display.println(F("VDC"));  // Display units (VDC)
 
      display.display();  // Send buffer to display unit
}
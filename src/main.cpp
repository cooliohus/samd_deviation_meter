/********************************************************************
 * TITLE: K3JSE, WA3NOA  Deviation monitor                          *
 *                                                                  *
 * VERSION: 05-31-2024-15:26                                        *
 *  -  WA3NOA added OLED code                                       *
 *                                                                  *
 * VERSION: 06-02-2024-14:33                                        *
 *  - Major rework of the code including                            * 
 *  - Made many variable names more descriptive                     *
 *  - Moved some stuff into function                                *
 *  - Added some constant definition for better maintenance         *
 *  - Now using "max deviation" value rather than average which     *
 *    adds some display volatility                                  *
 *                                                                  *
 * VERSION: 06-02-2024-16:16                                        *
 *  - Refined sliding window code                                   *
 *                                                                  *
 * VERSION: 06-02-2024-22:00                                        *
 *  - Removed period after Hz, typo cleanup                         *
 *                                                                  *
 * VERSION: 06032024-1300                                           *
 *  - added "dual input" feature.  If PIN9 / gpio PA07 is high or   *
 *    floating the scaling factor is set for primary (TM-V71)       *
 *    If the pin is pulled low then an alternate ADC_SCALE factor   *
 *    is used e.g. scanner                                          *
 *  - removed serial line ready check so code won't hang if serial  *
 *    port isn't active                                             *
 *                                                                  *
 ********************************************************************/

#include "andy.h"
//#include "jim.h"

#define SERIAL_DEBUG
//#define USE_NEO_PIXEL
//#define USE_8DIGIT_DISPLAY
#define USE_OLED_MONO // If OLED MONO connected to I2C bus

#include <Arduino.h>

#ifdef USE_NEOPIXEL
#include <Adafruit_NeoPixel.h>
#define NEO_PIN NEOPIXEL_BUILTIN 
#endif

#ifdef USE_8DIGIT_DISPLAY
#include "LedController.hpp"
#include <SPI.h>              // SPI bus drivers
#endif

#ifdef USE_OLED_MONO
#include <Wire.h>             // I2C bus drivers
#include <Adafruit_GFX.h>     // Graphics library
#include <Adafruit_SSD1306.h> // Mono OLED driver
#endif

 
#define LED_COUNT  1

#define SAMPLE_WINDOW 750     // This no longer determines the display update rate
                              // Display update is now a function of the sample window AND
                              // the [experimental] sliding average window code in the main loop

#define SL_WINDOW 64          // number of sample_window values to average for display                          

#define SCREEN_WIDTH 128      // OLED display width, in pixels
#define SCREEN_HEIGHT 64      // OLED display height, in pixels
#define OLED_RESET     -1     // Reset pin # (or -1 if sharing Feather reset pin)
#define SCREEN_ADDRESS 0x3C   // Current I2C address of Amazon OLED displays
#define TEXT_HEADER_SIZE  1   // Size of heading text to display
#define VALUE_TEXT_SIZE  2    // Size of deviation data value text to display

#define JITTER 3

/******** F o r w a r d   f u n c t i o n   d e c l a r a t i o n s ********/
void init_adc();
#ifdef USE_8DIGIT_DISPLAY
void udate_8digit_display();
void init_8digit_display();
#endif
#ifdef USE_NEO_PIXEL
void update_neo_pixel();
void init_neo_pixel();
#endif
#ifdef USE_OLED_MONO
void update_oled_mono();
void init_oled_mono();
#endif
#ifdef SERIAL_DEBUG
void update_serial();
#endif
/**************************************************************************/  

#ifdef USE_8DIGIT_DISPLAY
void scrollDigits();
#endif

// Object / library declarations

#ifdef USE_NEO_PIXEL
Adafruit_NeoPixel neo(LED_COUNT, NEO_PIN, NEO_GRB + NEO_KHZ800);
#endif

#ifdef USE_8DIGIT_DISPLAY
LedController lc;
#endif

#ifdef  USE_OLED_MONO // Check if OLED is to be used for display
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET); // Paramter setup for OLED device
#endif

/* ***********************
 * Variable declarations *
**************************/

/*
 * A note on variable names
 *   - Variables starting with an underscore are working variables for use inside the ADC ISR
 *   - The same variable mame without the underscore is the saved value for use outside the ISR
*/

volatile uint16_t _adcResult; 

uint32_t wfAccumulator = 0;
uint32_t _wfAccumulator = 0;
int wfCount = 0;              
int _wfCount = 0;

uint32_t winAccumulator = 0;   // sum of ADC values during sample window
uint32_t _winAccumulator = 0;  //   working value in ADC ISR
int winCount = 0;              // Count of ADC samples during sample window
int _winCount = 0;             //   working value in ADC ISR


int wfMax = 0;
int _wfMax = 0;
int wfMin = 0;
int _wfMin = 0;
int _adcMax,
    adcMax,
    _adcMin,
    adcMin;

int winMax,         // maximum ADC value during sample window
    _winMax,        //   ADC ISR working winMax
    winMin,         // minimum ADC value during sample window
    _winMin;        //   ADC ISR working winMin 

int _wfDif;         // waveform peak difference

float  winMaxDev,   // sample window deviation
       winAvgDC;    // sample window average DC value

int led = LED_BUILTIN;

bool _rising = true;
int _crossings = 0;

int sl_buf[SL_WINDOW];
int sl_ptr = 0;
float sl_avg = 0;


// Used by display routines

volatile bool displayReady = false;  
//float maxVolts, minVolts;
float ADC_SCALE;
char buff1[9];

unsigned long delaytime=250;

void setup() {

    pinMode(5,OUTPUT);         // Pin 5 / GPIO PA15 track display update
    pinMode(6,OUTPUT);         // Pin 6 / GPIO PA20 track ISR
    pinMode (9,INPUT_PULLUP);  // Controls an option e.g. scale factor
    pinMode(led, OUTPUT);

    #ifdef SERIAL_DEBUG
    Serial.begin(115200);      // Start the native USB port
    //while(!Serial)
    //  {};                    // Wait for the console to open
    #endif

    #ifdef USE_OLED_MONO
    init_oled_mono();
    #endif

    #ifdef USE_8DIGIT_DISPLAY
    init_8digit_display();
    #endif

    #ifdef USE_NEO_PIXEL
    init_neo_pixel();
    #endif


    // Initialize the sliding window buffer to "some value"
    for (int i=1;i <SL_WINDOW;i++){
      sl_buf[i] = 2048;
    }

    init_adc();
}



void loop() {

  /*
   * This is my attempt to explain [to myself] how things work.  Note that the first
   * goal is to measure digital mode constant / tune tones at 1500 Hz.  The tune tone
   * may be modulated by a PL tone which the software must be accounted for.  The
   * hardware partially filters PL tones to keep the amplitide within range.
   *
   * Sampling is broken into two time periods 
   *   - waveform
   *   - window
   * 
   * The waveform sample period attempts to idenify "peaks and valleys" by looking for
   * changes in direction i.e. are the ADC samples increasing or decreasing and then
   * counting the direction changes.  Three direction changes should constitute one
   * full waveform.
   * 
   * The window sample period is longer and determines the display update rate.  The
   * goal is to set the diplay update 
   * 
   * Window period related variables include
   * 
   *   - winAccumulator     // sum of all ADC samples during the sample window
   *   - winCount           // count of all ADC samples during the sample window
   *                        // averaging winAccumulator creates the average offset
   *                        // during th esample windows which should be (design goal)
   *                        // in the vicinity of Vcc / 2 or 1.65 volts
   * 
   *   - wfAccumulator      // Sum of all waveform peak to peak (wfADCmax - wfADCmin)
   *                        // values during the window period
   *   - wfCount            // count of wfAccumulator values
   *                        // Averaging wfAccumulator creates the average peak to peak value
   *                        // during the sample window
   *  
   *   - wfMax, wfMin       // Maximum and minimum peak to peak waveform value during thw window
   *                        //  period. The wfAccumulator average and wfMax values shoul dbe close for
   *                        // a 1500 Hz tune signal but the wfAccumulator average will be
   *                        // lower (misleadig) during voice or data transmissions
   *   - adcMin, adcMax     // minimum and maximum ADC values during sample window
   *
   *   - winMaxDev          // maximum deviation during sample window
   * 
   *   - winAvgDC           // average DC during sample window
  */
   if (displayReady) {
      PORT->Group[0].OUTSET.reg = PORT_PA15;      // Set Arduino pin 5 high for scope trigger
      

      /*
       * the following code attempts to implement a sliding window average, I need to 
       * think carefully about this one :-)
       * 
       * Note that this brute force code takes many display updates to initialize, will 
       * fix that later.
       * 
       * The code maintains a sliding average of SL_WINDOW samples and updates the display
       * each time the buffer fills... this is not exactly what I want but its an OK beginning!
       * 
      */
      sl_buf[sl_ptr] = winMax;
      sl_ptr++;

      if (sl_ptr > SL_WINDOW-1)  {
          sl_ptr = 0;
      
          sl_avg = 0;
          for (int i=0;i < SL_WINDOW;i++) {
              sl_avg += sl_buf[i];
          }
          sl_avg = sl_avg / SL_WINDOW;
           
          /*******************************************************************
           *  Use Arduino PIN9 / SAMD GPIO PA07 to control deviation scaling
           *   - pin is configued as input with pullup so it 'floats" high
           *     High value sets ADC_SCALE for TM-V71
           *   - jumper the pin to GND to select an alternate input source
           *     e.g pin low value is curently configured for Andy's scanner
          *******************************************************************/ 
          if (PORT->Group[0].IN.reg & PORT_PA07)    // Arduino PiIN9 floating / high
              ADC_SCALE = ADC_HIGH;                    // scale factor for Andy's TM-V71
          else
              ADC_SCALE = ADC_LOW;                    // scale factor for Andy's scanner

          winMaxDev = sl_avg * ADC_SCALE / 4096;
          winAvgDC = (winAccumulator / winCount) * 3.3 / 4096;

          #ifdef SERIAL_DEBUG
          update_serial();
          #endif
      
          #ifdef USE_8DIGIT_DISPLAY
          update_8digit_display();
          #endif

          #ifdef USE_OLED_MONO 
          update_oled_mono();
          #endif
       
          #ifdef USE_NEO_PIXEL
          update_neo_pixel();
          #endif
       }  // end of sliding window code
      
      displayReady = false;                      // Clear the resultsReady flag     

      //PORT->Group[0].OUTCLR.reg = PORT_PA15;
      PORT->Group[0].OUTTGL.reg = PORT_PA15;    // Togle Arduini Pin 5 (GPIO PA15) off
      //PORT->Group[0].OUTCLR.reg = PORT_PA20;    // reset interupt monitor
  }
}

#ifdef USE_8DIGIT_DIPLAY
void udate_8digit_display() {
//sprintf(buff1,"%3d",int(((saveMax-saveMin) * 3.3 / 4096 +.005)*100));
      sprintf(buff1,"%04d",int(((dev_sav / tranSave) * ADC_SCALE / 4096 +.0005)*1000));  // TM-V71
      //sprintf(buff1,"%04d",int(((dev_sav / tranSave) * 10.35 / 4096 +.0005)*1000));   // IC-706
      lc.setChar(0,7,buff1[0],true);
      lc.setChar(0,6,buff1[1],false);
      lc.setChar(0,5,buff1[2],false);
      lc.setChar(0,4,buff1[3],false);

      sprintf(buff1,"%3d",int(((accumulatorSave / SAMPLE_NO) * 3.3 / 4096 +.005)*100));
      //lc.setChar(0,3,buff1[0],true);
      lc.setChar(0,2,buff1[0],true);
      lc.setChar(0,1,buff1[1],false);
      lc.setChar(0,0,buff1[2],false);
}
#endif

#ifdef USE_NEOPIXEL
void update_neo_pixel() {
    if ((savewfMax-savewfMin) > 2758) {
          neo.setPixelColor(0,25,0,0);    // RED for overdeviation
          }
      else
      if ((savewfMax-savewfMin) < 1854) {
          neo.setPixelColor(0,0,0,50);    // BLUE for underdeviation
          }
      else
          neo.setPixelColor(0,0,50,0);    // GREEN for overdeviation
      neo.show();
}
#endif

#ifdef USE_OLED_MONO 
void update_oled_mono() {
// All the display commands below just populate the display buffer
 // Nothing diaplays until the display.display() command is executed

      //sprintf(buff1,"%04d",int(((winMax) * ADC_SCALE / 4096 +.0005)*1000));  // Assemble print buffer for the -> TM-V71
      sprintf(buff1,"%04d",int((winMaxDev+0.0005)*1000));  // Assemble print buffer for the -> TM-V71

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

      //sprintf(buff1,"%3d",int(((winAccumulator / winCount) * 3.3 / 4096 +.005)*100));  // Assemble print buffer
      sprintf(buff1,"%3d",int((winAvgDC+0.005)*100));  // Assemble print buffer

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
#endif

#ifdef SERIAL_DEBUG
void update_serial() {
    Serial.print("adcMin/Max ");Serial.print(adcMin); Serial.print("  "); Serial.print(adcMax);
    Serial.print("  adcDiff ");Serial.print(adcMax-adcMin);
    Serial.print("  winMax/Min ");Serial.print(winMax); Serial.print("  "); Serial.print(winMin);
    Serial.print("  winDiff ");Serial.print(winMax-winMin);
    Serial.print("  Vavg: "); Serial.print(winAvgDC, 4);
    Serial.print(F("  Vpp: "));
    Serial.print(F("  Max Dev: ")); Serial.println(winMaxDev, 4);
}
#endif

void ADC_Handler() {
  /*
    Things I would like the ADC interrupt handler to do....
    - check for "zero crossings" to find complete waveform cycles then record the max and min
      values for a defined number of cycles.  This should minimize inflated values resulting from 
      DC drift caused by PL tone or other stuff?
    - Calculate the waveform sample peak to peak value.  
    - store and update the maximum P2P value.  This value can be used by the display routines 
      to show maximum deviation in the display window
    - Store and add to a P2P sum value and count how increment how many samples are in the summ.
      These values can be used by the display routines to show the average devition during the
      display window.
    - Kep rack of the median waveforn sample values to enable display routines to show the average
      DC offset during each sample window - should be about half Vcc (for a tune signal)

    Conceptually there are a few complete waveforms in a waveform cycle and many waveform cycles
    in a sample window.  Sample window size determines how responsive th edisplay is e.g. 1/2 second,
    one second, etc.

    Clear as mud!
  
  */
  if (ADC->INTFLAG.bit.RESRDY) {                      // An ADC sample is available
  
      //PORT->Group[0].OUTSET.reg = PORT_PA20;          // set Arduino PIN 6 on for monitoring (scope)
       if (_winCount == 0) PORT->Group[0].OUTSET.reg = PORT_PA20;          // set Arduino PIN 6 on for monitoring (scope)

      _adcResult = ADC->RESULT.reg;                    // store the ADC result;

      _winAccumulator += _adcResult;
      _winCount++;                                    // signal display routines when defined value reached
                                                      // This will determine display update rate
                                                 

      /*
       * keep track of maximum and minimum ADC values during the sample window in _ variables
       * When the sample count reaches yhe sample period the _ values are saved to the non _
       * variables for main loop processing and then reset
      */    
      if (_adcResult > _adcMax)
          _adcMax = _adcResult;
      else if (_adcResult < _adcMin)
          _adcMin = _adcResult;

      /*
       * process ADC samples to look for waveform max and min values and setermine if the waveform
       * is rising or falling.  When the direction changes increment the _crossings variable, which
       * indicates a "virtual" zero crossing.  Three crossings should indicate an entire waveform
       *  has been found
      */
      if (_rising) {
          if (_adcResult > _wfMax)
              _wfMax = _adcResult;
          else if (_wfMax - _adcResult > JITTER) {
              _rising = false;
              _crossings += 1;
              }
      }
      else {  // falling
          if (_adcResult < _wfMin)
          _wfMin = _adcResult;
      else if (_adcResult-_wfMin > JITTER) {
          _rising = true;
          _crossings += 1;
          }
      }

      /*
       * if the number of crossings exceeds three then a complete waveform has been found.
       * Procssing single waveforms is important as it minimizes the affect of PL tone
       * voltage offsets
       * 
       * Also, update _wfAccumulator for calculating the sample window average peak to peak value
      */
      if (_crossings > 3) {                   // at least one complete waveform
          wfMax = _wfMax;                     // save the working value
          wfMin = _wfMin;                     // save the working value
          _wfMin = wfMax;                     // reset the working value to the opposite peak
          _wfMax = wfMin;                     // reset the working value to the opposite peak
          _wfDif = wfMax-wfMin;
          _wfAccumulator += _wfDif;    // update the accumulated peak to peak values and counter
          _wfCount++;
          if (_wfDif > _winMax)
              _winMax = _wfDif;
          else if (_wfDif < _winMin)
              _winMin = _wfDif;
          _crossings = 0;                     // reset to start looking for the next waveform
     }
    
      /*
       * The ADC sample counter _winCount has reached the sample window value
       * save in process values and reset working values for the start of the
       * next sample window
      */
      if (_winCount >= SAMPLE_WINDOW)  {    // ADC samples equal the window size
            
          winAccumulator = _winAccumulator;
          winCount = _winCount;
          _winAccumulator = 0;
          _winCount = 0;
      
          wfAccumulator = _wfAccumulator;
          wfCount = _wfCount;
          _wfAccumulator = 0;
          _wfCount = 0;
          
          adcMax = _adcMax;
          adcMin = _adcMin;
          _adcMax = 0;
          _adcMin = 4095;

          winMax = _winMax;
          winMin = _winMin;
          _winMin = 4095;
          _winMax = 0;
          PORT->Group[0].OUTCLR.reg = PORT_PA20;
          displayReady = true;                         // Signal the display routines to run
      }
    
      ADC->INTFLAG.bit.RESRDY = 1;                     // Clear the RESRDY flag
      while(ADC->STATUS.bit.SYNCBUSY);                 // Wait for read synchronization
    
      //PORT->Group[0].OUTCLR.reg = PORT_PA20;
      //PORT->Group[0].OUTTGL.reg = PORT_PA20;           // Toggle Arduino Pin 6 off
  } // ADC sample available
}

void init_adc() {
  /*
    Notes:
      - some peripheral registers use an asyncronous clock hence the syncbusy wait
      - The default analog reference is half the supply voltage so the input is divided by two
      - ".bit" refers to bit fields within a 32 bit register
      - ".reg" referes to th eentirte 32 bit register
      - The reserved ADC ISR function is ADC_HANDLER which must be redefined.
      - The ADC is configured to free run and generate result ready intrrupts.  The clock divider,
        sample length, and sample number parameters result in approx 23K samples / second which is 
        more than good enough for a 1500 Hz tune tone
  */


    /*****************************************************************************************
     *
     * The following block of code reads the factory ADC calibration data and writes it to 
     * the ADC calibration registers.  I suspect this is redundant as the Arduino framework
     *  probably does this but better safe than sorry :-)
     * 
     * ***************************************************************************************/

    uint32_t bias = (*((uint32_t *) ADC_FUSES_BIASCAL_ADDR) & ADC_FUSES_BIASCAL_Msk) >> ADC_FUSES_BIASCAL_Pos;
    uint32_t linearity = (*((uint32_t *) ADC_FUSES_LINEARITY_0_ADDR) & ADC_FUSES_LINEARITY_0_Msk) >> ADC_FUSES_LINEARITY_0_Pos;
    linearity |= ((*((uint32_t *) ADC_FUSES_LINEARITY_1_ADDR) & ADC_FUSES_LINEARITY_1_Msk) >> ADC_FUSES_LINEARITY_1_Pos) << 5;

   /* Wait for bus synchronization. */
    while (ADC->STATUS.bit.SYNCBUSY) {};

    /* Write the calibration data. */
    ADC->CALIB.reg = ADC_CALIB_BIAS_CAL(bias) | ADC_CALIB_LINEARITY_CAL(linearity);
    
    /*******************************************************************************************/


    ADC->INPUTCTRL.bit.MUXPOS = 0x3;                   // Set the analog input to A2
    while(ADC->STATUS.bit.SYNCBUSY);                   // Wait for synchronization
    ADC->INPUTCTRL.bit.MUXNEG = 0x18;                  // Set the negative analog input to GND
    while(ADC->STATUS.bit.SYNCBUSY);                   // Wait for synchronization
    ADC->SAMPCTRL.bit.SAMPLEN = 3;                     // Set Sampling Time Length 
    ADC->CTRLB.reg = ADC_CTRLB_PRESCALER_DIV16 |       // Divide ADC GCLK by 8 (48MHz/8, 136 KHz)
                     ADC_CTRLB_RESSEL_12BIT |          // Set the ADC resolution to 12 bits
                     ADC_CTRLB_FREERUN;                // Set the ADC to free run
    while(ADC->STATUS.bit.SYNCBUSY);                   // Wait for synchronization  
    ADC->INPUTCTRL.bit.GAIN = ADC_INPUTCTRL_GAIN_DIV2_Val;
    while(ADC->STATUS.bit.SYNCBUSY);                   // Wait for synchronization  
    ADC->AVGCTRL.bit.SAMPLENUM = 2;                    // avrage 2 samples
    while(ADC->STATUS.bit.SYNCBUSY);                   // Wait for synchronization  
    ADC->AVGCTRL.bit.ADJRES = 2;
    while(ADC->STATUS.bit.SYNCBUSY);                   // Wait for synchronization  
 
    // Set the Nested Vector Interrupt Controller (NVIC) priority for the ADC to 0 (highest) 
    NVIC_SetPriority(ADC_IRQn, 0);
    // Connect the ADC to Nested Vector Interrupt Controller (NVIC)
    NVIC_EnableIRQ(ADC_IRQn);        
    
    ADC->INTENSET.reg = ADC_INTENSET_RESRDY;           // Generate interrupt on result ready (RESRDY)
     ADC->CTRLA.bit.ENABLE = 1;                         // Enable the ADC
    while(ADC->STATUS.bit.SYNCBUSY);                   // Wait for synchronization
    ADC->SWTRIG.bit.START = 1;                         // Initiate a software trigger to start an ADC conversion
    while(ADC->STATUS.bit.SYNCBUSY);                   // Wait for synchronization
  }

#ifdef USE_OLED_MONO
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
#endif

#ifdef USE_8DIGIT_DISPLAY
void init_8digit_display(){
    lc=LedController(12,11,10,4);
    lc.activateAllSegments();
    lc.setIntensity(10);        // Set the brightness to a medium values 
    lc.clearMatrix();           // and clear the display
    //scrollDigits();             // some start-up bling, show it's alive
    lc.clearMatrix();
}
#endif

#ifdef USE_NEO_PIXEL
void init_neo_pixel() {
    pinMode(NEO_PIN, OUTPUT);
    // initialize the neo pixel
    neo.begin();
    neo.setPixelColor(0,0,0,0);
    neo.show();
}
#endif
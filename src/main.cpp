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
 * VERSION: 06162024-2028                                           *
 *  - moved code around to multiple files for easier maintenance    *
 *                                                                  *
 ********************************************************************/


#include <Arduino.h>
#include "global_def.h"
#include "sam_adc.h"
#include "andy.h"
//#include "jim.h"


#ifdef USE_NEO_PIXEL
#include "neo_pixel.h"
#endif

#ifdef USE_8DIGIT_DISPLAY
#include "8digit.h"
#endif

#ifdef USE_OLED_MONO
#include "oled_mono.h"
#endif

#define SL_WINDOW 64          // number of sample_window values to average for display         


#ifdef USE_8DIGIT_DISPLAY
void udate_8digit_display();
void init_8digit_display();
#endif

#ifdef SERIAL_DEBUG
void update_serial();
#endif


float  winMaxDev,   // sample window deviation
       winDC,       // deviation P2P DC voltage
       winAvgDC;    // sample window average DC value

int led = LED_BUILTIN;

int sl_buf[SL_WINDOW];
int sl_ptr = 0;
float sl_avg = 0;

float ADC_SCALE;

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

    init_adc();  // Initialize the ADC.  See ADC constant in global_def.h

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
      //sl_buf[sl_ptr] = winMax;
      sl_buf[sl_ptr] = wfAccumulator / wfCount;
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

          winMaxDev = sl_avg * ADC_SCALE / ADC_BITS;
          winAvgDC = (winAccumulator / winCount) * 3.3 / ADC_BITS;
          winDC = sl_avg * 3.3 / ADC_BITS;

          #ifdef SERIAL_DEBUG
          update_serial();
          #endif
      
          #ifdef USE_8DIGIT_DISPLAY
          update_8digit_display(maxDev,avgDC);
          #endif

          #ifdef USE_OLED_MONO 
          update_oled_mono(winMaxDev,winAvgDC);
          #endif
       
          #ifdef USE_NEO_PIXEL
          update_neo_pixel(2561);
          #endif
       }  // end of sliding window code
      
      displayReady = false;                      // Clear the resultsReady flag     

      //PORT->Group[0].OUTCLR.reg = PORT_PA15;
      PORT->Group[0].OUTTGL.reg = PORT_PA15;    // Togle Arduini Pin 5 (GPIO PA15) off
      //PORT->Group[0].OUTCLR.reg = PORT_PA20;    // reset interupt monitor
  }
}  // end loop

#ifdef SERIAL_DEBUG
void update_serial() {
    Serial.print("adcMin/Max ");Serial.print(adcMin); Serial.print("  "); Serial.print(adcMax);
    Serial.print("  adcDiff ");Serial.print(adcMax-adcMin);
    Serial.print("  winMax/Min ");Serial.print(winMax); Serial.print("  "); Serial.print(winMin);
    Serial.print("  winDiff ");Serial.print(winMax-winMin);
    Serial.print("  Vavg: "); Serial.print(winAvgDC, 4);
    Serial.print(F("  Vpp: ")); Serial.print(winDC, 4);
    Serial.print(F("  Max Dev: ")); Serial.println(winMaxDev, 4);
    //Serial.print(F("  Max Dev: ")); Serial.println(sl_avg/ADC_BITS*3.3, 4);
}
#endif
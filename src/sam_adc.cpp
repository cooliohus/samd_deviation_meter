#include "sam_adc.h"
//nclude <Arduino.h>

#ifdef ADC_12BITS
int ADC_BITS = 4096;      // 12 bit ADC
#endif

#ifdef ADC_10BITS
int ADC_BITS = 1024;      // 10 bit ADC
#endif

#ifdef ADC_8BITS
int ADC_BITS = 256;       // 8 bit ADC
#endif


uint32_t wfAccumulator = 0;

int wfCount = 0;

uint32_t winAccumulator = 0;   // sum of ADC values during sample window

int winCount = 0;              // Count of ADC samples during sample window

int wfMax = 0;
int wfMin = ADC_BITS - 1;

int adcMax,
    adcMin;

int winMax,         // maximum ADC value during sample window
    winMin;         // minimum ADC value during sample window




volatile bool displayReady = false;


volatile uint16_t _adcResult; 
bool _rising = true;
int _crossings = 0;
int _wfDif;         // waveform peak difference

int _winMax,        //   ADC ISR working winMax
    _winMin;        //   ADC ISR working winMin 

int _wfMax = 0;

int _adcMax,
    _adcMin;

int _wfMin = 0;
int _wfCount = 0;
uint32_t _winAccumulator = 0;  //   working value in ADC ISR

uint32_t _wfAccumulator = 0;
int _winCount = 0;             //   working value in ADC ISR

#define JITTER 5

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
       if (_winCount == 0)
           PORT->Group[0].OUTSET.reg = PORT_PA20;          // set Arduino PIN 6 on for monitoring (scope)

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
          if (_wfMax > _winMax)
              _winMax = _wfMax;
          else if (_wfMin < _winMin)
              _winMin = _wfMin;
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
          _adcMin = ADC_BITS-1;

          winMax = _winMax;
          winMin = _winMin;
          _winMin = ADC_BITS-1;
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
    ADC->SAMPCTRL.bit.SAMPLEN = 4;                     // Set Sampling Time Length 
    ADC->CTRLB.reg = ADC_CTRLB_PRESCALER_DIV16 |       // Divide ADC GCLK by 8 (48MHz/8, 136 KHz)
                     ADC_CTRLB_RESSEL_12BIT |          // Set the ADC resolution to 12 bits
                     ADC_CTRLB_FREERUN;                // Set the ADC to free run

    #ifdef ADC_8BITS
    ADC->CTRLB.bit.RESSEL = ADC_CTRLB_RESSEL_8BIT_Val;
    #endif
    #ifdef ADC_10BITS
    ADC->CTRLB.bit.RESSEL = ADC_CTRLB_RESSEL_10BIT_Val;
    #endif
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


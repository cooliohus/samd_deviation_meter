#include <Arduino.h>
#include "global_def.h"

#ifndef ADC_MODULE
#define ADC_MODULE

void init_adc();

extern uint32_t wfAccumulator;

extern int wfCount;

extern uint32_t winAccumulator;   // sum of ADC values during sample window

extern int winCount;              // Count of ADC samples during sample window

extern int wfMax;
extern int wfMin;

extern int adcMax,
           adcMin;

extern int winMax,         // maximum ADC value during sample window
           winMin;         // minimum ADC value during sample window

//#define ADC_8BITS
#define ADC_10BITS
//#define ADC_12BITS

#ifdef ADC_12BITS
 extern int ADC_BITS = 4096;      // 12 bit ADC
#endif

#ifdef ADC_10BITS
extern int ADC_BITS;      // 10 bit ADC
#endif

#ifdef ADC_8BITS
extern int ADC_BITS;       // 8 bit ADC
#endif

extern volatile bool displayReady;  

#define SAMPLE_WINDOW 750     // This no longer determines the display update rate
                              // Display update is now a function of the sample window AND
                              // the [experimental] sliding average window code in the main loop

#endif
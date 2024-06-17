#include "8digit.h"
LedController lc;

char buff[9];

//#ifdef USE_8DIGIT_DIPLAY
void udate_8digit_display(float maxDev, float avgDC) {
//sprintf(buff1,"%3d",int(((saveMax-saveMin) * 3.3 / ADC_BITS +.005)*100));
      //sprintf(buff1,"%04d",int((maxDev * ADC_SCALE / ADC_BITS +.0005)*1000));  // TM-V71
      sprintf(buff,"%04d",int((maxDev+0.0005)*1000));  // Assemble print buffer for the -> TM-V71
      //sprintf(buff1,"%04d",int(((dev_sav / tranSave) * 10.35 / ADC_BITS +.0005)*1000));   // IC-706
      lc.setChar(0,7,buff[0],true);
      lc.setChar(0,6,buff[1],false);
      lc.setChar(0,5,buff[2],false);
      lc.setChar(0,4,buff[3],false);

      //sprintf(buff1,"%3d",int(avgDC * 3.3 / ADC_BITS +.005)*100);
      sprintf(buff,"%3d",int((avgDC+0.005)*100));  // Assemble print buffer
      //lc.setChar(0,3,buff1[0],true);
      lc.setChar(0,2,buff[0],true);
      lc.setChar(0,1,buff[1],false);
      lc.setChar(0,0,buff[2],false);
}
//#endif

//#ifdef USE_8DIGIT_DISPLAY
void init_8digit_display(){
    lc=LedController(12,11,10,4);
    lc.activateAllSegments();
    lc.setIntensity(10);        // Set the brightness to a medium values 
    lc.clearMatrix();           // and clear the display
    //scrollDigits();             // some start-up bling, show it's alive
    lc.clearMatrix();
}
//#endif


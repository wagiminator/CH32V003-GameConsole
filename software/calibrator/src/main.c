// ===================================================================================
// Project:   Joypad Calibrator
// Version:   v1.0
// Year:      2023
// Author:    Stefan Wagner
// Github:    https://github.com/wagiminator
// EasyEDA:   https://easyeda.com/wagiminator
// License:   http://creativecommons.org/licenses/by-sa/3.0/
// ===================================================================================
//
// Description:
// ------------
// Prints ADC-values of joypad-buttons on OLED for calibration.

// ===================================================================================
// Libraries, Definitions and Macros
// ===================================================================================
#include <driver.h>           // TinyJoypad conversion driver

// ===================================================================================
// Main Function
// ===================================================================================
int main(void) {
  // Setup
  JOY_init();

  // Loop
  while(1) {
    OLED_printD(ADC_read()); OLED_write('\n');
    DLY_ms(500);
  }
}

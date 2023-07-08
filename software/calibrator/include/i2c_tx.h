// ===================================================================================
// Basic I2C Master Functions (write only) for CH32V003                       * v1.0 *
// ===================================================================================
//
// Functions available:
// --------------------
// I2C_init()               Init I2C with defined clock rate (400kHz)
// I2C_start(addr)          I2C start transmission, addr must contain R/W bit
// I2C_write(b)             I2C transmit one data byte via I2C
// I2C_stop()               I2C stop transmission
//
// I2C remap settings (set below in I2C parameters):
// -------------------------------------------------
// I2C_REMAP   SDA-pin  SCL-pin
//        0      PC1      PC2
//        1      PD0      PD1
//        2      PC6      PC5
//
// External pull-up resistors (4k7 - 10k) are mandatory!
// 2023 by Stefan Wagner:   https://github.com/wagiminator

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "ch32v003.h"

// I2C Parameters
#define I2C_CLKRATE   400000    // I2C bus clock rate (Hz)
#define I2C_PRERATE   4000000   // I2C logic clock rate
#define I2C_DUTY      1         // I2C duty cycle - 0: 33%, 1: 36%
#define I2C_REMAP     0         // I2C pin remapping (see above)

// I2C Functions
void I2C_init(void);            // I2C init function
void I2C_start(uint8_t addr);   // I2C start transmission, addr must contain R/W bit
void I2C_write(uint8_t data);   // I2C transmit one data byte via I2C
void I2C_stop(void);            // I2C stop transmission

#ifdef __cplusplus
};
#endif

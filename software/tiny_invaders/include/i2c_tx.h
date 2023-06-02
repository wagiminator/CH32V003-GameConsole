// ===================================================================================
// Basic I2C Master Functions for CH32V003                                    * v1.0 *
// ===================================================================================
//
// External pull-up resistors (4k7 - 10k) are mandatory!
// 2023 by Stefan Wagner:   https://github.com/wagiminator

#pragma once
#include <stdint.h>
#include "ch32v003.h"

#ifdef __cplusplus
extern "C" {
#endif

// I2C Definitions
#define I2C_CLKRATE   400000    // I2C bus clock rate (Hz)
#define I2C_PRERATE   1200000   // I2C logic clock rate
#define I2C_DUTY      1         // I2C duty cycle - 0: 33%, 1: 36%
#define I2C_REMAP     0         // I2C pin remapping (see bottom)

// I2C Functions
void I2C_init(void);            // I2C init function
void I2C_start(uint8_t addr);   // I2C start transmission, addr must contain R/W bit
void I2C_write(uint8_t data);   // I2C transmit one data byte via I2C
void I2C_stop(void);            // I2C stop transmission

// I2C_REMAP   SDA-pin  SCL-pin
//        0      PC1      PC2
//        1      PD0      PD1
//        2      PC6      PC5

#ifdef __cplusplus
};
#endif

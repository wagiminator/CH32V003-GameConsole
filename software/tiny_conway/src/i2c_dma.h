// ===================================================================================
// Basic I2C Master Functions with DMA for TX for CH32V003                    * v1.0 *
// ===================================================================================
//
// Functions available:
// --------------------
// I2C_init()               Init I2C with defined clock rate (see below)
// I2C_start(addr)          I2C start transmission, addr must contain R/W bit
// I2C_write(b)             I2C transmit one data byte via I2C
// I2C_read(ack)            I2C receive one data byte (set ack=0 for last byte)
// I2C_stop()               I2C stop transmission
// I2C_writeBuffer(buf,len) Send buffer (*buf) with length (len) via I2C/DMA and stop
//
// I2C pin mapping (set below in I2C parameters):
// ----------------------------------------------
// I2C_MAP    0     1     2
// SDA-pin   PC1   PD0   PC6
// SCL-pin   PC2   PD1   PC5
//
// External pull-up resistors (4k7 - 10k) are mandatory!
// 2023 by Stefan Wagner:   https://github.com/wagiminator

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "system.h"

// I2C Parameters
#define I2C_CLKRATE   400000    // I2C bus clock rate (Hz)
#define I2C_MAP       0         // I2C pin mapping (see above)

// Interrupt enable check
#if SYS_USE_VECTORS == 0
  #error Interrupt vector table must be enabled (SYS_USE_VECTORS in system.h)!
#endif

// I2C Functions
void I2C_init(void);              // I2C init function
void I2C_start(uint8_t addr);     // I2C start transmission, addr must contain R/W bit
void I2C_stop(void);              // I2C stop transmission
void I2C_write(uint8_t data);     // I2C transmit one data byte via I2C
uint8_t I2C_read(uint8_t ack);    // I2C receive one data byte from the slave
void I2C_writeBuffer(uint8_t* buf, uint16_t len);

#define I2C_busy()  (I2C1->STAR2 & I2C_STAR2_BUSY)  // check if I2C is busy

#ifdef __cplusplus
};
#endif

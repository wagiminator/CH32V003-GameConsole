// ===================================================================================
// Basic I2C Master Functions with DMA for TX for CH32V003                    * v1.0 *
// ===================================================================================
// 2023 by Stefan Wagner:   https://github.com/wagiminator

#include "i2c_dma.h"

// Read/write flag
uint8_t I2C_rwflag;

// Init I2C
void I2C_init(void) {
  // Setup GPIO pins
  #if I2C_MAP == 0
    // Set pin PC1 (SDA) and PC2 (SCL) to output, open-drain, 10MHz, multiplex
    RCC->APB2PCENR |= RCC_AFIOEN | RCC_IOPCEN;
    GPIOC->CFGLR = (GPIOC->CFGLR & ~(((uint32_t)0b1111<<(1<<2)) | ((uint32_t)0b1111<<(2<<2))))
                                 |  (((uint32_t)0b1101<<(1<<2)) | ((uint32_t)0b1101<<(2<<2)));
  #elif I2C_MAP == 1
    // Set pin PD0 (SDA) and PD1 (SCL) to output, open-drain, 10MHz, multiplex
    RCC->APB2PCENR |= RCC_AFIOEN | RCC_IOPDEN;
    AFIO->PCFR1    |= 1<<1;
    GPIOD->CFGLR = (GPIOD->CFGLR & ~(((uint32_t)0b1111<<(0<<2)) | ((uint32_t)0b1111<<(1<<2))))
                                 |  (((uint32_t)0b1101<<(0<<2)) | ((uint32_t)0b1101<<(1<<2)));
  #elif I2C_MAP == 2
    // Set pin PC6 (SDA) and PC5 (SCL) to output, open-drain, 10MHz, multiplex
    RCC->APB2PCENR |= RCC_AFIOEN | RCC_IOPCEN;
    AFIO->PCFR1    |= 1<<22;
    GPIOC->CFGLR = (GPIOC->CFGLR & ~(((uint32_t)0b1111<<(6<<2)) | ((uint32_t)0b1111<<(5<<2))))
                                 |  (((uint32_t)0b1101<<(6<<2)) | ((uint32_t)0b1101<<(5<<2)));
  #else
    #warning Wrong I2C REMAP
  #endif

  // Setup and enable I2C
  RCC->APB1PCENR |= RCC_I2C1EN;                   // enable I2C module clock
  I2C1->CTLR2     = 4;                            // set input clock rate
  #if I2C_CLKRATE > 100000                        // Fast mode ?
    I2C1->CKCFGR  = (F_CPU / (3 * I2C_CLKRATE))   // -> set clock division factor 1:2
                  | I2C_CKCFGR_FS;                // -> enable fast mode (400kHz)
  #else                                           // Standard mode?
    I2C1->CKCFGR  = (F_CPU / (2 * I2C_CLKRATE));  // -> set clock division factor 1:1
  #endif
  I2C1->CTLR1   = I2C_CTLR1_PE;                   // enable I2C

  // Setup DMA Channel 5
  RCC->AHBPCENR |= RCC_DMA1EN;                    // enable DMA module clock
  DMA1_Channel6->PADDR = (uint32_t)&I2C1->DATAR;  // peripheral address
  DMA1_Channel6->CFGR  = DMA_CFG6_MINC            // increment memory address
                       | DMA_CFG6_DIR             // memory to I2C
                       | DMA_CFG6_TCIE;           // transfer complete interrupt enable
  DMA1->INTFCR         = DMA_CGIF6;               // clear interrupt flags
  NVIC_EnableIRQ(DMA1_Channel6_IRQn);             // enable the DMA IRQ
}

// Start I2C transmission (addr must contain R/W bit)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
void I2C_start(uint8_t addr) {
  while(I2C1->STAR2 & I2C_STAR2_BUSY);            // wait until bus ready
  I2C1->CTLR1 |= I2C_CTLR1_START                  // set START condition
               | I2C_CTLR1_ACK;                   // set ACK
  while(!(I2C1->STAR1 & I2C_STAR1_SB));           // wait for START generated
  I2C1->DATAR = addr;                             // send slave address + R/W bit
  while(!(I2C1->STAR1 & I2C_STAR1_ADDR));         // wait for address transmitted
  uint16_t reg = I2C1->STAR2;                     // clear flags
  I2C_rwflag = addr & 1;                          // set read/write flag
}
#pragma GCC diagnostic pop

// Send data byte via I2C bus
void I2C_write(uint8_t data) {
  while(!(I2C1->STAR1 & I2C_STAR1_TXE));          // wait for last byte transmitted
  I2C1->DATAR = data;                             // send data byte
}

// Read data byte via I2C bus (ack=0 for last byte, ack>0 if more bytes to follow)
uint8_t I2C_read(uint8_t ack) {
  if(!ack) {                                      // last byte?
    I2C1->CTLR1 &= ~I2C_CTLR1_ACK;                // -> set NAK
    I2C1->CTLR1 |=  I2C_CTLR1_STOP;               // -> set STOP condition
  }
  while(!(I2C1->STAR1 & I2C_STAR1_RXNE));         // wait for data byte received
  return I2C1->DATAR;                             // return received data byte
}

// Stop I2C transmission
void I2C_stop(void) {
  if(!I2C_rwflag) {                               // only if not already stopped
    while(!(I2C1->STAR1 & I2C_STAR1_BTF));        // wait for last byte transmitted
    I2C1->CTLR1 |= I2C_CTLR1_STOP;                // set STOP condition
  }
}

// Send data buffer via I2C bus using DMA
void I2C_writeBuffer(uint8_t* buf, uint16_t len) {
  DMA1_Channel6->CNTR  = len;                     // number of bytes to be transfered
  DMA1_Channel6->MADDR = (uint32_t)buf;           // memory address
  DMA1_Channel6->CFGR |= DMA_CFG6_EN;             // enable DMA channel
  I2C1->CTLR2         |= I2C_CTLR2_DMAEN;         // enable DMA request
}

// Interrupt service routine
void DMA1_Channel6_IRQHandler(void) __attribute__((interrupt));
void DMA1_Channel6_IRQHandler(void) {
  I2C1->CTLR2         &= ~I2C_CTLR2_DMAEN;        // disable DMA request
  DMA1_Channel6->CFGR &= ~DMA_CFG6_EN;            // disable DMA channel
  DMA1->INTFCR         = DMA_CGIF6;               // clear interrupt flags
  while(!(I2C1->STAR1 & I2C_STAR1_BTF));          // wait for last byte transmitted
  I2C1->CTLR1         |= I2C_CTLR1_STOP;          // set STOP condition
}

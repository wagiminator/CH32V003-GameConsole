// ===================================================================================
// Basic I2C Master Functions (write only) for CH32V003                       * v1.0 *
// ===================================================================================
// 2023 by Stefan Wagner:   https://github.com/wagiminator

#include "i2c_tx.h"

// I2C event flag definitions
#define I2C_START_GENERATED     0x00030001    // BUSY, MSL, SB
#define I2C_ADDR_TRANSMITTED    0x00030082    // BUSY, MSL, ADDR, TXE
#define I2C_BYTE_TRANSMITTED    0x00030084    // BUSY, MSL, BTF, TXE
#define I2C_checkEvent(n)       ((((uint32_t)I2C1->STAR1 | (I2C1->STAR2<<16)) & n) == n)

// Init I2C
void I2C_init(void) {
  #if I2C_REMAP == 0
  // Enable GPIO port C and I2C module
  RCC->APB2PCENR |= RCC_AFIOEN | RCC_IOPCEN;
  RCC->APB1PCENR |= RCC_I2C1EN;

  // Set pin PC1 (SDA) and PC2 (SCL) to output, open-drain, 10MHz, multiplex
  GPIOC->CFGLR = (GPIOC->CFGLR & ~(((uint32_t)0b1111<<(1<<2)) | ((uint32_t)0b1111<<(2<<2))))
                               |  (((uint32_t)0b1101<<(1<<2)) | ((uint32_t)0b1101<<(2<<2)));
  #elif I2C_REMAP == 1
  // Remap I2C pins, enable GPIO port D and I2C module
  RCC->APB2PCENR |= RCC_AFIOEN | RCC_IOPDEN;
  RCC->APB1PCENR |= RCC_I2C1EN;
  AFIO->PCFR1    |= 1<<1;

  // Set pin PD0 (SDA) and PD1 (SCL) to output, open-drain, 10MHz, multiplex
  GPIOD->CFGLR = (GPIOD->CFGLR & ~(((uint32_t)0b1111<<(0<<2)) | ((uint32_t)0b1111<<(1<<2))))
                               |  (((uint32_t)0b1101<<(0<<2)) | ((uint32_t)0b1101<<(1<<2)));
  #elif I2C_REMAP == 2
  // Remap I2C pins, enable GPIO port C and I2C module
  RCC->APB2PCENR |= RCC_AFIOEN | RCC_IOPCEN;
  RCC->APB1PCENR |= RCC_I2C1EN;
  AFIO->PCFR1    |= 1<<22;

  // Set pin PC6 (SDA) and PC5 (SCL) to output, open-drain, 10MHz, multiplex
  GPIOC->CFGLR = (GPIOC->CFGLR & ~(((uint32_t)0b1111<<(6<<2)) | ((uint32_t)0b1111<<(5<<2))))
                               |  (((uint32_t)0b1101<<(6<<2)) | ((uint32_t)0b1101<<(5<<2)));
  #else
    #warning Wrong I2C REMAP
  #endif

  // Set logic clock rate
  I2C1->CTLR2 = (I2C1->CTLR2 & ~I2C_CTLR2_FREQ) | (F_CPU / I2C_PRERATE);

  // Set bus clock configuration
  #if I2C_CLKRATE <= 100000
  I2C1->CKCFGR = (F_CPU / (2 * I2C_CLKRATE));
  #elif I2C_DUTY == 0
  I2C1->CKCFGR = (F_CPU / (3 * I2C_CLKRATE))
               | I2C_CKCFGR_FS;
  #else
  I2C1->CKCFGR = (F_CPU / (25 * I2C_CLKRATE))
               | I2C_CKCFGR_FS
               | I2C_CKCFGR_DUTY;
  #endif

  // Enable I2C with auto-ACK
  I2C1->CTLR1 |= I2C_CTLR1_ACK | I2C_CTLR1_PE;
}

// Start I2C transmission (addr must contain R/W bit)
void I2C_start(uint8_t addr) {
  while(I2C1->STAR2 & I2C_STAR2_BUSY);            // wait until bus ready
  I2C1->CTLR1 |= I2C_CTLR1_START;                 // set START condition
  while(!(I2C1->STAR1 & I2C_STAR1_SB));           // wait for START generated
  I2C1->DATAR = addr;                             // send slave address + R/W bit
  while(!I2C_checkEvent(I2C_ADDR_TRANSMITTED));   // wait for address transmitted
}

// Send data byte via I2C bus
void I2C_write(uint8_t data) {
  while(!(I2C1->STAR1 & I2C_STAR1_TXE));          // wait for last byte transmitted
  I2C1->DATAR = data;                             // send data byte
}

// Stop I2C transmission
void I2C_stop(void) {
  while(!(I2C1->STAR1 & I2C_STAR1_TXE));          // wait for last byte transmitted
  I2C1->CTLR1 |= I2C_CTLR1_STOP;                  // set STOP condition
}

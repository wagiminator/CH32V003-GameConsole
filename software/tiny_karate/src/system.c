// ===================================================================================
// Basic System Functions for CH32V003                                        * v1.6 *
// ===================================================================================
//
// This file must be included!!!!
//
// References:
// -----------
// - CNLohr ch32v003fun: https://github.com/cnlohr/ch32v003fun
// - WCH Nanjing Qinheng Microelectronics: http://wch.cn
//
// 2023 by Stefan Wagner:   https://github.com/wagiminator

#include "system.h"

// ===================================================================================
// Setup Microcontroller (this function is called automatically at startup)
// ===================================================================================
void SYS_init(void) {
  // Init system clock
  #if SYS_CLK_INIT > 0
  #if F_CPU > 24000000
  FLASH->ACTLR = FLASH_ACTLR_LATENCY_1;                     // 1 cycle latency
  #endif
  CLK_init();                                               // init system clock
  #endif

  // Init SYSTICK
  #if SYS_TICK_INIT > 0
  STK_init();
  #endif

  // Enable GPIO
  #if SYS_GPIO_EN > 0
    RCC->APB2PCENR |= RCC_IOPAEN | RCC_IOPCEN | RCC_IOPDEN;
  #endif
}

// ===================================================================================
// System Clock Functions
// ===================================================================================

// Init internal oscillator (non PLL) as system clock source
void CLK_init_HSI(void) {
  RCC->CFGR0 = CLK_DIV;                                         // set clock divider
}

// Init internal oscillator with PLL as system clock source
void CLK_init_HSI_PLL(void) {
  RCC->CTLR  = RCC_HSION | RCC_PLLON | ((HSITRIM) << 3);        // enable PLL, keep HSI on
  while(!(RCC->CTLR & RCC_PLLRDY));                             // wait till PLL is ready
  RCC->CFGR0 = CLK_DIV | RCC_SW_PLL;                            // select PLL as system clock source
  while((RCC->CFGR0 & RCC_SWS) != RCC_SWS_PLL);                 // wait till PLL is used as system clock source
}

// Init external crystal (non PLL) as system clock source
void CLK_init_HSE(void) {
  RCC->APB2PCENR |= RCC_AFIOEN;                                 // enable auxiliary clock module
  AFIO->PCFR1    |= AFIO_PCFR1_PA12_REMAP;                      // pins PA1-PA2 for external crystal
  RCC->CTLR       = RCC_HSION | RCC_HSEON | ((HSITRIM) << 3);   // enable HSE and keep HSI on
  while(!(RCC->CTLR & RCC_HSERDY));                             // wait till HSE is ready
  RCC->CFGR0      = CLK_DIV | RCC_SW_HSE;                       // set clock divider, use HSE for system clock
  while((RCC->CFGR0 & RCC_SWS) != RCC_SWS_HSE);                 // wait till HSE is used as system clock source
}

// Init external crystal (PLL) as system clock source
void CLK_init_HSE_PLL(void) {
  RCC->APB2PCENR |= RCC_AFIOEN;                                 // enable auxiliary clock module
  AFIO->PCFR1    |= AFIO_PCFR1_PA12_REMAP;                      // pins PA1-PA2 for external crystal
  RCC->CTLR       = RCC_HSION | RCC_HSEON | ((HSITRIM) << 3);   // enable HSE and keep HSI on
  while(!(RCC->CTLR & RCC_HSERDY));                             // wait till HSE is ready
  RCC->CFGR0      = RCC_PLLSRC | CLK_DIV;                       // set clock divider, use HSE as PLL source
  RCC->CTLR       = RCC_PLLON | RCC_HSION | RCC_HSEON | ((HSITRIM) << 3);   // enable PLL
  while(!(RCC->CTLR & RCC_PLLRDY));                             // wait till PLL is ready
  RCC->CFGR0      = RCC_PLLSRC | CLK_DIV | RCC_SW_PLL;          // select PLL as system clock source
  while((RCC->CFGR0 & RCC_SWS) != RCC_SWS_PLL);                 // wait till PLL is used as system clock source
}

// Reset system clock to default state
void CLK_reset(void) {
  RCC->CTLR |= RCC_HSION;                                       // enable HSI
  while(!(RCC->CTLR & RCC_HSIRDY));                             // wait until HSI is ready
  RCC->CFGR0 = 0x00000000;                                      // select HSI as system clock source
  while(RCC->CFGR0 & RCC_SWS);                                  // wait until HSI is selected
  RCC->CTLR  = RCC_HSION | ((HSITRIM) << 3);                    // use HSI only
  RCC->INTR  = 0x009F0000;                                      // disable interrupts and clear flags
  FLASH->ACTLR = FLASH_ACTLR_LATENCY_0;                         // no flash wait states
}

// Setup pin PC4 for MCO (output, push-pull, 50MHz, auxiliary)
void MCO_init(void) {
  RCC->APB2PCENR |= RCC_AFIOEN | RCC_IOPCEN;
  GPIOC->CFGLR    = (GPIOC->CFGLR & ~((uint32_t)0b1111<<(4<<2))) | ((uint32_t)0b1011<<(4<<2));
}

// ===================================================================================
// Delay Functions
// ===================================================================================

// Wait n counts of SysTick
void DLY_ticks(uint32_t n) {
  uint32_t end = STK->CNT + n;
  while(((int32_t)(STK->CNT - end)) < 0);
}

// ===================================================================================
// Bootloader (BOOT) Functions
// ===================================================================================

// Perform software reset and jump to bootloader
void BOOT_now(void) {
  FLASH->KEYR = 0x45670123;
  FLASH->KEYR = 0xCDEF89AB;
  FLASH->BOOT_MODEKEYR = 0x45670123;
  FLASH->BOOT_MODEKEYR = 0xCDEF89AB;      // unlock flash
  FLASH->STATR |= (uint16_t)1<<14;        // start bootloader after software reset
  FLASH->CTLR  |= FLASH_CTLR_LOCK;        // lock flash
  RCC->RSTSCKR |= RCC_RMVF;               // clear reset flags
  PFIC->CFGR = PFIC_RESETSYS | PFIC_KEY3; // perform software reset
}

// ===================================================================================
// Independent Watchdog Timer (IWDG) Functions
// ===================================================================================

// Start independent watchdog timer (IWDG) with given time in milliseconds (max 8191).
// Once the IWDG has been started, it cannot be disabled, only reloaded (feed).
// It can be stopped by disabling the internal low-speed clock (LSI).
void IWDG_start(uint16_t ms) {
  LSI_enable();                         // enable internal low-speed clock (LSI)
  IWDG->CTLR = 0x5555;                  // allow register modification
  while(IWDG->STATR & IWDG_PVU);        // wait for clock register to be ready
  IWDG->PSCR = 0b111;                   // set LSI clock prescaler 256
  while(IWDG->STATR & IWDG_RVU);        // wait for reload register to be ready
  IWDG->RLDR = ms >> 1;                 // set watchdog counter reload value
  IWDG->CTLR = 0xAAAA;                  // load reload value into watchdog counter
  IWDG->CTLR = 0xCCCC;                  // enable IWDG
}

// Reload watchdog counter with n milliseconds, n<=8191
void IWDG_reload(uint16_t ms) {
  IWDG->CTLR = 0x5555;                  // allow register modification
  while(IWDG->STATR & IWDG_RVU);        // wait for reload register to be ready
  IWDG->RLDR = ms >> 1;                 // set watchdog counter reload value
  IWDG->CTLR = 0xAAAA;                  // load reload value into watchdog counter
}

// ===================================================================================
// Automatic Wake-up Timer (AWU) Functions
// ===================================================================================

// Init automatic wake-up timer
void AWU_init(void) {
  LSI_enable();                         // enable internal low-speed clock (LSI)
  EXTI->EVENR |= ((uint32_t)1<<9);      // enable AWU event
  EXTI->RTENR |= ((uint32_t)1<<9);      // enable AWU rising edge triggering
  RCC->APB1PCENR |= RCC_PWREN;          // enable power module
  PWR->AWUCSR = PWR_AWUCSR_AWUEN;       // enable automatic wake-up timer
}

// Stop automatic wake-up timer
void AWU_stop(void) {
  PWR->AWUCSR  = 0x00;                  // disable automatic wake-up timer
  EXTI->EVENR &= ~((uint32_t)1<<9);     // disable AWU event
  EXTI->RTENR &= ~((uint32_t)1<<9);     // disable AWU rising edge triggering
}

// ===================================================================================
// Sleep Functions
// ===================================================================================

// Put device into sleep, wake up by interrupt
void SLEEP_WFI_now(void) {
  PFIC->SCTLR &= ~PFIC_SLEEPDEEP;       // set power-down mode to sleep
  __WFI();                              // wait for interrupt
}

// Put device into sleep, wake up by event
void SLEEP_WFE_now(void) {
  PFIC->SCTLR &= ~PFIC_SLEEPDEEP;       // set power-down mode to sleep
  __WFE();                              // wait for event
}

// Put device into standby (deep sleep), wake up interrupt
void STDBY_WFI_now(void) {
  RCC->APB1PCENR |= RCC_PWREN;          // enable power module
  PWR->CTLR   |= PWR_CTLR_PDDS;         // set power-down mode to standby (deep sleep)
  PFIC->SCTLR |= PFIC_SLEEPDEEP;
  __WFI();                              // wait for interrupt
  PWR->CTLR   &= ~PWR_CTLR_PDDS;        // disable PDDS again
}

// Put device into standby (deep sleep), wake up event
void STDBY_WFE_now(void) {
  RCC->APB1PCENR |= RCC_PWREN;          // enable power module
  PWR->CTLR   |= PWR_CTLR_PDDS;         // set power-down mode to standby (deep sleep)
  PFIC->SCTLR |= PFIC_SLEEPDEEP;
  __WFE();                              // wait for event
  PWR->CTLR   &= ~PWR_CTLR_PDDS;        // disable PDDS again
}

// ===================================================================================
// C++ Support
// Based on CNLohr ch32v003fun: https://github.com/cnlohr/ch32v003fun
// ===================================================================================
#ifdef __cplusplus
extern void __cxa_pure_virtual() { while (1); }
extern void (*__preinit_array_start[]) (void) __attribute__((weak));
extern void (*__preinit_array_end[]) (void) __attribute__((weak));
extern void (*__init_array_start[]) (void) __attribute__((weak));
extern void (*__init_array_end[]) (void) __attribute__((weak));

void __libc_init_array(void) {
  uint32_t count, i;
  count = __preinit_array_end - __preinit_array_start;
  for(i = 0; i < count; i++) __preinit_array_start[i]();
  count = __init_array_end - __init_array_start;
  for(i = 0; i < count; i++) __init_array_start[i]();
}
#endif

// ===================================================================================
// C version of CH32V003 Startup .s file from WCH
// Based on CNLohr ch32v003fun: https://github.com/cnlohr/ch32v003fun
// ===================================================================================
extern uint32_t _sbss;
extern uint32_t _ebss;
extern uint32_t _data_lma;
extern uint32_t _data_vma;
extern uint32_t _edata;

// Prototypes
int main(void)                __attribute__((section(".text.main"), used));
void jump_reset(void)         __attribute__((section(".init.jump"), naked, used));
void reset_handler(void)      __attribute__((section(".text.reset_handler"), naked, used));

// FLASH starts with a jump to the reset handler
void jump_reset(void)         { asm volatile("j reset_handler"); }

#if SYS_USE_VECTORS > 0
// Unless a specific handler is overridden, it just spins forever
void default_handler(void)    __attribute__((section(".text.vector_handler"), naked, used));
void default_handler(void)    { while(1); }

// All interrupt handlers are aliased to default_handler unless overridden individually
#define DUMMY_HANDLER __attribute__((section(".text.vector_handler"), weak, alias("default_handler"), used))
DUMMY_HANDLER void NMI_Handler(void);
DUMMY_HANDLER void HardFault_Handler(void);
DUMMY_HANDLER void SysTick_Handler(void);
DUMMY_HANDLER void SW_Handler(void);
DUMMY_HANDLER void WWDG_IRQHandler(void);
DUMMY_HANDLER void PVD_IRQHandler(void);
DUMMY_HANDLER void FLASH_IRQHandler(void);
DUMMY_HANDLER void RCC_IRQHandler(void);
DUMMY_HANDLER void EXTI7_0_IRQHandler(void);
DUMMY_HANDLER void AWU_IRQHandler(void);
DUMMY_HANDLER void DMA1_Channel1_IRQHandler(void);
DUMMY_HANDLER void DMA1_Channel2_IRQHandler(void);
DUMMY_HANDLER void DMA1_Channel3_IRQHandler(void);
DUMMY_HANDLER void DMA1_Channel4_IRQHandler(void);
DUMMY_HANDLER void DMA1_Channel5_IRQHandler(void);
DUMMY_HANDLER void DMA1_Channel6_IRQHandler(void);
DUMMY_HANDLER void DMA1_Channel7_IRQHandler(void);
DUMMY_HANDLER void ADC1_IRQHandler(void);
DUMMY_HANDLER void I2C1_EV_IRQHandler(void);
DUMMY_HANDLER void I2C1_ER_IRQHandler(void);
DUMMY_HANDLER void USART1_IRQHandler(void);
DUMMY_HANDLER void SPI1_IRQHandler(void);
DUMMY_HANDLER void TIM1_BRK_IRQHandler(void);
DUMMY_HANDLER void TIM1_UP_IRQHandler(void);
DUMMY_HANDLER void TIM1_TRG_COM_IRQHandler(void);
DUMMY_HANDLER void TIM1_CC_IRQHandler(void);
DUMMY_HANDLER void TIM2_IRQHandler(void);

// Interrupt vector table
void (*const vectors[])(void) __attribute__((section(".init.vectors"), used));
void (*const vectors[])(void) = {
  // RISC-V handlers
  0,                                //  1 - Reserved
  NMI_Handler,                      //  2 - NMI Handler
  HardFault_Handler,                //  3 - Hard Fault Handler
  0,                                //  4 - Reserved
  0,                                //  5 - Reserved
  0,                                //  6 - Reserved
  0,                                //  7 - Reserved
  0,                                //  8 - Reserved
  0,                                //  9 - Reserved
  0,                                // 10 - Reserved
  0,                                // 11 - Reserved
  SysTick_Handler,                  // 12 - SysTick Handler
  0,                                // 13 - Reserved
  SW_Handler,                       // 14 - SW Handler
  0,                                // 15 - Reserved
  
  // Peripheral handlers
  WWDG_IRQHandler,                  // 16 - Window Watchdog
  PVD_IRQHandler,                   // 17 - PVD through EXTI Line detect
  FLASH_IRQHandler,                 // 18 - Flash
  RCC_IRQHandler,                   // 19 - RCC
  EXTI7_0_IRQHandler,               // 20 - EXTI Line 7..0
  AWU_IRQHandler,                   // 21 - AWU
  DMA1_Channel1_IRQHandler,         // 22 - DMA1 Channel 1
  DMA1_Channel2_IRQHandler,         // 23 - DMA1 Channel 2
  DMA1_Channel3_IRQHandler,         // 24 - DMA1 Channel 3
  DMA1_Channel4_IRQHandler,         // 25 - DMA1 Channel 4
  DMA1_Channel5_IRQHandler,         // 26 - DMA1 Channel 5
  DMA1_Channel6_IRQHandler,         // 27 - DMA1 Channel 6
  DMA1_Channel7_IRQHandler,         // 28 - DMA1 Channel 7
  ADC1_IRQHandler,                  // 29 - ADC1
  I2C1_EV_IRQHandler,               // 30 - I2C1 Event
  I2C1_ER_IRQHandler,               // 31 - I2C1 Error
  USART1_IRQHandler,                // 32 - USART1
  SPI1_IRQHandler,                  // 33 - SPI1
  TIM1_BRK_IRQHandler,              // 34 - TIM1 Break
  TIM1_UP_IRQHandler,               // 35 - TIM1 Update
  TIM1_TRG_COM_IRQHandler,          // 36 - TIM1 Trigger and Commutation
  TIM1_CC_IRQHandler,               // 37 - TIM1 Capture Compare
  TIM2_IRQHandler,                  // 38 - TIM2
};
#endif  // SYS_USE_VECTORS > 0

// Reset handler
void reset_handler(void) {
  uint32_t *src, *dst;
  
  // Set pointers, vectors, processor status, and interrupts
  asm volatile(
  " .option push              \n\
    .option norelax           \n\
    la gp, __global_pointer$  \n\
    .option pop               \n\
    la sp, _eusrstack         \n"
    #if __GNUC__ > 10
    ".option arch, +zicsr     \n"
    #endif
  " li a0, 0x88               \n\
    csrw mstatus, a0          \n\
    li a1, 0x3                \n\
    csrw 0x804, a1            \n\
    la a0, jump_reset         \n\
    or a0, a0, a1             \n\
    csrw mtvec, a0            \n\
    csrw mepc, %[main]        \n"
    : : [main] "r" (main) : "a0", "a1" , "memory"
  );

  // Copy data from FLASH to RAM
  src = &_data_lma;
  dst = &_data_vma;
  while(dst < &_edata) *dst++ = *src++;

  // Clear uninitialized variables
  #if SYS_CLEAR_BSS > 0
  dst = &_sbss;
  while(dst < &_ebss) *dst++ = 0;
  #endif

  // C++ Support
  #ifdef __cplusplus
  __libc_init_array();
  #endif

  // Init system
  SYS_init();

  // Return
  asm volatile("mret");
}

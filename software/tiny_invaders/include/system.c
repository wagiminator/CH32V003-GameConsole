// ===================================================================================
// Basic System Functions for CH32V003                                        * v1.3 *
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
  CLK_init();
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
  FLASH->ACTLR  = FLASH_ACTLR_LATENCY_0;                        // 0 cycle latency
  RCC->INTR     = 0x009F0000;                                   // clear ready flags
  RCC->CFGR0    = CLK_DIV;                                      // set clock divider
  RCC->CTLR     = RCC_HSION | ((HSITRIM) << 3);                 // use HSI, Only
}

// Init internal oscillator with PLL as system clock source
void CLK_init_HSI_PLL(void) {
  FLASH->ACTLR  = FLASH_ACTLR_LATENCY_1;                        // 1 cycle latency
  RCC->INTR     = 0x009F0000;                                   // clear ready flags
  RCC->CFGR0    = CLK_DIV | RCC_PLLSRC_HSI_Mul2;                // set PLL and clock divider
  RCC->CTLR     = RCC_HSION | RCC_PLLON | ((HSITRIM) << 3);     // use HSI, but enable PLL
  while(!(RCC->CTLR & RCC_PLLRDY));                             // wait till PLL is ready
  RCC->CFGR0 = (RCC->CFGR0 & ~(RCC_SW)) | RCC_SW_PLL;           // select PLL as system clock source
  while((RCC->CFGR0 & RCC_SWS) != RCC_SWS_PLL);                 // wait till PLL is used as system clock source
}

// Init external crystal (non PLL) as system clock source
void CLK_init_HSE(void) {
  RCC->APB2PCENR |= RCC_AFIOEN;                                 // enable auxiliary clock module
  AFIO->PCFR1    |= AFIO_PCFR1_PA12_REMAP;                      // pins PA1-PA2 for external crystal
  FLASH->ACTLR    = FLASH_ACTLR_LATENCY_0;                      // 0 cycle latency
  RCC->CTLR       = RCC_HSION | RCC_HSEON | RCC_PLLON;          // enable HSE and keep HSI+PLL on
  while(!(RCC->CTLR & RCC_HSERDY));                             // wait till HSE is ready
  RCC->CFGR0      = RCC_HPRE_DIV1 | RCC_SW_HSE;                 // HCLK = SYSCLK = APB1 and use HSE for system clock
  while((RCC->CFGR0 & RCC_SWS) != RCC_SWS_HSE);                 // wait till HSE is used as system clock source
  RCC->CTLR       = RCC_HSEON;                                  // turn off HSI + PLL.
}

// Init external crystal (PLL) as system clock source
void CLK_init_HSE_PLL(void) {
  RCC->APB2PCENR |= RCC_AFIOEN;                                 // enable auxiliary clock module
  AFIO->PCFR1    |= AFIO_PCFR1_PA12_REMAP;                      // pins PA1-PA2 for external crystal
  RCC->CTLR       = RCC_HSION | RCC_HSEON | RCC_PLLON;          // enable HSE and keep HSI+PLL on
  while(!(RCC->CTLR & RCC_HSERDY));                             // wait till HSE is ready
  RCC->CFGR0      = RCC_SW_HSE | RCC_HPRE_DIV1;                 // HCLK = SYSCLK = APB1 and use HSE for system clock
  FLASH->ACTLR    = FLASH_ACTLR_LATENCY_1;                      // 1 cycle latency
  RCC->CTLR       = RCC_HSEON;                                  // turn off PLL and HSI
  RCC->CFGR0      = RCC_SW_HSE | RCC_HPRE_DIV1 | RCC_PLLSRC_HSE_Mul2; // use PLL with HSE
  RCC->CTLR       = RCC_HSEON | RCC_PLLON;                      // turn PLL back on..
  while(!(RCC->CTLR & RCC_PLLRDY));                             // wait till PLL is ready
  RCC->CFGR0 = RCC_SW_PLL | RCC_HPRE_DIV1 | RCC_PLLSRC_HSE_Mul2;// select PLL as system clock source
  while((RCC->CFGR0 & RCC_SWS) != RCC_SWS_PLL);                 // wait till PLL is used as system clock source
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
// Sleep Functions
// ===================================================================================

// Init automatic wake-up timer
void AWU_init(void) {
  LSI_enable();                         // enable internal low-speed clock (LSI)
  EXTI->EVENR |= ((uint32_t)1<<9);      // enable AWU event
  EXTI->FTENR |= ((uint32_t)1<<9);      // enable AWU falling edge triggering
  RCC->APB1PCENR |= RCC_PWREN;          // enable power module
  PWR->AWUCSR = PWR_AWUCSR_AWUEN;       // enable automatic wake-up timer
}

// Put device into sleep, wake up by interrupt
void SLEEP_WFI_now(void) {
  PWR->CTLR &= ~PWR_CTLR_PDDS;          // set power-down mode to sleep
  __WFI();                              // wait for interrupt
}

// Put device into sleep, wake up by event
void SLEEP_WFE_now(void) {
  PWR->CTLR &= ~PWR_CTLR_PDDS;          // set power-down mode to sleep
  __WFE();                              // wait for event
}

// Put device into standby (deep sleep), wake up interrupt
void STDBY_WFI_now(void) {
  PWR->CTLR   |= PWR_CTLR_PDDS;         // set power-down mode to standby (deep sleep)
  PFIC->SCTLR |= PFIC_SLEEPDEEP;        // set deep sleep mode
  __WFI();                              // wait for interrupt
  PFIC->SCTLR &= ~PFIC_SLEEPDEEP;       // unset deep sleep mode
}

// Put device into standby (deep sleep), wake up event
void STDBY_WFE_now(void) {
  PWR->CTLR   |= PWR_CTLR_PDDS;         // set power-down mode to standby (deep sleep)
  PFIC->SCTLR |= PFIC_SLEEPDEEP;        // set deep sleep mode
  __WFE();                              // wait for event
  PFIC->SCTLR &= ~PFIC_SLEEPDEEP;       // unset deep sleep mode
}

// ===================================================================================
// C++ Support
// Based on CNLohr ch32v003fun: https://github.com/cnlohr/ch32v003fun
// ===================================================================================
#ifdef __cplusplus
// This is required to allow pure virtual functions to be defined.
extern void __cxa_pure_virtual() { while (1); }

// These magic symbols are provided by the linker.
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
void (*const vectors[])(void) __attribute__((section(".init.vectors"), used));
void default_handler(void)    __attribute__((section(".text.vector_handler"), naked, used));
void reset_handler(void)      __attribute__((section(".text.reset_handler"), naked, used));

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

// FLASH starts with a jump to the reset handler
void jump_reset(void) { asm volatile("j reset_handler"); }

// Afterwards there comes the interrupt vector table
void (* const vectors[])(void) = {
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

// Unless a specific handler is overridden, it just spins forever
void default_handler(void) { while(1); }

// Reset handler
void reset_handler(void) {
  uint32_t *src, *dst;

  // Set global pointer and stack pointer
  asm volatile( "\n\
    .option push\n\
    .option norelax\n\
    la gp, __global_pointer$\n\
    .option pop\n\
    la sp, _eusrstack\n"
    #if __GNUC__ > 10
  " .option arch, +zicsr\n"
    #endif

    // Setup the interrupt vector, processor status and INTSYSCR
  " li a0, 0x80\n\
    csrw mstatus, a0\n\
    li a3, 0x3\n\
    ;csrw 0x804, a3\n\
    la a0, jump_reset\n\
    or a0, a0, a3\n\
    csrw mtvec, a0\n" 
    : : : "a0", "a3", "memory"
  );

  // Copy data from FLASH to RAM
  src = &_data_lma;
  dst = &_data_vma;
  while(dst < &_edata) *dst++ = *src++;

  // Clear uninitialized variables
  dst = &_sbss;
  while(dst < &_ebss) *dst++ = 0;

  // C++ Support
  #ifdef __cplusplus
  __libc_init_array();
  #endif

  // Init system
  SYS_init();

  // Set mepc to be main as the root app
  asm volatile(
  " csrw mepc, %[main]\n"
  " mret\n" : : [main]"r"(main)
  );
}

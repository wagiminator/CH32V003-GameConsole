// ===================================================================================
// Basic System Functions for CH32V003                                        * v1.6 *
// ===================================================================================
//
// This file must be included!!! The system configuration and the system clock are 
// set up automatically on system start.
//
// System clock functions available:
// ---------------------------------
// CLK_init_HSI()           init internal oscillator (non PLL) as system clock source
// CLK_init_HSI_PLL()       init internal oscillator with PLL as system clock source
// CLK_init_HSE()           init external crystal (non PLL) as system clock source
// CLK_init_HSE_PLL()       init external crystal (PLL) as system clock source
// CLK_reset()              reset system clock to default state
//
// HSI_enable()             enable internal 8MHz high-speed clock (HSI)
// HSI_disable()            disable HSI
// HSI_ready()              check if HSI is stable
//
// HSE_enable()             enable external high-speed clock (HSE)
// HSE_disable()            disable HSE
// HSE_ready()              check if HSE is stable
// HSE_bypass_on()          enable HSE clock bypass
// HSE_bypass_off()         disable HSE clock bypass
//
// LSI_enable()             enable internal 128kHz low-speed clock (LSI)
// LSI_disable()            disable LSI
// LSI_ready()              check if LSI is stable
//
// PLL_enable()             enable phase-locked loop (PLL)
// PLL_disable()            disable PLL
// PLL_ready()              check if PLL is stable
// PLL_setHSI()             set HSI as PLL input (PLL muste be disabled)
// PLL_setHSE()             set HSE as PLL input (PLL muste be disabled)
//
// MCO_init()               init clock output to pin PC4
// MCO_setSYS()             output SYS_CLK on pin PC4
// MCO_setHSI()             output internal oscillator on pin PC4
// MCO_setHSE()             output external crystal on pin PC4 (if available)
// MCO_setPLL()             output PLL on pin PC4
//
// Delay (DLY) functions available:
// --------------------------------
// DLY_ticks(n)             delay n clock cycles
// DLY_us(n)                delay n microseconds
// DLY_ms(n)                delay n milliseconds
//
// Reset (RST) and Bootloader (BOOT) functions available:
// ------------------------------------------------------
// BOOT_now()               conduct software reset and jump to bootloader
// RST_now()                conduct software reset
// RST_clearFlags()         clear all reset flags
// RST_wasLowPower()        check if last reset was caused by low power
// RST_wasWWDG()            check if last reset was caused by window watchdog
// RST_wasIWDG()            check if last reset was caused by independent watchdog
// RST_wasSoftware()        check if last reset was caused by software
// RST_wasPower()           check if last reset was caused by power up
// RST_wasPin()             check if last reset was caused by RST pin low
//
// Independent Watchdog Timer (IWDG) functions available:
// ------------------------------------------------------
// IWDG_start(n)            start independent watchdog timer, n milliseconds, n<=8191
// IWDG_reload(n)           reload watchdog counter with n milliseconds, n<=8191
// IWDG_feed()              feed the dog (reload last time)
//
// Automatic Wake-up Timer (AWU) functions available:
// --------------------------------------------------
// AWU_start(n)             start AWU with n milliseconds period and event trigger
// AWU_stop()               stop AWU and event trigger
// AWU_set(n)               set AWU period to n milliseconds
//
// AWU_enable()             enable AWU (without LSI and PWR module)
// AWU_disable()            disable AWU (without LSI and PWR module)
// AWU_RT_enable()          enable AWU rising edge trigger
// AWU_RT_disable()         disable AWU rising edge trigger
// AWU_EV_enable()          enable AWU event
// AWU_EV_disable()         disable AWU event
// AWU_INT_enable()         enable AWU interrupt (without NVIC)
// AWU_INT_disable()        disable AWU interrupt (without NVIC)
//
// Sleep functions available:
// --------------------------
// SLEEP_WFI_now()          put device into sleep, wake up by interrupt
// SLEEP_WFE_now()          put device into sleep, wake up by event
// STDBY_WFI_now()          put device into standby (deep sleep), wake by interrupt
// STDBY_WFE_now()          put device into standby (deep sleep), wake by event
//
// SLEEP_ms(n)              put device into SLEEP for n milliseconds (uses AWU)
// STDBY_ms(n)              put device into STANDBY for n milliseconds (uses AWU)
//
// Programmable Voltage Detector (PVD) functions available:
// --------------------------------------------------------
// PVD_enable()             enable PVD
// PVD_set_2V7()            set detection level to 2.7V  falling / 2.85V rising edge
// PVD_set_2V9()            set detection level to 2.9V  falling / 3.05V rising edge
// PVD_set_3V15()           set detection level to 3.15V falling / 3.3V  rising edge
// PVD_set_3V3()            set detection level to 3.3V  falling / 3.5V  rising edge
// PVD_set_3V5()            set detection level to 3.5V  falling / 3.7V  rising edge
// PVD_set_3V7()            set detection level to 3.7V  falling / 3.9V  rising edge
// PVD_set_3V9()            set detection level to 3.9V  falling / 4.1V  rising edge
// PVD_set_4V2()            set detection level to 4.2V  falling / 4.4V  rising edge
// PVD_isLow()              check if VDD is below detection level
//
// Interrupt (INT) functions available:
// ------------------------------------
// INT_enable()             global interrupt enable
// INT_disable()            global interrupt disable
// INT_ATOMIC_BLOCK { }     execute block without being interrupted
//
// References:
// -----------
// - CNLohr ch32v003fun: https://github.com/cnlohr/ch32v003fun
// - WCH Nanjing Qinheng Microelectronics: http://wch.cn
//
// 2023 by Stefan Wagner:   https://github.com/wagiminator

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "ch32v003.h"

// ===================================================================================
// System Options (set "1" to activate)
// ===================================================================================
#define SYS_CLK_INIT      1         // 1: init system clock on startup
#define SYS_TICK_INIT     1         // 1: init and start SYSTICK on startup
#define SYS_GPIO_EN       1         // 1: enable GPIO ports on startup
#define SYS_CLEAR_BSS     1         // 1: clear uninitialized variables
#define SYS_USE_VECTORS   1         // 1: create interrupt vector table
#define SYS_USE_HSE       0         // 1: use external crystal

// ===================================================================================
// Sytem Clock Defines
// ===================================================================================
// Set system clock frequency
#ifndef F_CPU
  #define F_CPU           24000000  // 24Mhz if not otherwise defined
#endif

// Calculate system clock settings
#if   F_CPU == 48000000
  #define CLK_DIV         RCC_HPRE_DIV1
  #define SYS_USE_PLL
#elif F_CPU == 24000000
  #define CLK_DIV         RCC_HPRE_DIV1
#elif F_CPU == 16000000
  #define CLK_DIV         RCC_HPRE_DIV3
  #define SYS_USE_PLL
#elif F_CPU == 12000000
  #define CLK_DIV         RCC_HPRE_DIV2
#elif F_CPU ==  8000000
  #define CLK_DIV         RCC_HPRE_DIV3
#elif F_CPU ==  6000000
  #define CLK_DIV         RCC_HPRE_DIV4
#elif F_CPU ==  4000000
  #define CLK_DIV         RCC_HPRE_DIV6
#elif F_CPU ==  3000000
  #define CLK_DIV         RCC_HPRE_DIV8
#elif F_CPU ==  1500000
  #define CLK_DIV         RCC_HPRE_DIV16
#elif F_CPU ==   750000
  #define CLK_DIV         RCC_HPRE_DIV32
#elif F_CPU ==   375000
  #define CLK_DIV         RCC_HPRE_DIV64
#elif F_CPU ==   187500
  #define CLK_DIV         RCC_HPRE_DIV128
#elif F_CPU ==    93750
  #define CLK_DIV         RCC_HPRE_DIV256
#else
  #warning Unsupported system clock frequency, using internal 24MHz
  #define CLK_DIV         RCC_HPRE_DIV1
  #undef  F_CPU
  #define F_CPU           24000000
#endif

#if SYS_USE_HSE > 0
  #ifdef SYS_USE_PLL
    #define CLK_init      CLK_init_HSE_PLL
  #else
    #define CLK_init      CLK_init_HSE
  #endif
#else
  #ifdef SYS_USE_PLL
    #define CLK_init      CLK_init_HSI_PLL
  #else
    #define CLK_init      CLK_init_HSI
  #endif
#endif

// ===================================================================================
// System Clock Functions
// ===================================================================================
void CLK_init_HSI(void);      // init internal oscillator (non PLL) as system clock source
void CLK_init_HSI_PLL(void);  // init internal oscillator with PLL as system clock source
void CLK_init_HSE(void);      // init external crystal (non PLL) as system clock source
void CLK_init_HSE_PLL(void);  // init external crystal (PLL) as system clock source
void CLK_reset(void);         // reset system clock to default state

// Internal 8MHz high-speed clock (HSI) functions
#define HSI_enable()      RCC->CTLR |= RCC_HSION        // enable HSI
#define HSI_disable()     RCC->CTLR &= ~RCC_HSION       // disable HSI
#define HSI_ready()       (RCC->CTLR & RCC_HSIRDY)      // check if HSI is stable

// External high-speed clock (HSE) functions
#define HSE_enable()      RCC->CTLR |= RCC_HSEON        // enable HSE
#define HSE_disable()     RCC->CTLR &= ~RCC_HSEON       // disable HSE
#define HSE_ready()       (RCC->CTLR & RCC_HSERDY)      // check if HSE is stable
#define HSE_bypass_on()   RCC->CTLR |= RCC_HSEBYP       // enable HSE clock bypass
#define HSE_bypass_off()  RCC->CTLR &= ~RCC_HSEBYP      // disable HSE clock bypass

// Internal 128kHz low-speed clock (LSI) functions
#define LSI_enable()      RCC->RSTSCKR |= RCC_LSION     // enable LSI
#define LSI_disable()     RCC->RSTSCKR &= ~RCC_LSION    // disable LSI
#define LSI_ready()       (RCC->RSTSCKR & RCC_LSIRDY)   // check if LSI is stable

// Phase-locked loop (PLL) functions
#define PLL_enable()      RCC->CTLR |=  RCC_PLLON       // enable PLL
#define PLL_disable()     RCC->CTLR &= ~RCC_PLLON       // disable PLL
#define PLL_ready()       (RCC->CTLR & RCC_PLLRDY)      // check if PLL is stable
#define PLL_setHSI()      RCC->CFGR0 &= ~RCC_PLLSRC     // set HSI as PLL input
#define PLL_setHSE()      RCC->CFGR0 |=  RCC_PLLSRC     // set HSE as PLL input

// Clock output functions (pin PC4)
#define MCO_setSYS()      RCC->CFGR0 = (RCC->CFGR0 & ~RCC_CFGR0_MCO) | RCC_CFGR0_MCO_SYSCLK
#define MCO_setHSI()      RCC->CFGR0 = (RCC->CFGR0 & ~RCC_CFGR0_MCO) | RCC_CFGR0_MCO_HSI
#define MCO_setHSE()      RCC->CFGR0 = (RCC->CFGR0 & ~RCC_CFGR0_MCO) | RCC_CFGR0_MCO_HSE
#define MCO_setPLL()      RCC->CFGR0 = (RCC->CFGR0 & ~RCC_CFGR0_MCO) | RCC_CFGR0_MCO_PLL
#define MCO_stop()        RCC->CFGR0 &= ~RCC_CFGR0_MCO  // stop clock output to pin PC4
void MCO_init(void);                                    // init clock output to pin PC4

// ===================================================================================
// Delay (DLY) Functions
// ===================================================================================
#define STK_init()        STK->CTLR = STK_CTLR_STE | STK_CTLR_STCLK // init SYSTICK @ F_CPU
#define DLY_US_TIME       (F_CPU / 1000000)             // system ticks per us
#define DLY_MS_TIME       (F_CPU / 1000)                // system ticks per ms
#define DLY_us(n)         DLY_ticks((n) * DLY_US_TIME)  // delay n microseconds
#define DLY_ms(n)         DLY_ticks((n) * DLY_MS_TIME)  // delay n milliseconds
void DLY_ticks(uint32_t n);                             // delay n system ticks

// ===================================================================================
// Reset (RST) Functions
// ===================================================================================
#define RST_now()         PFIC->CFGR    = PFIC_RESETSYS | PFIC_KEY3
#define RST_clearFlags()  RCC->RSTSCKR |= RCC_RMVF
#define RST_wasLowPower() (RCC->RSTSCKR & RCC_LPWRRSTF)
#define RST_wasWWDG()     (RCC->RSTSCKR & RCC_WWDGRSTF)
#define RST_wasIWDG()     (RCC->RSTSCKR & RCC_IWDGRSTF)
#define RST_wasSoftware() (RCC->RSTSCKR & RCC_SFTRSTF)
#define RST_wasPower()    (RCC->RSTSCKR & RCC_PORRSTF)
#define RST_wasPin()      (RCC->RSTSCKR & RCC_PINRSTF)

// ===================================================================================
// Bootloader (BOOT) Functions
// ===================================================================================
void BOOT_now(void);        // perform software reset and jump to bootloader

// ===================================================================================
// Independent Watchdog Timer (IWDG) Functions
// ===================================================================================
void IWDG_start(uint16_t ms);                           // start IWDG with time in ms
void IWDG_reload(uint16_t ms);                          // reload IWDG with time in ms
#define IWDG_feed()       IWDG->CTLR = 0xAAAA           // feed the dog (reload time)
#define IWDG_reset()      IWDG->CTLR = 0xAAAA           // alias

// ===================================================================================
// Automatic Wake-up Timer (AWU) Functions
// ===================================================================================
void AWU_init(void);        // init automatic wake-up timer
void AWU_stop(void);        // stop automatic wake-up timer

// AWU macros
#define AWU_start(n)          {AWU_init(); AWU_set(n);}
#define AWU_enable()          PWR->AWUCSR = PWR_AWUCSR_AWUEN
#define AWU_disable()         PWR->AWUCSR = 0x00
#define AWU_RT_enable()       EXTI->RTENR  |=  ((uint32_t)1 << 9)
#define AWU_RT_disable()      EXTI->RTENR  &= ~((uint32_t)1 << 9)
#define AWU_EV_enable()       EXTI->EVENR  |=  ((uint32_t)1 << 9)
#define AWU_EV_disable()      EXTI->EVENR  &= ~((uint32_t)1 << 9)
#define AWU_INT_enable()      EXTI->INTENR |=  ((uint32_t)1 << 9)
#define AWU_INT_disable()     EXTI->INTENR &= ~((uint32_t)1 << 9)

#define AWU_sleep(ms)         {AWU_set(ms); SLEEP_WFE_now();}
#define AWU_stdby(ms)         {AWU_set(ms); STDBY_WFE_now();}

// Set automatic wake-up timer in milliseconds
#define AWU_set(ms) \
  (ms <    64 ? ({PWR->AWUPSC = 0b1000; PWR->AWUWR = (ms);    }) : \
  (ms <   128 ? ({PWR->AWUPSC = 0b1001; PWR->AWUWR = (ms)>>1; }) : \
  (ms <   256 ? ({PWR->AWUPSC = 0b1010; PWR->AWUWR = (ms)>>2; }) : \
  (ms <   512 ? ({PWR->AWUPSC = 0b1011; PWR->AWUWR = (ms)>>3; }) : \
  (ms <  1024 ? ({PWR->AWUPSC = 0b1100; PWR->AWUWR = (ms)>>4; }) : \
  (ms <  2048 ? ({PWR->AWUPSC = 0b1101; PWR->AWUWR = (ms)>>5; }) : \
  (ms <  5120 ? ({PWR->AWUPSC = 0b1110; PWR->AWUWR = (ms)/80; }) : \
  (ms < 30720 ? ({PWR->AWUPSC = 0b1111; PWR->AWUWR = (ms)/480;}) : \
  (0)))))))))

// ===================================================================================
// Sleep Functions
// ===================================================================================
void SLEEP_WFI_now(void);   // put device into sleep, wake up by interrupt
void SLEEP_WFE_now(void);   // put device into sleep, wake up by event
void STDBY_WFI_now(void);   // put device into standby (deep sleep), wake up interrupt
void STDBY_WFE_now(void);   // put device into standby (deep sleep), wake up event

#define SLEEP_ms(n)           {AWU_start(n); SLEEP_WFE_now(); AWU_stop();}
#define STDBY_ms(n)           {AWU_start(n); STDBY_WFE_now(); AWU_stop();}

// ===================================================================================
// Programmable Voltage Detector (PVD) Functions
// ===================================================================================
#define PVD_enable()          {RCC->APB1PCENR |= RCC_PWREN; PWR->CTLR |= PWR_CTLR_PVDE;}
#define PVD_set_2V7()         PWR->CTLR &= ~PWR_CTLR_PLS
#define PVD_set_2V9()         PWR->CTLR  = (PWR->CTLR & ~PWR_CTLR_PLS) | (0b001 << 5)
#define PVD_set_3V15()        PWR->CTLR  = (PWR->CTLR & ~PWR_CTLR_PLS) | (0b010 << 5)
#define PVD_set_3V3()         PWR->CTLR  = (PWR->CTLR & ~PWR_CTLR_PLS) | (0b011 << 5)
#define PVD_set_3V5()         PWR->CTLR  = (PWR->CTLR & ~PWR_CTLR_PLS) | (0b100 << 5)
#define PVD_set_3V7()         PWR->CTLR  = (PWR->CTLR & ~PWR_CTLR_PLS) | (0b101 << 5)
#define PVD_set_3V9()         PWR->CTLR  = (PWR->CTLR & ~PWR_CTLR_PLS) | (0b110 << 5)
#define PVD_set_4V2()         PWR->CTLR |=  PWR_CTLR_PLS
#define PVD_isLow()           (PWR->CSR & PWR_CSR_PVDO)

#define PVD_RT_enable()       EXTI->RTENR  |=  ((uint32_t)1 << 8)
#define PVD_RT_disable()      EXTI->RTENR  &= ~((uint32_t)1 << 8)
#define PVD_FT_enable()       EXTI->FTENR  |=  ((uint32_t)1 << 8)
#define PVD_FT_disable()      EXTI->FTENR  &= ~((uint32_t)1 << 8)
#define PVD_EV_enable()       EXTI->EVENR  |=  ((uint32_t)1 << 8)
#define PVD_EV_disable()      EXTI->EVENR  &= ~((uint32_t)1 << 8)
#define PVD_INT_enable()      EXTI->INTENR |=  ((uint32_t)1 << 8)
#define PVD_INT_disable()     EXTI->INTENR &= ~((uint32_t)1 << 8)

// ===================================================================================
// Interrupt (INT) Functions
// ===================================================================================
#define INT_enable()          __enable_irq()
#define INT_disable()         __disable_irq()
#define INT_ATOMIC_BLOCK      for(INT_ATOMIC_RESTORE, __ToDo = 1; __ToDo; __ToDo = 0)
#define INT_ATOMIC_RESTORE    uint32_t __reg_save __attribute__((__cleanup__(__iRestore))) = __iSave()

// Save interrupt status and disable interrupts
static inline uint32_t __iSave(void) {
  uint32_t result, temp;
  __asm volatile(
    #if __GNUC__ > 10
    ".option arch, +zicsr \n"
    #endif
    "csrr %0, mstatus     \n"
    "andi	%1, %0, ~0x88   \n"
    "csrw	mstatus, %1     \n"
    "andi	%0, %0, 0x88" : "=r" (result), "=r" (temp)
  );
  return result;
}

// Restore interrupt status
static inline void __iRestore(const uint32_t *__s) {
  uint32_t temp;
  __asm volatile(
    #if __GNUC__ > 10
    ".option arch, +zicsr \n"
    #endif
    "csrr %0, mstatus     \n"
    "or	  %0, %0, %1      \n"
    "csrw mstatus, %0" : "=&r" (temp) : "r" (*__s)
  );
}

// ===================================================================================
// Device Electronic Signature (ESIG)
// ===================================================================================
#define ESIG_FLASHSIZE      (*(__I uint16_t*)(0x1FFFF7E0))
#define ESIG_UID1           (*(__I uint32_t*)(0x1FFFF7E8))
#define ESIG_UID2           (*(__I uint32_t*)(0x1FFFF7EC))
#define ESIG_UID3           (*(__I uint32_t*)(0x1FFFF7F0))

// ===================================================================================
// Imported System Functions
// ===================================================================================
// Enable Global Interrupt
static inline void __enable_irq(void) {
  uint32_t temp;
  __asm volatile(
    #if __GNUC__ > 10
    ".option arch, +zicsr \n"
    #endif
    "csrr %0, mstatus     \n"
    "ori  %0, %0, 0x88    \n"
    "csrw mstatus, %0" : "=r" (temp)
  );
}

// Disable Global Interrupt
static inline void __disable_irq(void) {
  uint32_t temp;
  __asm volatile(
    #if __GNUC__ > 10
    ".option arch, +zicsr \n"
    #endif
    "csrr %0, mstatus     \n"
    "andi %0, %0, ~0x88   \n"
    "csrw mstatus, %0" : "=r" (temp)
  );
}

// No OPeration
static inline void __NOP(void) {
  __asm volatile("nop");
}

// Enable NVIC interrupt (interrupt numbers)
static inline void NVIC_EnableIRQ(IRQn_Type IRQn) {
  NVIC->IENR[((uint32_t)(IRQn) >> 5)] = (1 << ((uint32_t)(IRQn) & 0x1F));
}

// Disable NVIC interrupt (interrupt numbers)
static inline void NVIC_DisableIRQ(IRQn_Type IRQn) {
  NVIC->IRER[((uint32_t)(IRQn) >> 5)] = (1 << ((uint32_t)(IRQn) & 0x1F));
}

// Get Interrupt Enable State
static inline uint32_t NVIC_GetStatusIRQ(IRQn_Type IRQn) {
  return((uint32_t) ((NVIC->ISR[(uint32_t)(IRQn) >> 5] & (1 << ((uint32_t)(IRQn) & 0x1F)))?1:0));
}

// Get Interrupt Pending State
static inline uint32_t NVIC_GetPendingIRQ(IRQn_Type IRQn) {
  return((uint32_t) ((NVIC->IPR[(uint32_t)(IRQn) >> 5] & (1 << ((uint32_t)(IRQn) & 0x1F)))?1:0));
}

// Set Interrupt Pending
static inline void NVIC_SetPendingIRQ(IRQn_Type IRQn) {
  NVIC->IPSR[((uint32_t)(IRQn) >> 5)] = (1 << ((uint32_t)(IRQn) & 0x1F));
}

// Clear Interrupt Pending
static inline void NVIC_ClearPendingIRQ(IRQn_Type IRQn) {
  NVIC->IPRR[((uint32_t)(IRQn) >> 5)] = (1 << ((uint32_t)(IRQn) & 0x1F));
}

// Get Interrupt Active State
static inline uint32_t NVIC_GetActive(IRQn_Type IRQn) {
  return((uint32_t)((NVIC->IACTR[(uint32_t)(IRQn) >> 5] & (1 << ((uint32_t)(IRQn) & 0x1F)))?1:0));
}

// Set Interrupt Priority
static inline void NVIC_SetPriority(IRQn_Type IRQn, uint8_t priority) {
  NVIC->IPRIOR[(uint32_t)(IRQn)] = priority;
}

// Wait for Interrupt
__attribute__( ( always_inline ) ) static inline void __WFI(void) {
  NVIC->SCTLR &= ~(1<<3);   // wfi
  asm volatile ("wfi");
}

// Wait for Events
__attribute__( ( always_inline ) ) static inline void __WFE(void) {
  uint32_t t;
  t = NVIC->SCTLR;
  NVIC->SCTLR |= (1<<3)|(1<<5);     // (wfi->wfe)+(__sev)
  NVIC->SCTLR = (NVIC->SCTLR & ~(1<<5)) | ( t & (1<<5));
  asm volatile ("wfi");
  asm volatile ("wfi");
}

// Set VTF Interrupt
static inline void SetVTFIRQ(uint32_t addr, IRQn_Type IRQn, uint8_t num, FunctionalState NewState) {
  if(num > 1)  return;
  if(NewState != DISABLE) {
    NVIC->VTFIDR[num] = IRQn;
    NVIC->VTFADDR[num] = ((addr&0xFFFFFFFE)|0x1);
  }
  else {
    NVIC->VTFIDR[num] = IRQn;
    NVIC->VTFADDR[num] = ((addr&0xFFFFFFFE)&(~0x1));
  }
}

// Initiate a system reset request
static inline void NVIC_SystemReset(void) {
  NVIC->CFGR = NVIC_KEY3|(1<<7);
}

// Return the Machine Status Register
static inline uint32_t __get_MSTATUS(void) {
  uint32_t result;
  __ASM volatile(
    #if __GNUC__ > 10
    ".option arch, +zicsr \n"
    #endif
    "csrr %0, mstatus" : "=r" (result)
  );
  return (result);
}

// Set the Machine Status Register
static inline void __set_MSTATUS(uint32_t value) {
  __ASM volatile(
    #if __GNUC__ > 10
    ".option arch, +zicsr \n"
    #endif
    "csrw mstatus, %0" : : "r" (value)
  );
}

// Return the Machine ISA Register
static inline uint32_t __get_MISA(void) {
  uint32_t result;
  __ASM volatile(
    #if __GNUC__ > 10
    ".option arch, +zicsr \n"
    #endif
    "csrr %0, misa" : "=r" (result)
  );
  return (result);
}

// Set the Machine ISA Register
static inline void __set_MISA(uint32_t value) {
  __ASM volatile(
    #if __GNUC__ > 10
    ".option arch, +zicsr \n"
    #endif
    "csrw misa, %0" : : "r" (value)
  );
}

// Return the Machine Trap-Vector Base-Address Register
static inline uint32_t __get_MTVEC(void) {
  uint32_t result;
  __ASM volatile(
    #if __GNUC__ > 10
    ".option arch, +zicsr \n"
    #endif
    "csrr %0, mtvec" : "=r" (result)
  );
  return (result);
}

// Set the Machine Trap-Vector Base-Address Register
static inline void __set_MTVEC(uint32_t value) {
  __ASM volatile(
    #if __GNUC__ > 10
    ".option arch, +zicsr \n"
    #endif
    "csrw mtvec, %0" : : "r" (value)
  );
}

// Return the Machine Seratch Register
static inline uint32_t __get_MSCRATCH(void) {
  uint32_t result;
  __ASM volatile(
    #if __GNUC__ > 10
    ".option arch, +zicsr \n"
    #endif
    "csrr %0, mscratch" : "=r" (result)
  );
  return (result);
}

// Set the Machine Seratch Register
static inline void __set_MSCRATCH(uint32_t value) {
  __ASM volatile(
    #if __GNUC__ > 10
    ".option arch, +zicsr \n"
    #endif
    "csrw mscratch, %0" : : "r" (value)
  );
}

// Return the Machine Exception Program Register
static inline uint32_t __get_MEPC(void) {
  uint32_t result;
  __ASM volatile(
    #if __GNUC__ > 10
    ".option arch, +zicsr \n"
    #endif
    "csrr %0, mepc" : "=r" (result)
  );
  return (result);
}

// Set the Machine Exception Program Register
static inline void __set_MEPC(uint32_t value) {
  __ASM volatile(
    #if __GNUC__ > 10
    ".option arch, +zicsr \n"
    #endif
    "csrw mepc, %0" : : "r" (value)
  );
}

// Return the Machine Cause Register
static inline uint32_t __get_MCAUSE(void) {
  uint32_t result;
  __ASM volatile(
    #if __GNUC__ > 10
    ".option arch, +zicsr \n"
    #endif
    "csrr %0, mcause" : "=r" (result)
  );
  return (result);
}

// Set the Machine Cause Register
static inline void __set_MCAUSE(uint32_t value) {
  __ASM volatile(
    #if __GNUC__ > 10
    ".option arch, +zicsr \n"
    #endif
    "csrw mcause, %0" : : "r" (value)
  );
}

// Return Vendor ID Register
static inline uint32_t __get_MVENDORID(void) {
  uint32_t result;
  __ASM volatile(
    #if __GNUC__ > 10
    ".option arch, +zicsr \n"
    #endif
    "csrr %0, mvendorid" : "=r" (result)
  );
  return (result);
}

// Return Machine Architecture ID Register
static inline uint32_t __get_MARCHID(void) {
  uint32_t result;
  __ASM volatile(
    #if __GNUC__ > 10
    ".option arch, +zicsr \n"
    #endif
    "csrr %0, marchid" : "=r" (result)
  );
  return (result);
}

// Return Machine Implementation ID Register
static inline uint32_t __get_MIMPID(void) {
  uint32_t result;
  __ASM volatile(
    #if __GNUC__ > 10
    ".option arch, +zicsr \n"
    #endif
    "csrr %0, mimpid" : "=r" (result)
  );
  return (result);
}

// Return Hart ID Register
static inline uint32_t __get_MHARTID(void) {
  uint32_t result;
  __ASM volatile(
    #if __GNUC__ > 10
    ".option arch, +zicsr \n"
    #endif
    "csrr %0, mhartid" : "=r" (result)
  );
  return (result);
}

// Return SP Register
static inline uint32_t __get_SP(void) {
  uint32_t result;
  __ASM volatile("mv %0, sp" : "=r" (result):);
  return (result);
}

#ifdef __cplusplus
};
#endif

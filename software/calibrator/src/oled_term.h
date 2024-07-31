// ===================================================================================
// SSD1306 128x64 Pixels OLED Terminal Functions                              * v1.0 *
// ===================================================================================
//
// Collection of the most necessary functions for controlling an SSD1306 128x64 pixels
// I2C OLED for the display of text in the context of emulating a terminal output.
//
// Functions available:
// --------------------
// OLED_init()              Init OLED display
// OLED_clear()             Clear screen of OLED display
// OLED_write(c)            Write a character or handle control characters
// OLED_print(s)            Print string on OLED display
// OLED_println(s)          Print string with newline
// OLED_printS(s)           Print string on OLED display
// OLED_printD(n)           Print decimal value
// OLED_printL(n)           Print hex long value
// OLED_printW(n)           Print hex word value
// OLED_printB(n)           Print hex byte value
// OLED_newline()           Print newline
//
// References:
// -----------
// - Neven Boyanov: https://github.com/tinusaur/ssd1306xled
// - Stephen Denne: https://github.com/datacute/Tiny4kOLED
// - David Johnson-Davies: http://www.technoblogy.com/show?TV4
// - TinyOLEDdemo: https://github.com/wagiminator/attiny13-tinyoleddemo
// - TinyTerminal: https://github.com/wagiminator/ATtiny85-TinyTerminal
// - USB2OLED: https://github.com/wagiminator/CH552-USB-OLED
//
// 2022 by Stefan Wagner: https://github.com/wagiminator

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "i2c_tx.h"

// OLED definitions
#define OLED_ADDR         0x78    // OLED write address (0x3C << 1)
#define OLED_CMD_MODE     0x00    // set command mode
#define OLED_DAT_MODE     0x40    // set data mode

// OLED commands
#define OLED_COLUMN_LOW   0x00    // set lower 4 bits of start column (0x00 - 0x0F)
#define OLED_COLUMN_HIGH  0x10    // set higher 4 bits of start column (0x10 - 0x1F)
#define OLED_MEMORYMODE   0x20    // set memory addressing mode (following byte)
#define OLED_COLUMNS      0x21    // set start and end column (following 2 bytes)
#define OLED_PAGES        0x22    // set start and end page (following 2 bytes)
#define OLED_STARTLINE    0x40    // set display start line (0x40-0x7F = 0-63)
#define OLED_CONTRAST     0x81    // set display contrast (following byte)
#define OLED_CHARGEPUMP   0x8D    // (following byte - 0x14:enable, 0x10: disable)
#define OLED_XFLIP_OFF    0xA0    // don't flip display horizontally
#define OLED_XFLIP        0xA1    // flip display horizontally
#define OLED_INVERT_OFF   0xA6    // set non-inverted display
#define OLED_INVERT       0xA7    // set inverse display
#define OLED_MULTIPLEX    0xA8    // set multiplex ratio (following byte)
#define OLED_DISPLAY_OFF  0xAE    // set display off (sleep mode)
#define OLED_DISPLAY_ON   0xAF    // set display on
#define OLED_PAGE         0xB0    // set start page (following byte)
#define OLED_YFLIP_OFF    0xC0    // don't flip display vertically
#define OLED_YFLIP        0xC8    // flip display vertically
#define OLED_OFFSET       0xD3    // set display offset (y-scroll: following byte)
#define OLED_COMPINS      0xDA    // set COM pin config (following byte)

// OLED functions
void OLED_init(void);             // OLED init function
void OLED_clear(void);            // OLED clear screen
void OLED_write(char c);          // OLED write a character or handle control characters
void OLED_print(char* str);       // OLED print string
void OLED_println(char* str);     // OLED print string with newline
void OLED_printD(uint32_t value); // print decimal value
void OLED_printL(uint32_t value); // print hex long value
void OLED_printW(uint16_t value); // print hex word value
void OLED_printB(uint8_t value);  // print hex byte value
#define OLED_newline() OLED_write('\n')   // print newline
#define OLED_printS OLED_print            // alias

#ifdef __cplusplus
};
#endif

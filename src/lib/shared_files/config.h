/**
 * Configuration header for the Miniware M2009 Modem Cartridge / Viditel Modem Emulator.
 * 
 * Author: Bart Eversdijk
 */

#pragma once

#define ENABLE_LOG 1

// Define either VIEWDATAEMULATOR or M2009EMULATOR via build flags (-D VIEWDATAEMULATOR or -D M2009EMULATOR)

#if defined(VIEWDATAEMULATOR)
#define  PIN_LED_GREEN    32   // Externe Groene LED
#define  PIN_LED_RED      33   // Externe Rode LED

#define RX1_PIN  16
#define TX1_PIN  17

#define RX2_PIN  4   // 25
#define TX2_PIN  26

#define PIN_CTS  21
#define PIN_RI   23

// If defined, we have only two LEDs for status indication (GREEN and RED) else 8
//#define TWOLEDS 

#endif
#if defined(M2009EMULATOR)
#define  PIN_LED_GREEN    17   // Externe Groene LED
#define  PIN_LED_RED      16   // Externe Rode LED
#define  TWOLEDS
#endif

#define PIN_BUS_OE 11
#define PIN_RESET_INT 14  // /RESET on GP14

#define DEFAULT_HOSTPORT 23
#define DEFAULT_FTP_PORT 21


// -------------------------------------------------------------------------------------------------
#ifdef TWOLEDS
#define LED_ON HIGH
#define LED_OFF LOW
#else
#define LED_ON LOW
#define LED_OFF HIGH
#endif


#if !(defined(VIEWDATAEMULATOR) ^ defined(M2009EMULATOR))
#error "VIEWDATAEMULATOR OR M2009EMULATOR must be defined, but not both"
#endif

#ifdef LOCAL_DEBUG_ENABLE
    #define DEBUG_IS_ON ENABLE_LOG
#else
    #define DEBUG_IS_ON ENABLE_LOG
#endif

#if ENABLE_LOG == 1
    #define LOG(format, ...) logFormat(format, ##__VA_ARGS__)
#else
    #define LOG(format, ...) // Do nothing
#endif

#if ENABLE_BUS_DEBUG == 1
    #define LOGDBG(...)    logFormatTitle("BUS TRACE", __VA_ARGS__)
#else
    #define LOGDBG(...)    // Do nothing
#endif

#if ENABLE_INTERNAL_PAGES_LOG == 1
    #define LOGIP(...)       logFormatTitle("INT-PAGES", __VA_ARGS__)
#else
    #define LOGIP(...)       // Do nothing
#endif

#if ENABLE_NETWORK_LOG == 1
    #define LOGNET(...)       logFormatTitle("NETWORK", __VA_ARGS__)
#else
    #define LOGNET(...)       // Do nothing
#endif

#if ENABLE_LOG_TELETEXT == 1
    #define LOGTXT(...)       logFormatTitle("TELETEXT", __VA_ARGS__)
#else
    #define LOGTXT(...)       // Do nothing
#endif

#if ENABLE_LOG_VIEWDATA == 1
    #define LOGVD(...)       logFormatTitle("VIEWDATA", __VA_ARGS__)
#else
    #define LOGVD(...)       // Do nothing
#endif

#if ENABLE_LOG_WIFI == 1
    #define LOGWIFI(...)       logFormatTitle("WIFI", __VA_ARGS__)
#else
    #define LOGWIFI(...)       // Do nothing
#endif

#if ENABLE_LOG_UART == 1
    #define LOGUART(...)       logFormatTitle("UART", __VA_ARGS__)
#else
    #define LOGUART(...)       // Do nothing
#endif

#if ENABLE_LOG_PHONE_BOOK == 1
    #define LOGPB(...)       logFormatTitle("PHONE_BOOK", __VA_ARGS__)
#else
    #define LOGPB(...)       // Do nothing
#endif

#if ENABLE_LOG_OTHER == 1
    #define LOGOTH(...)       logFormatTitle("OTHER", __VA_ARGS__)
#else
    #define LOGOTH(...)       // Do nothing
#endif

#if ENABLE_LOG_FTP == 1
    #define LOGFTP(...)       logFormatTitle("FTP", __VA_ARGS__)
#else
    #define LOGFTP(...)       // Do nothing
#endif

#if ENABLE_LOG_DIAL == 1
    #define LOGDIAL(...)       logFormatTitle("FTP", __VA_ARGS__)
#else
    #define LOGDIAL(...)       // Do nothing
#endif

#define LOG_BAUDRATE 115200

// De onderliggende functie (blijft hetzelfde)
void logInit(); 
void logFormat(const char* format, ...);
void logFormatTitle(const char* title, const char* format, ...);
/*
 * WiFi Settings and Phone Book Handling for Miniware M2009 Modem Cartridge / Viditel Modem Emulator.
 * Author: Bart Eversdijk
 *
 *  Description: Handles reading and saving phone book data to NVM and updating it via a web interface.
 */
#ifndef WIFI_PORTAL_H
#define WIFI_PORTAL_H

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <WiFiServer.h>
#include <queue>
#include <string>

// Define maximum lengths for the text fields
#define MAX_NUMBER_LEN 10
#define MAX_NAME_LEN   16
#define MAX_URL_LEN    52

// Total amount of records * compressedrecordsize can not exceed EEPROM_SIZE (2048 bytes)
#define MAX_PHONEBOOK_ITEMS 30

#define MAX_COMPRESSED_NUMBER_LEN 5
#define MAX_COMPRESSED_NAME_LEN 12
#define MAX_COMPRESSED_URL_LEN 44

struct CompressedPhoneItem {
  uint8_t bcdNumber[MAX_COMPRESSED_NUMBER_LEN];   // 10 digits in 5 bytes (BCD)
  uint8_t packedName[MAX_COMPRESSED_NAME_LEN];    // 16 characters packed in 12 bytes (6-bit packing)
  char url[MAX_COMPRESSED_URL_LEN];               // URL base (44 tekens + \0)
  uint8_t typeAndPrefix;                          // Bit 0-2: ServerType, Bit 6-7: HttpPrefix
}; 


#if defined(VIEWDATAEMULATOR)
const int RESET_WIFI_BUTTON_PIN = 0; // Pin for the physical Wi-Fi reset button (ESP32 BOOT button)
#endif

#define DNS_PORT 53

enum class connectionType {
    LOCALMENU,
    VIEWDATA,
    TELETEXT,
    OTHER,
#if defined(M2009EMULATOR)    
    P2000FTP,
#endif
    DISCONNECTED
};

struct WifiConfig {
  uint32_t magic;
  char ssid[33];      
  char password[65];  
  
  bool useStaticIP;
  char staticIP[16];   
  char gateway[16];
  char subnet[16];
};

enum ServerType : uint8_t {
    VIEWDATA = 0,
    TELETEXT = 1,
    BBS      = 2,
    OTHER    = 3,
#if defined(M2009EMULATOR)
    P2000FTP,  
    INTERNALMENU,
#endif
    NUM_SERVER_TYPES
};
//#define NUM_SERVER_TYPES 4

struct PhoneNumberToSite {
  char number[MAX_NUMBER_LEN+1];
  char name[MAX_NAME_LEN+1];
  char url[MAX_URL_LEN+1];
  ServerType type;
};

struct PhoneBookBlock {
  uint32_t magic;
  PhoneNumberToSite items[MAX_PHONEBOOK_ITEMS];
};

enum class connectionStatus {
    IDLE,
    CONNECTING,
    CONNECTED,
    DISCONNECTING
};

#if defined(VIEWDATAEMULATOR)
  struct UARTConfig {
    uint16_t baudrateDown;
    uint16_t baudrateUp;
    bool dataBits8;   // true for 8 data bits, false for 7 data bits
    bool stopBits1;   // true for 1 stop bit, false for 2 stop bits
    char parity;      // 'N' for None, 'E' for Even, 'O' for Odd
  };

  struct UARTConfigBlock {
    uint32_t magic;
    UARTConfig uart;
  };
  #define NUM_BAUD_RATES 10
  extern UARTConfig uartConfig;
#else
  #define NUM_BAUD_RATES 11
  struct UARTConfigBlock {
    uint32_t magic;
    uint16_t baudrateSim;
    uint16_t port;           // TCP port for the connection
  };
  extern UARTConfigBlock uartSimConfig;
#endif
extern uint16_t baudRates[NUM_BAUD_RATES];
extern const char * baudRatesTxt[NUM_BAUD_RATES];
void storeUARTConfig();
void readUARTConfig();
void handleSaveUARTConfig();


void wifiStartup();

void readPhoneBook();
void storePhonebook();
void handleSaveSettings();
void wifiStartup();
void handleStartCaptivePortal();
void startCaptivePortal();
void deviceRestart();
PhoneNumberToSite *lookIntoPhoneBook(char *number);

extern std::queue<uint8_t> rxBufferScc;
extern bool byteReadyToSend;
extern bool byteReadyForFirstRead;
extern PhoneNumberToSite phonebook[MAX_PHONEBOOK_ITEMS];

extern bool useStaticIPaddress;
// Defined in m2009-pico.ino
extern bool activeNetworkConnection;
extern String author;
extern String version;

#endif // WIFI_PORTAL_H

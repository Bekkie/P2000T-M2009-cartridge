/*
  Viditel modem Emulator for a P2000T
  Author: Bart Eversdijk
  
  License: MIT
  https://opensource.org/licenses/MIT

  Description: Serial (V.24) Viditel modem emulator for a P2000T
  Main program for handling the modem functions

  SAA5050 testpage on teletekstachief.nl page 887

  TODO:
  - Haiys protocol support/filter  ATDT01756664433
  - BBS esc comversion to color codes 

  */

// ArduinoJson by Benoit Blanchon
// base64 by Densaugeo
// lib_deps =
//       bblanchon/ArduinoJson @ ^7.0.0
//       densaugeo/base64 @ ~1.4.0


#include <Arduino.h>
#include <WiFi.h>
#include <queue>
#include <ESPmDNS.h>
#include "time.h"

#define ENABLE_BUS_DEBUG 0
#include "config.h"

#include "wifi-portal.h"
#include "internal-pages.h"
#include "teletext-proxy.h"
#include "viewdata.h"
#include "otherserver.h"
#if !(defined(VIEWDATAEMULATOR))
#error "VIEWDATAEMULATOR must be defined in config.h"
#endif

#if !defined(GIT_VERSION)
  #define GIT_VERSION "0.9b"
#endif
String version = GIT_VERSION;
String author = "(c) Bart Eversdijk";

std::queue<uint8_t> rxBuffer; // Internet data main queue for data reception
std::queue<uint8_t> txLocalBuffer;

HardwareSerial Serial75(1);  // Gebruik hardware UART1
HardwareSerial Serial1200(2);  // Gebruik hardware UART2

WiFiServer answerServer(DEFAULT_HOSTPORT);
WiFiClient answerClient;
volatile bool m_activeAnswerConnection = false; 
int m_answerPort = DEFAULT_HOSTPORT;

connectionType currentConnection = connectionType::LOCALMENU;
bool bLedStatus = false;

char timeString[20];
const char* dagenNL[] = {"zo", "ma", "di", "wo", "do", "vr", "za"};
const char* maandenNL[] = {"jan", "feb", "mrt", "apr", "mei", "jun", "jul", "aug", "sep", "okt", "nov", "dec"};


char * getCurrentTime()
{
    struct tm timeinfo;
    if(!getLocalTime(&timeinfo)){
      // Need exact size "vr 12 okt 12:00"
      strcpy(timeString, "Geen tijd info ");
      Serial.println("No valid time found");
      return timeString;
    }

    char clockStr[10];
    strftime(clockStr, sizeof(clockStr), "%H:%M", &timeinfo);

    snprintf(timeString, sizeof(timeString), "%s %02d %s %s", 
           dagenNL[timeinfo.tm_wday],       
           timeinfo.tm_mday,                
           maandenNL[timeinfo.tm_mon],
           clockStr); 

           return timeString;
}

void initTime()
{
    configTzTime("CET-1CEST,M3.5.0/2,M10.5.0/3", "pool.ntp.org");
  
    struct tm timeinfo;
    if(!getLocalTime(&timeinfo)){
      Serial.println("No valid time found");
      return;
    }
    Serial.println(getCurrentTime());
}


void handleDisconnect(uint8_t data)
{
    // Implement handling of no connection here
    // if characters received start main menu interaction
    if (data > 31 && data < 128) {
        setConnectionType(connectionType::LOCALMENU);
        startLocalConnection();
    }
    // AT checks? ESC/ESC/? check
}

void pollDisconnect()
{
    // Implement polling logic for disconnected state here
}

void handleReceiveByte(uint8_t data)
{
    static uint8_t escCount = 0;
    if (m_activeAnswerConnection) 
    {   
        if (answerClient.connected()) {
            // Write the byte to the server socket
            answerClient.write(data);
            return;
        } else {
            LOG("[Modem] Answer client disconnected.");
            m_activeAnswerConnection = false;
            digitalWrite(PIN_RI, LED_OFF);
        }
    }

    if ((escCount >= 2) && (data == '?' )) {
        // Handle the case where ESC ESC ? sequence is detected
        LOG("[Modem] ESC ESC ? sequence detected.");
        if (getConnectionType() ==  connectionType::VIEWDATA || getConnectionType() == connectionType::TELETEXT) {
            sendStatusPage(true);
        }
        escCount = 0; // Reset the escape count after handling
    }
    if (data == CHAR_ESCAPE) {
        escCount++;
    } else {
      escCount = 0;
    }

    Serial.printf("RECV: %02X (%c)\n", data, (data > 20 && data < 128) ? data : '?');
    if (m_statusPageActive) {
        LOG("[Modem] Status page is active.");
        handleSendLocal(data);
    } else {
        switch (getConnectionType()) {
            case connectionType::LOCALMENU:
                handleSendLocal(data);
                break;
            case connectionType::VIEWDATA:
                handleViewdata(data);
                break;
            case connectionType::TELETEXT:
                handleTeletext(data);
                break;
            case connectionType::DISCONNECTED:
                handleDisconnect(data);
                break;
            case connectionType::OTHER:
                handleOther(data);
                break;
        }
    }
    
}


const char* connectionTypeName(connectionType type)
{
    switch (type) {
        case connectionType::LOCALMENU:    return "connectionType::LOCALMENU";
        case connectionType::VIEWDATA:     return "connectionType::VIEWDATA";
        case connectionType::TELETEXT:     return "connectionType::TELETEXT";
        case connectionType::OTHER:        return "connectionType::OTHER";
        case connectionType::DISCONNECTED: return "connectionType::DISCONNECTED";
    }
    return "connectionType::UNKNOWN";
}

void setConnectionType(connectionType type)
{
    m_statusPageActive = false;
    if (currentConnection == type) {
        return; // No change needed
    }
    if ((currentConnection == connectionType::TELETEXT || currentConnection == connectionType::VIEWDATA)
        && type == connectionType::DISCONNECTED) {
        // Handle any cleanup needed when disconnecting from teletext
        currentConnection = connectionType::LOCALMENU;
        startLocalConnection();

    } else {
        currentConnection = type;
    }
    LOG("[Modem] Connection type changed to %s.", connectionTypeName(type));
}

connectionType getConnectionType()
{
    return (currentConnection);
}

void handleSendByte(uint8_t data)
{
    //LOG("[Modem] Send byte %02x (%c)", data, ((data > 31 && data < 127) ? data : '.'));
    if ((data == '\x0C') && (txLocalBuffer.size() > 0)) {
        // Clear the local buffer on form feed character, no need to continue sending current page
        txLocalBuffer = std::queue<unsigned char>();
    }
    txLocalBuffer.push(data);
}

void handleSendByteAlways(uint8_t data)
{
    // Compatible wrapper for handleSendByte (M2009-emulator)
    handleSendByte(data);
}

void disconnect() {
    LOG("[Modem] Handling disconnect.");
    // Implement the logic to handle disconnection here
    digitalWrite(PIN_CTS, HIGH);
    delay(1000);
    digitalWrite(PIN_CTS, LOW);
}

void handleCheckForNewVisitors()
{
    if (answerServer.hasClient()) {
        LOG("[Host] INCOMING CALL! The line is ringing...");
        if (getConnectionType() == connectionType::LOCALMENU) {
            answerClient = answerServer.accept();
            m_activeAnswerConnection = true;
            digitalWrite(PIN_RI, LED_ON);
            LOG("[Host] Host connection established.");
        } else {
            WiFiClient busyClient = answerServer.accept();
            busyClient.println("VIEWDATA BUSY"); 
            busyClient.stop();
            LOG("[Host] Sorry, line is busy!");
        }
    }
}

void clientSocketDataAvaiable()
{   // Read incoming data from the visitor and inject it directly into the P2000T read port
    if (m_activeAnswerConnection) {
        if (answerClient.connected()) {
            if (answerClient.available()) {
                // in host mode do not process, just pass it through
                handleSendByteAlways(answerClient.read());
            }
        } else {
            LOG("[Server] Caller has disconnected or hung up.");
            m_activeAnswerConnection = false;
            digitalWrite(PIN_RI, LED_OFF);
            return;            
        }
    }
}


void serialInit(int baudRateRxD, int baudRateTxD, int dataBits, char parity)
{
  uint32_t  config = SERIAL_8N1;
  switch (parity) {
    case 'N':
      config = (dataBits == 8) ? SERIAL_8N1 : SERIAL_7N1;
      break;
    case 'E':
      config = (dataBits == 8) ? SERIAL_8E1 : SERIAL_7E1;
      break;
    case 'O':
      config = (dataBits == 8) ? SERIAL_8O1 : SERIAL_7O1;
      break;
  }
  Serial.printf("[SETUP] UART1 set on %d baud %d%c1 (RX pin %d)\n", baudRateTxD, dataBits, parity, RX1_PIN);
  Serial75.begin(baudRateTxD, config, RX1_PIN, TX1_PIN);
  Serial75.setRxBufferSize(2048);
  Serial75.setTxBufferSize(2048);
    
  Serial.printf("[SETUP] UART2 set on %d baud %d%c1 (TX pin %d)\n", baudRateRxD, dataBits, parity, TX2_PIN);
  Serial1200.begin(baudRateRxD, config, RX2_PIN, TX2_PIN);
  Serial1200.setRxBufferSize(2048);
  Serial1200.setTxBufferSize(2048);
}


void setup() {
    Serial.begin(115200);
    pinMode(PIN_CTS, OUTPUT);
    pinMode(TX2_PIN, OUTPUT);
    pinMode(PIN_RI, OUTPUT);

    // ESP32 tell P2000T we are BUSY/PAUSE (transistor aan = 0V op CTS)
    digitalWrite(TX2_PIN, HIGH); // Set LED_ON to prevent rubbing the line
    digitalWrite(PIN_CTS, HIGH); 
    digitalWrite(PIN_RI, LED_OFF);

    Serial.printf("\n[SETUP] Starting P2000T Viditel modem emulator by %s, version %s\n", author.c_str(), version.c_str());

    pinMode(PIN_LED_GREEN, OUTPUT);
    pinMode(PIN_LED_RED, OUTPUT);

    // Activate the RED LED and switch off GREEN LED to indicate start of wifi initialization
    digitalWrite(PIN_LED_GREEN, LED_OFF);
    digitalWrite(PIN_LED_RED, LED_ON);
    
    // Read phone directory from preferences if empty give some default entries
    readPhoneBook();

    // Start Wi-Fi
    wifiStartup();
    
    Serial.println("[SETUP] ESP32 Viditel Emulator Started!");

    readUARTConfig();
    serialInit(uartConfig.baudrateDown, uartConfig.baudrateUp, uartConfig.dataBits8 ? 8 : 7, uartConfig.parity);
    
    // Configure NTP time synchronization
    initTime();

    // Start mDNS with the hostname "p2000t-m2009" on port 23
    if (m_answerPort > 0) {
        if (MDNS.begin("viditel")) {
            LOG("[mDNS] Started with hostname 'viditel'");
            MDNS.addService("viewdata", "tcp", m_answerPort);
            MDNS.addServiceTxt("viewdata", "tcp", "model", "P2000T-Viditel");
            MDNS.addService("telnet", "tcp", m_answerPort);
            MDNS.addServiceTxt("telnet", "tcp", "model", "P2000T-Viditel");
        } 
        answerServer = WiFiServer(m_answerPort);
        answerServer.begin();
        LOG("[Host] Telnet server started on port %d. Waiting for remote callers...", m_answerPort);
    }
    // Turn off the RED LED to indicate the successful completion of wifi initialization
    digitalWrite(PIN_LED_RED, LED_OFF);   
    
    // ESP32 tell P2000T we are READY
    digitalWrite(PIN_CTS, LOW); 

    // Send the mainpage on startup
    startLocalConnection();
}

// TODO: check wifi status and updat GREEN LED accordingly

void loop() {

    if (Serial75.available()) {
#ifdef TWOLEDS    
        digitalWrite(PIN_LED_RED, LED_ON);
#endif    
        uint8_t incomingByte = Serial75.read();
        //Serial.printf("UART 1 Recv byte: %02X (%c)\n", incomingByte, (incomingByte > 20 && incomingByte < 128) ? incomingByte : '?');
        handleReceiveByte(incomingByte);
#ifdef TWOLEDS
    } else {
        digitalWrite(PIN_LED_RED, LED_OFF);
#endif    
    }

    if (Serial1200.available()) {
#ifdef TWOLEDS    
        digitalWrite(PIN_LED_RED, LED_ON);
#endif    
        uint8_t incomingByte = Serial1200.read();
        //Serial.printf("UART 2 Received byte: %02X (%c)\n", incomingByte, (incomingByte > 20 && incomingByte < 128) ? incomingByte : '?');
        handleReceiveByte(incomingByte);
#ifdef TWOLEDS
    } else {
        digitalWrite(PIN_LED_RED, LED_OFF);
#endif      
    }

    if (!txLocalBuffer.empty()) {
#ifdef TWOLEDS    
        digitalWrite(PIN_LED_GREEN, bLedStatus ? LED_ON : LED_OFF);
        bLedStatus = !bLedStatus;
#endif    
        uint8_t byteToProcess = txLocalBuffer.front();
        txLocalBuffer.pop();
        //Serial.printf("SND: %02X\n", byteToProcess);
        Serial1200.write(byteToProcess);
#ifdef TWOLEDS    
    } else {
        digitalWrite(PIN_LED_GREEN, LED_ON);
#endif    
    }
  
    switch (getConnectionType()) 
    {
        case connectionType::LOCALMENU:
            pollLocal();
            break;
        case connectionType::VIEWDATA:
            pollViewdata();
            break;
        case connectionType::TELETEXT:
            pollTeletext();
            break;
        case connectionType::DISCONNECTED:
            pollDisconnect();
            break;
        case connectionType::OTHER:
            pollOther();
            break;
    }

    // Handle incoming visitor connections
    handleCheckForNewVisitors();
    clientSocketDataAvaiable();

    // Update the GREEN LED based on WiFi connection status
    digitalWrite(PIN_LED_GREEN, WiFi.status() == WL_CONNECTED ? LED_ON : LED_OFF);
    
    delay(1);
}

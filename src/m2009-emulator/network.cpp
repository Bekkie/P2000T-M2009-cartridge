/**
 * Network setup and communication for the Raspberry Pico W M2009 Emulator
 * 
 * Author: Bart Eversdijk
 */

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <LEAmDNS.h>
#include "time.h"

#include "pico/multicore.h"
#include "pico/time.h"
#include "pico/util/queue.h"

#define ENABLE_NETWORK_LOG 0
#include "config.h"

#include "wifi-portal.h"
#include "network.h"
#include "internal-pages.h"
#include "teletext-proxy.h"
#include "viewdata.h"
#include "otherserver.h"
#include "p2000ftp.h"

volatile bool bResetPending = false;
volatile bool bStartCaptivePortal = false;
unsigned long m_baudrateSimDelayUp;
unsigned long m_baudrateSimDelayDown;
uint16_t m_answerPort;

// Host mode: listens for incoming Telnet connections on port 23
WiFiServer answerServer(DEFAULT_HOSTPORT);
WiFiClient answerClient;
volatile bool m_activeAnswerConnection = false;      // Active anwer connection

// External network connection status
volatile bool m_activeDialoutConnection = false;     // Active dialout connection

// Viewdata special command state management
uint8_t telnetState = 0;
uint32_t scc_fifodata = 0;
connectionType currentConnection = connectionType::DISCONNECTED;

char timeString[20];
const char* dagenNL[] = {"zo", "ma", "di", "wo", "do", "vr", "za"};
const char* maandenNL[] = {"jan", "feb", "mrt", "apr", "mei", "jun", "jul", "aug", "sep", "okt", "nov", "dec"};

char * getCurrentTime()
{
    struct tm timeinfo;
    time_t now = time(nullptr);
    // Before the NTP sync the clock is still near 1970
    if (now < 1000000000 || !localtime_r(&now, &timeinfo)) {
      // Need exact size "vr 12 okt 12:00"
      strcpy(timeString, "Geen tijd info ");
      LOG("[Network] No valid time found");
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
    //  NTP sets the system clock in UTC; the time zone comes from TZ
    setenv("TZ", "CET-1CEST,M3.5.0/2,M10.5.0/3", 1);
    tzset();
    NTP.begin("pool.ntp.org");

    // Wait up to 5 s for the first NTP sync
    uint32_t start = millis();
    while (time(nullptr) < 1000000000 && millis() - start < 5000) {
      delay(100);
    }
    LOG(getCurrentTime());
}


// ============================================================================
// VIEWDATA ENCODING / DECODING
// ============================================================================
// Encode outgoing data, applying parity and bit length settings.
uint8_t encodeOutgoingData(uint8_t data,  uint8_t* sccWriteRegs) {
  // DETERMINE DATA BITS PER CHARACTER FROM WR5 (Channel A)
  // WR5 Bits 6-5: 00 = 5 bits, 10 = 6 bits, 01 = 7 bits, 11 = 8 bits
  uint8_t wr5_config = sccWriteRegs[5];
  uint8_t bitLengthBits = (wr5_config >> 5) & 0x03;
  
  // In 8-bit mode, ignore parity entirely 
  // and return the raw 8-bit data to prevent corruption of bit 7.
  if (bitLengthBits == 0x03) {
    return data;
  }

  // Default to 7 data bits (0x01 config)
  uint8_t cleanData = data & 0x7F;

  // DETERMINE PARITY CONFIGURATION FROM WR4 
  uint8_t wr4_config = sccWriteRegs[4]; 

  // Check WR4 Bit 0: Parity Enable (1 = Enabled, 0 = Disabled)
  if (!(wr4_config & 0x01)) {
    return cleanData; // Parity is disabled, send raw sized bits directly
  }

  // Count the number of set bits ('1's) within the data frame
  uint8_t bitCount = 0;
  for (int i = 0; i < 8; i++) {
    if ((cleanData >> i) & 1) bitCount++;
  }

  // Check WR4 Bit 1: Parity Even/Odd (1 = Even Parity, 0 = Odd Parity)
  bool isEvenParity = (wr4_config & 0x02) != 0;

  if (isEvenParity) {
    // EVEN PARITY: Total number of 1-bits must be even
    if (bitCount % 2 != 0) {
      return (cleanData | 0x80); // Inject parity bit into bit 7 position
    }
  } else {
    // ODD PARITY: Total number of 1-bits must be odd
    if (bitCount % 2 == 0) {
      return (cleanData | 0x80); // Inject parity bit into bit 7 position
    }
  }
  return cleanData;
}


// Returns -1 when the byte must not be forwarded (telnet overhead), otherwise the byte (0..255)
int decodeIncomingViewdata(uint8_t incomingByte, uint8_t* sccWriteRegs) {
  // === REMOVE INTERNET/TELNET OVERHEAD ===
  if (telnetState == 0) {
    if (incomingByte == 0xFF) { // Telnet IAC start
      telnetState = 1;
      return -1;
    }
  } else if (telnetState == 1) {
    if (incomingByte == 0xFF) {
      telnetState = 0; // escaped 0xFF data byte
      return 0xFF;
    }
    if (incomingByte >= 0xFB && incomingByte <= 0xFE) {
      telnetState = 2; // WILL/WONT/DO/DONT commando
    } else {
      telnetState = 0; // 2-byte commando
    }
    return -1;
  } else if (telnetState == 2) {
    telnetState = 0; // Optie-byte compleet
    return -1;
  }

  // WR3 bits 7-6 (receiver bits/char): 00 = 5, 01 = 7, 10 = 6, 11 = 8
  uint8_t bitLengthBits = (sccWriteRegs[3] >> 6) & 0x03;

  // 8-bit mode: pass through unchanged (also 0x00 and 0x80-0xFF)
  if (bitLengthBits == 0x03) {
    return incomingByte;
  }

  // 7-bit mode: remove parity bit (bit 7)
  return incomingByte & 0x7F;
}

// ============================================================================
// Check for new incoming external visitors (the Pico is being called)
// ============================================================================
void handleCheckForNewVisitors()
{
  if (answerServer.hasClient()) {
    LOG("[Host] New connection request!");
    if (!answerClient.connected()) {
       
       answerClient = answerServer.accept();
       LOG("[Host] INCOMING INTERNET CALL! The line is ringing...");

       sendFifoCommand(FifoCommand::INCOMING_CALL);
    
    } else {
       WiFiClient busyClient = answerServer.accept();
       busyClient.println("VIEWDATA BUSY"); 
       busyClient.stop();
    }
  }
}


// ============================================================================
// Check for unexpected disconnection on the internet side && dial timeout
// ============================================================================
void handleConnectionAlive()
{
    // Check for unexpected disconnection on the client side
    if ((m_activeAnswerConnection || m_incomingCall) && !answerClient.connected()) {
        LOG("[Telnet] Connection closed by the remote host.");
        m_activeAnswerConnection = false;
        m_incomingCall = false;
        sendFifoCommand(FifoCommand::CONNECTION_LOST);
        LOGNET("[SCC emulation] CARRIER DETECT LOST (CD = 0)");
    }
}

void handleDisconnect(uint8_t data)
{
    // Implement handling of no connection here
}
void pollDisconnect()
{
    // Implement polling logic for disconnected state here
}

void disconnect() {
    LOG("[Modem] Handling disconnect.");
    sendFifoCommand(FifoCommand::CONNECTION_LOST);
}

void clientSocketDataAvaiable()
{   // Read incoming data from the visitor and inject it directly into the P2000T read port
    if (m_activeAnswerConnection) {
        if (answerClient.connected()) {
            if (answerClient.available() && !queue_is_full(&interCoreRxQueue)) {
                // in hosy mode do not process, just pass it through
                int decoded = decodeIncomingViewdata(answerClient.read(), m_sccWriteRegsB_ptr);
                uint8_t viewDataByte = (uint8_t)decoded;

                if (decoded >= 0) {
                    LOGNET("Injecting byte from internet: 0x%02X", viewDataByte);
                    // Add the byte to the Channel A queue
                    if (!queue_try_add(&interCoreRxQueue, &viewDataByte)) {
                        LOG("[PICO] Inter-core RxD queue full!");
                    }
                }
            }
        } else {
            LOG("[Server] Caller has disconnected or hung up.");
            return;            
        }
    }
}

void handleSendByte(uint8_t data)
{
    // Inject the byte directly into the Z80 read port
    LOGNET("byte from internet: (%c) 0x%02X", ((data > 31 && data < 127) ? data : '.'), data);
    // Clear the interCoreRxQueue on form feed character if received, to speed up pages loading
    if (data == CHAR_FORM_FEED) {
        clearRxQueue();
    }   
    if (!queue_try_add(&interCoreRxQueue, &data)) {
        LOG("[PICO] Inter-core RX queue full!");
    }
}

void handleSendByteAlways(uint8_t data)
{
    // Inject the byte directly into the Z80 read port without any conditions
    LOGNET("byte from internet (always): (%c) 0x%02X", ((data > 31 && data < 127) ? data : '.'), data);
    if (!queue_try_add(&interCoreRxQueue, &data)) {
        LOG("[PICO] Inter-core RX queue full!");
    }
}

bool isRxQueueEmpty()
{
    return queue_is_empty(&interCoreRxQueue);
}
bool isRxQueueFull()
{
    return queue_is_full(&interCoreRxQueue);
}
void clearRxQueue()
{
    uint8_t dummyByte;
    while (queue_try_remove(&interCoreRxQueue, &dummyByte)) {
    }
}


const char* connectionTypeName(connectionType type)
{
    switch (type) {
        case connectionType::LOCALMENU:    return "connectionType::LOCALMENU";
        case connectionType::VIEWDATA:     return "connectionType::VIEWDATA";
        case connectionType::TELETEXT:     return "connectionType::TELETEXT";
        case connectionType::OTHER:        return "connectionType::OTHER";
        case connectionType::P2000FTP:     return "connectionType::P2000FTP";
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

    LOG("[Modem] Connection type changed to %s.", connectionTypeName(type));
    currentConnection = type;
}

connectionType getConnectionType()
{
    return (currentConnection);
}

void handleTxQueue()
{
    static unsigned long lastTxTime = 0;
    if (millis() < (m_baudrateSimDelayUp + lastTxTime)) {
        return; // Limit TX handling to simulate baud rate delay
    }
    lastTxTime = millis();

    if (m_activeDialoutConnection) 
    {
        if (!queue_is_empty(&interCoreTxQueue)) {
            uint8_t data;
            if (queue_try_remove(&interCoreTxQueue, &data)) {

                static uint8_t escCount = 0;

                if (getConnectionType() <= connectionType::OTHER) {
                    // Not all server type support the ESC ESC ? sequence handling
                    // Only handle it for connection types up to OTHER
                    if ((escCount >= 2) && (data == '?' )) {
                        // Handle the case where ESC ESC ? sequence is detected
                        LOG("[Modem] ESC ESC ? sequence detected.");
                        if (getConnectionType() == connectionType::VIEWDATA || getConnectionType() == connectionType::TELETEXT) {
                            sendStatusPage(true);
                        }
                        escCount = 0; // Reset the escape count after handling
                    }
                    if (data == CHAR_ESCAPE) {
                        escCount++;
                    } else {
                        escCount = 0;
                    }
                }

                LOGNET("RECV: %02X (%c)", data, (data > 20 && data < 128) ? data : '?');
                if (m_statusPageActive) {
                    LOG("[Modem] Status page is active.");
                    handleSendLocal(data);
                } else {
                    switch (getConnectionType()) {
                        case connectionType::LOCALMENU:
                            LOGNET("[Modem] Local menu.");
                            handleSendLocal(data);
                            break;
                        case connectionType::VIEWDATA:
                            LOGNET("[Modem] Viewdata.");
                            handleViewdata(data);
                            break;
                        case connectionType::TELETEXT:
                            LOGNET("[Modem] Teletext.");
                            handleTeletext(data);
                            break;
                        case connectionType::OTHER:
                            LOGNET("[Modem] Other.");
                            handleOther(data);
                            break;
                        case connectionType::P2000FTP:
                            LOGNET("[Modem] P2000 FTP.");
                            handleFtp(data);
                            break;
                        case connectionType::DISCONNECTED:
                            LOGNET("[Modem] Disconnected.");
                            handleDisconnect(data);
                            break;
                    }
                }
            }
        }
    }

    if (m_activeAnswerConnection && answerClient.connected()) 
    {
        if (!queue_is_empty(&interCoreTxQueue)) {
            uint8_t protocolByte;
            if (queue_try_remove(&interCoreTxQueue, &protocolByte)) {
                // Write the byte to the server socket
                answerClient.write(encodeOutgoingData(protocolByte, m_sccWriteRegsA_ptr));
            }
        }
    }
}

void handleDialedNumber()
{
    // Core 0 reports that a number has been dialed
    LOG("[PICO] Number dialed: %s\n", m_phoneNumber);
    PhoneNumberToSite * siteInfo = lookIntoPhoneBook(m_phoneNumber);

    if (siteInfo == nullptr || siteInfo->url[0] == '\0') {
        LOG("[PICO] Invalid site information or missing server address.");
        sendFifoCommand(FifoCommand::WRONG_NUMBER);
        return;
    } 
    
    if (siteInfo->type != ServerType::INTERNALMENU && WiFi.status() != WL_CONNECTED) {
        // For internal pages, WiFi is not required. For other server types, WiFi must be connected.
        LOG("[PICO] WiFi is not connected.");
        sendFifoCommand(FifoCommand::NO_NETWORK_RESPONSE);
        return;
    }

    LOG("[Modem] Connecting to: %s ", siteInfo->name);
    switch(siteInfo->type) 
    {
        case ServerType::INTERNALMENU:
            startLocalConnection();
            m_activeDialoutConnection = true;
            sendFifoCommand(FifoCommand::SUCCESSFUL_CONNECTION);
            setConnectionType(connectionType::LOCALMENU);
            break;

        case  ServerType::TELETEXT:
            if (openTeletextConnection(std::string(siteInfo->url))) {
                // Connection successful
                m_activeDialoutConnection = true;
                sendFifoCommand(FifoCommand::SUCCESSFUL_CONNECTION);
                setConnectionType(connectionType::TELETEXT);
            } else {
                // Connection failed
                sendFifoCommand(FifoCommand::BUSY_LINE);
            }
            break;

        case ServerType::VIEWDATA:
            if (openViewdataConnection(std::string(siteInfo->url))) {
                // Connection successful
                m_activeDialoutConnection = true;
                sendFifoCommand(FifoCommand::SUCCESSFUL_CONNECTION);
                telnetState = 0;
                setConnectionType(connectionType::VIEWDATA);
            } else {
                // Connection failed
                sendFifoCommand(FifoCommand::BUSY_LINE);
            }
            break;

        case ServerType::BBS:
            // Not yet implemented
            // startBbsConnection(std::string(siteInfo->url)); 
            sendFifoCommand(FifoCommand::WRONG_NUMBER);
            break;

        case ServerType::OTHER:
            if (openOtherConnection(std::string(siteInfo->url))) {
                // Connection successful
                m_activeDialoutConnection = true;
                sendFifoCommand(FifoCommand::SUCCESSFUL_CONNECTION);
                setConnectionType(connectionType::OTHER);
            } else {
                // Connection failed
                sendFifoCommand(FifoCommand::BUSY_LINE);
            }
            break;

        case ServerType::P2000FTP:
            // No dial out support for P2000FTP (only speed dial)
            sendFifoCommand(FifoCommand::BUSY_LINE);
            break;

        default:
            // Handle other server types if needed
            sendFifoCommand(FifoCommand::WRONG_NUMBER);
            break;
    }
}

void handleSpeedDialNumber() {
    // Core 0 reports that a speed dial number has been selected
    LOG("[PICO] Speed dial number selected: %d", m_speedDialIndex);
    
    if (m_speedDialIndex >= 0 && m_speedDialIndex < MAX_PHONEBOOK_ITEMS) {
        LOG("[Network] Speed dial command for server %s\n", phonebook[m_speedDialIndex].name);
        
        switch (phonebook[m_speedDialIndex].type)
        {
            case  ServerType::TELETEXT:
                if (openTeletextConnection(std::string(phonebook[m_speedDialIndex].url))) {
                    // Connection successful
                    m_activeDialoutConnection = true;
                    setConnectionType(connectionType::TELETEXT);
                }
                break;

            case ServerType::VIEWDATA:
                if (openViewdataConnection(std::string(phonebook[m_speedDialIndex].url))) {
                    // Connection successful
                    m_activeDialoutConnection = true;
                    telnetState = 0;
                    setConnectionType(connectionType::VIEWDATA);
                }
                break;

            case ServerType::BBS:
                // Not yet implemented
                // startBbsConnection(std::string(siteInfo->url)); 
                break;
            
            case ServerType::P2000FTP:
                if (openFtpConnection(std::string(phonebook[m_speedDialIndex].url))) {
                    m_activeDialoutConnection = true;
                    setConnectionType(connectionType::P2000FTP);
                }
                break;

            case ServerType::OTHER:
                if (openOtherConnection(std::string(phonebook[m_speedDialIndex].url))) {
                    // Connection successful
                    m_activeDialoutConnection = true;
                    setConnectionType(connectionType::OTHER);
                }
                break;
        }
    }
    if (m_speedDialIndex == SPECIAL_FTP_SERVER) {
        LOG("[Network] FTP speed dial command");
        // Open the selected FTP server
        if (openFtpConnection()) {
            m_activeDialoutConnection = true;
            setConnectionType(connectionType::P2000FTP);
        }
    }

    LOG("[Network] Active dialout connection: %s", m_activeDialoutConnection ? "true" : "false");
    if (m_activeDialoutConnection) {
       // If we go from no active dialout connection to active answer connection -> report this to Core 0
       sendFifoCommand(FifoCommand::SPEED_DIAL_CONNECTED);
    }
}

void handleSpeedDialUrl()
{
    // Core 0 reports that a number has been speed dialed
    std::string url = std::string(m_speedDialUrl);
    LOG("[PICO] Speed URL requested: %s type: %d\n", url.c_str(), m_speedDialserverType);

    switch (m_speedDialserverType)
    {
        case  ServerType::TELETEXT:
            if (openTeletextConnection(url)) {
                // Connection successful
                m_activeDialoutConnection = true;
                setConnectionType(connectionType::TELETEXT);
            }
            break;

        case ServerType::VIEWDATA:
            if (openViewdataConnection(url)) {
                // Connection successful
                m_activeDialoutConnection = true;
                telnetState = 0;
                setConnectionType(connectionType::VIEWDATA);
            }
            break;

        case ServerType::BBS:
            // Not yet implemented
            // startBbsConnection(std::string(m_speedDialUrl)); 
            break;
        
        case ServerType::P2000FTP:
            if (openFtpConnection(url)) {
                m_activeDialoutConnection = true;
                setConnectionType(connectionType::P2000FTP);
            }
            break;

        case ServerType::OTHER:
            if (openOtherConnection(url)) {
                // Connection successful
                m_activeDialoutConnection = true;
                setConnectionType(connectionType::OTHER);
            }
            break;
    }

    LOG("[Network] Active dialout connection: %s", m_activeDialoutConnection ? "true" : "false");
    if (m_activeDialoutConnection) {
       // If we go from no active dialout connection to active answer connection -> report this to Core 0
       sendFifoCommand(FifoCommand::SPEED_DIAL_CONNECTED);
    }
}

void handlePhoneOnHook() {
    // Core 0 reports that the phone has gone on hook
    LOG("[PICO] Phone on hook");
        
    answerClient.stop();
    m_activeAnswerConnection = false;

    switch (getConnectionType()) 
    {
        case connectionType::LOCALMENU:
            break;
        case connectionType::VIEWDATA:
            phoneOnHookViewdata();
            break;
        case connectionType::TELETEXT:
            phoneOnHookTeletext();
            break;
        case connectionType::OTHER:
            phoneOnHookOther();
            break;
        case connectionType::P2000FTP:
            phoneOnHookFtp();
            break;
    }

    m_activeDialoutConnection = false;
    digitalWrite(PIN_LED_RED, LED_OFF);
    setConnectionType(connectionType::DISCONNECTED);
}

void p2000tResetISR() {
    static unsigned long lastInterruptTime = 0;
    
    if (digitalRead(PIN_RESET_INT) != HIGH) {
        // interrupt pin change from HIGH to LOW (RISE)
        lastInterruptTime = millis();
    } else {
        // The pin changed from LOW to HIGH (FALL)
        unsigned long interruptTime = millis() - lastInterruptTime;
        if (interruptTime > 4000 && interruptTime < 7000)  {
            // Long reset detected  4 < t < 7 seconds, reset the system
            LOG("[PICO] Long reset detected, restarting device...");
            deviceRestart();
            return;
        }
        if (interruptTime > 8000 && interruptTime < 15000)  {
            // Long reset detected  8 < t < 15 seconds, reset the system
            LOG("[PICO] Long reset detected, start captive portal...");
            bStartCaptivePortal = true;
        }
        bResetPending = true;
    }
}

void networkSetup() {
    // ----------------------------------------------------------------------------
    // SETUP SERIAL COMMUNICATION 
    // ----------------------------------------------------------------------------
    
    Serial.printf("\nStarting P2000T M2009 emulator by %s, version %s\n", author.c_str(), version.c_str());
    pinMode(PIN_RESET_INT, INPUT_PULLUP);
    
    // Activate the RED LED to indicate start of wifi initialization
    digitalWrite(PIN_LED_RED, LED_ON);
    attachInterrupt(digitalPinToInterrupt(PIN_RESET_INT), p2000tResetISR, CHANGE);

     // ----------------------------------------------------------------------------
    // SETUP COMMUNICATION with Wi-Fi and initialize host server
    // ----------------------------------------------------------------------------
    // Read phone directory from preferences if empty give some default entries
    readPhoneBook();
    readUARTConfig();

    // Start Wi-Fi
    wifiStartup();
    WiFi.noLowPowerMode(); // power-save adds up to ~1 s latency to the first TCP connect
    LOG("Wi-Fi connected! IP address: %s\n", WiFi.localIP().toString().c_str());

    // Start mDNS with the hostname "p2000t-m2009" on port 23
    if (m_answerPort > 0) {
        if (MDNS.begin("m2009")) {
            LOG("[mDNS] Started with hostname 'm2009'");
            MDNS.addService("viewdata", "tcp", m_answerPort);
            MDNS.addServiceTxt("viewdata", "tcp", "model", "P2000T-M2009");
            MDNS.addService("telnet", "tcp", m_answerPort);
            MDNS.addServiceTxt("telnet", "tcp", "model", "P2000T-M2009");
            
            MDNS.announce(); 
        } 
        answerServer = WiFiServer(m_answerPort);
        answerServer.begin();
        LOG("[Host] Telnet server started on port %d. Waiting for remote callers...", m_answerPort);
    } 

    // Configure NTP time synchronization
    initTime();

    // Turn off the RED LED to indicate the successful completion of wifi initialization
    digitalWrite(PIN_LED_RED, LED_OFF); 
}

void networkLoop() {
    if (rp2040.fifo.pop_nb(&scc_fifodata)) {
        switch (scc_fifodata & 0xFF)
        {
            case FifoCommand::PHONE_ON_HOOK:
                // Close the active network connections
                handlePhoneOnHook();
                break;

            case FifoCommand::NUMBER_DIALED:
                handleDialedNumber();
                break;

            case FifoCommand::SPEED_DIAL_NUMBER:
                handleSpeedDialNumber();
                break;

            case FifoCommand::SPEED_DIAL_URL:
                handleSpeedDialUrl();
                break;

            default:
                break;
        }
    }

    // Check for unexpected disconnection on the internet side
    handleConnectionAlive();

    // Send P2000T queue data to the network connected client/server
    handleTxQueue();

    // Check for new incoming external visitors (the ESP32 is being called)
    handleCheckForNewVisitors();

    // Only check for client socket data for incoming connection, dial out is handled via poll<Type>()
    clientSocketDataAvaiable();

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
        case connectionType::OTHER:
            pollOther();
            break;
        case connectionType::P2000FTP:
            pollFtp();
            break;
        case connectionType::DISCONNECTED:
            pollDisconnect();
            break;
    }

    MDNS.update();

    if (bResetPending) {
        Serial.println("P2000T RESET detected (GP14)");
        bResetPending = false;

        handlePhoneOnHook();
        m_port_44 = 0;      // This variable belongs to core 0, but we reset it here for consistency

        if (bStartCaptivePortal) {
            startCaptivePortal();
            bStartCaptivePortal = false;
        }
    }
}


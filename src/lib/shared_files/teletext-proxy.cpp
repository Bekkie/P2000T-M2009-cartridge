/**
 * Teletext implementation for the Miniware M2009 Modem Cartridge / Viditel Modem Emulator.
 * 
 * Author: Bart Eversdijk
 * 
 * Teletext proxy relies on iflots teletext routines
 *
 */
#include <string>

#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <base64.hpp>
#include <vector>
#include <string>

#define ENABLE_LOG_TELETEXT 1
#include "config.h"

#include "wifi-portal.h"
#include "internal-pages.h"
#include "teletext-proxy.h"
#include "teletekst.h"
 

WiFiClientSecure secureClient;
WiFiClient unsecureClient;

std::string keyboardTxtBuffer;
TeletextPage teletextPage;

std::string teletextHost = "";
const size_t autoNextPageInterval = 30000; // Interval in milliseconds for auto next page
size_t autoNextPageTimer = 0; 
bool autoNextPage = false;
bool autoSubpagesPaused = false;

connectionStatus teletextStatus = connectionStatus::IDLE;
size_t teletextConnectionTimer = 0; 
// Skip length for time string and copyright line
#define LENGTH_TIME_STRING 17

std::string convertTeletext2Viewdata[0x20] = {
        " ",                    // 0x00
        CTRL_RED_TXT,           // 0x01
        CTRL_GREEN_TXT,         // 0x02
        CTRL_YELLOW_TXT,        // 0x03
        CTRL_BLUE_TXT,          // 0x04   
        CTRL_MAGENTA_TXT,       // 0x05
        CTRL_CYAN_TXT,          // 0x06
        CTRL_WHITE_TXT,         // 0x07
        CTRL_FLASH_ON,          // 0x08
        CTRL_FLASH_OFF,         // 0x09
        "\x0a",                 // 0x0A
        CTRL_INVERSE_OFF,       // 0x0B
        CTRL_NORMAL_SIZE,       // 0x0C
        CTRL_DOUBLE_SIZE,       // 0x0D
        " ",                    // 0x0E
        " ",                    // 0x0F
        CTRL_BLACK_GRPH,        // 0x10
        CTRL_RED_GRPH,          // 0x11
        CTRL_GREEN_GRPH,        // 0x12
        CTRL_YELLOW_GRPH,       // 0x13
        CTRL_BLUE_GRPH,         // 0x14
        CTRL_MAGENTA_GRPH,      // 0x15
        CTRL_CYAN_GRPH,         // 0x16
        CTRL_WHITE_GRPH,        // 0x17
        " ",                    // 0x18 -- Hidden text in teletext
        CTRL_NORMAL_GRPH,       // 0x19
        CTRL_SMALL_GRPH,        // 0x1A
        " ",                    // 0x1B
        CTRL_BACKG_BLACK,       // 0x1C
        CTRL_BACKG_COLOR,       // 0x1D
        CTRL_REPEAT_GRPH,       // 0x1E
        CTRL_NOREPEAT_GRPH      // 0x1F
};

static const std::string PAGE_MSG_HEADER = "\x0C"
    CTRL_YELLOW_TXT PAGE_HEADER CTRL_CR_LF
    CTRL_GREEN_GRPH + std::string(38, '\x2c') +  CTRL_CR_LF
    CTRL_CR_LF;

void teletextSentString(std::string message)  
{
    for (char c : message) {
        handleSendByte(static_cast<uint8_t>(c));
    }
}

void teletextSentData(uint8_t *data, size_t length)  
{
    for (int i=0; i < length; i++) {
        handleSendByte(data[i]);
    }
}

void teletextInfoPage(std::string title, std::string message1, std::string message2)
{
    // Page not found
    teletextSentString(PAGE_MSG_HEADER);
    teletextSentString(CTRL_CR_LF);
    teletextSentString(CTRL_DOUBLE_SIZE);
    teletextSentString(CTRL_MAGENTA_TXT);
    teletextSentString(title);
    for (int i = 0; i < 4; i++) {
        teletextSentString(CTRL_CR_LF);
    }
    teletextSentString(CTRL_DOUBLE_SIZE);
    teletextSentString(message1);
    teletextSentString(CTRL_CR_LF);
    teletextSentString(CTRL_CR_LF);
    teletextSentString(CTRL_CR_LF);
    teletextSentString(CTRL_YELLOW_TXT);
    teletextSentString(message2);
    for (int i = 0; i < 11; i++) {
        teletextSentString(CTRL_CR_LF);
    }
    teletextSentString(CTRL_GREEN_GRPH);
    teletextSentString(std::string(38, '\x2c'));
    teletextSentString(CTRL_CR_LF);
    teletextSentString(CTRL_CYAN_TXT);
}

void disconnectTeletext(const char * reason)
{
    LOGTXT("Disconnecting from teletext server. %s", reason);
    
    // Fill the rest of the page with spaces to ensure slow display
    teletextInfoPage("", reason, "");

    setCursorPosition(40, 42, CURSOR_HIDDEN);
    teletextPage.nextPage = "100";
    teletextPage.prevPage = "100";
    teletextPage.hasSubPage = false;

    teletextConnectionTimer = millis() + 4000; // Set the timer to 4 seconds in the future
    teletextStatus = connectionStatus::DISCONNECTING;
    
    disconnect();
}

bool getTeletextPage(std::string page) 
{
    HTTPClient http;

    digitalWrite(PIN_LED_RED, LED_ON);
    
    if (teletextHost.rfind("https://", 0) == 0) {
        http.begin(secureClient, (teletextHost + page).c_str());
    } else {
        // Open connection to the given URL
        http.begin(unsecureClient, (teletextHost + page).c_str());
    }
    
    int httpCode = http.GET(); 

    if (httpCode > 0) {
      // Check if the page was successfully found (HTTP 200 OK)
      if (httpCode == HTTP_CODE_OK) {
        String payload = http.getString();
        Serial.println("[TELETEXT] JSON succesvol gedownload!");

        JsonDocument doc; 
        DeserializationError error = deserializeJson(doc, payload);

        http.end();
        // We are loading a sub page of the current teletext page, so no clear screen
        bool bSkipClearScreen = (teletextPage.number == page.substr(0, 3));
        
        if (!error) {
            
            teletextPage.number = page.substr(0, 3); // Store main page number
            LOGTXT("Current page: %s", teletextPage.number.c_str());
            teletextPage.prevPage = doc["prevPage"].as<std::string>();
            teletextPage.nextPage = doc["nextPage"].as<std::string>();
            teletextPage.nextSubPage = doc["nextSubPage"].as<std::string>();

            // Determine if the current page has a subpage ()
            // if teletextPage.number != page -> we are a subpage so hasSubPage should be true also
            teletextPage.hasSubPage =  (teletextPage.nextSubPage != "" || teletextPage.number != page);

            // Check if the JSON contains a binaryDisplay section (used of archived teletext pages)
            if (!doc["binaryDisplay"].isNull()) { 
                Serial.println("[TELETEXT] Base64 decoding");

                // Base64 decoding here!
                std::string base64String = doc["binaryDisplay"].as<std::string>();
                std::string pagebuffer;
                        
                pagebuffer.resize(base64String.length());
                int decodeSize = decode_base64(
                        reinterpret_cast<unsigned char*>(&base64String[0]),
                        reinterpret_cast<unsigned char*>(&pagebuffer[0])
                );
                LOGTXT("JSON page decoded: %d bytes", decodeSize);

                if (bSkipClearScreen) {
                    setCursorPosition(1, 1, CURSOR_HIDDEN); // Move cursor to top-left corner
                } else {
                    handleSendByte(static_cast<uint8_t>(CHAR_FORM_FEED)); // Clear screen
                    handleSendByte(CHAR_HIDE_CURSOR);
                }
                
                // Send time string at the top of the page
                teletextSentString(CTRL_YELLOW_TXT);
                teletextSentString(getCurrentTime());
                teletextSentString(CTRL_WHITE_TXT);

                // Skip first 17 bytes to give space for time
                for (size_t i = LENGTH_TIME_STRING; i < decodeSize; ++i) {
                    uint8_t byteVal = static_cast<uint8_t>(pagebuffer[i]);
                    if (byteVal < 0x20) {
                        //LOGTXT("Convert byte %02x encoded %02x %02x", byteVal, (unsigned char)convert_table[byteVal][0], (unsigned char)convert_table[byteVal][1]);
                        teletextSentString(convertTeletext2Viewdata[byteVal]);
                    } else {
                        if (byteVal > 127) {
                            LOGTXT("Illegal byte decoded: %02x", pagebuffer[i]);
                            handleSendByte('?');
                        } else {
                            handleSendByte(byteVal);
                        }
                    }
                }
            } else if (!doc["content"].isNull()) {
                LOGTXT("Processing HTML content to Videotext...");
               
                // use ifilot's routine for decoding HTML content to P2000T-bytes
                uint8_t screen[TELETEKST_SCREEN_SIZE];
                uint8_t next_subpage = 0u;
                uint16_t pageNo = atoi(page.c_str()); 
                
                bool result = teletekst_decode_nos_json(payload.c_str(), payload.length(), pageNo, screen, &next_subpage);

                LOGTXT("Convert done with result: %s", result ? "OK" : "ERROR");
                if (result) {
                    // Convert the decoded screen bytes to Viewdata format and send them to the modem
                    if (bSkipClearScreen) {
                        setCursorPosition(1, 1, CURSOR_HIDDEN); // Move cursor to top-left corner
                    } else {
                        handleSendByte(static_cast<uint8_t>(CHAR_FORM_FEED)); // Clear screen
                        handleSendByte(CHAR_HIDE_CURSOR);
                    }

                    // Send time string at the top of the page
                    teletextSentString(CTRL_YELLOW_TXT);
                    teletextSentString(getCurrentTime());
                    teletextSentString(CTRL_WHITE_TXT);

                    // Skip first 17 bytes to give space for time
                    for (size_t i = LENGTH_TIME_STRING; i < TELETEKST_SCREEN_SIZE; ++i) {
                        uint8_t byteVal = screen[i];
                        if (byteVal < 0x20) {
                            //LOGTXT("Convert byte %02x encoded %02x %02x", byteVal, (unsigned char)convertTeletext2Viewdata[byteVal][0], (unsigned char)convertTeletext2Viewdata[byteVal][1]);
                            teletextSentString(convertTeletext2Viewdata[byteVal]);
                        } else {
                            if (byteVal > 127) {
                                LOGTXT("Illegal byte decoded: %02x", byteVal);
                                handleSendByte('?');
                            } else {
                                handleSendByte(byteVal);
                            }
                        }
                    }
                } else {
                    LOGTXT("Failed to decode HTML content to P2000T format");
                    teletextInfoPage(ERROR_PAGE_TITLE, "Pagina kon niet worden gedecodeerd", "");
                    teletextPage.nextSubPage = "";
                    teletextPage.hasSubPage = false;
                }
            }
            if (autoNextPage || autoSubpagesPaused) {
                setCursorPosition(36, 1, CURSOR_HIDDEN);
                handleSendByte(autoNextPage ? 'V' : 'A'); // Show auto next page mode
            } else {
                setCursorPosition(37, 1, CURSOR_HIDDEN);
            }

            // Reset the auto next page timer
            autoNextPageTimer = millis(); 
            digitalWrite(PIN_LED_RED, LED_OFF);
            return true;

        } else {
          Serial.print("[TELETEXT] JSON parsing failed: ");
          Serial.println(error.c_str());
          teletextInfoPage(ERROR_PAGE_TITLE, error.c_str(), "");
          teletextPage.nextSubPage = "";
          teletextPage.hasSubPage = false;
        }
      } else if (httpCode == 404) {
          Serial.print("[TELETEXT] Page not found (HTTP 404)");
          char pageMessage[64];
          snprintf(pageMessage, sizeof(pageMessage), "    Gevraagde pagina: %s", page.c_str());
          teletextInfoPage(PAGE_NOT_FOUND, pageMessage, "Maak een nieuwe keuze aub");
          teletextPage.nextPage = "100";
          teletextPage.prevPage = "100";
          teletextPage.nextSubPage = "";
          teletextPage.hasSubPage = false;
          digitalWrite(PIN_LED_RED, LED_OFF);
          return true;

      } else {
          char errorMessage[64];
          Serial.print("[TELETEXT] Error HTTP-code: ");
          Serial.println(errorMessage);
          snprintf(errorMessage, sizeof(errorMessage), " %d %s", httpCode, http.errorToString(httpCode).c_str());
          teletextInfoPage(ERROR_PAGE_TITLE, errorMessage, "");
          teletextPage.nextSubPage = "";
          teletextPage.hasSubPage = false;
      }
    } else {
      Serial.print("[TELETEXT] Fout tijdens HTTP-verzoek: ");
      Serial.println(http.errorToString(httpCode).c_str());
      teletextInfoPage(ERROR_PAGE_TITLE, http.errorToString(httpCode).c_str(), "");
      teletextPage.nextSubPage = "";
      teletextPage.hasSubPage = false;
    }
    
    // Stop the auto next page timer
    autoNextPageTimer = 0; 
    digitalWrite(PIN_LED_RED, LED_OFF);
    return false;
}

void handleTeletextPageReload()
{
    if (teletextStatus == connectionStatus::CONNECTED) {
        getTeletextPage(teletextPage.number);
    }
}

void startTeletextConnection(int serverId)
{
    if ((serverId < 0 || serverId >= MAX_PHONEBOOK_ITEMS) && phonebook[serverId].type != ServerType::TELETEXT) {
        LOGTXT("Invalid server ID: %d", serverId);
        teletextInfoPage(ERROR_PAGE_TITLE, "Onbekende server ID", "Maak een nieuwe keuze aub");
        return;
    }

    teletextHost = std::string(phonebook[serverId].url);

    //if (!teletextHost.starts_with("http://") && !teletextHost.starts_with("https://")) {
    if (teletextHost.rfind("http://", 0) != 0 && teletextHost.rfind("https://", 0) != 0) {
        teletextHost = std::string("http://") + std::string(phonebook[serverId].url);
    }
    LOGTXT("Preparing teletext connection to: %s",teletextHost.c_str());
    
    teletextInfoPage(CONNECTING_TITLE, phonebook[serverId].name, teletextHost);

    // Tell that the next poll we want to start a teletext connection
    // In between we inform the user that we are connecting to the teletext server
    // Wait 4 seconds before attempting the connection to finish page loading
    teletextConnectionTimer = millis() + 4000;
    
    setConnectionType(connectionType::TELETEXT);
    teletextStatus = connectionStatus::CONNECTING;
}

bool openTeletextConnection(std::string url)
{
    teletextHost = std::string(url);
    if (teletextHost.rfind("http://", 0) != 0 && teletextHost.rfind("https://", 0) != 0) {
        teletextHost = std::string("http://") + teletextHost;
    }
    LOGTXT("Preparing teletext connection to: %s",teletextHost.c_str());
    
    if (getTeletextPage("100")) {
        LOGTXT("Connection established");
        teletextStatus = connectionStatus::CONNECTED;
        setConnectionType(connectionType::TELETEXT); 
        return true;
    } 
    LOGTXT("Connection failed");
    return false;
}

void phoneOnHookTeletext()
{
    if (secureClient.connected()) {
        secureClient.stop();
    }
    if (unsecureClient.connected()) {
        unsecureClient.stop();
    }
}

void pollTeletext()
{
    // Implement polling logic for teletext here
            
    switch(teletextStatus) {

        case connectionStatus::IDLE:
            break;
    
        case connectionStatus::CONNECTING:
            if (millis() < teletextConnectionTimer) {
                return; // Wait until the timer expires
            }
            LOGTXT("Attempting to start teletext connection");
            secureClient.setInsecure(); // Skip HTTPS certificate verification

            if (!getTeletextPage("100")) {
                LOGTXT("Connection failed");
                teletextConnectionTimer = millis() + 4000; // Set the timer to 4 seconds in the future
                teletextStatus = connectionStatus::DISCONNECTING;
            } else {
                LOGTXT("Connection established");
                teletextStatus = connectionStatus::CONNECTED;
            }
            return; // Exit the function no polling until connection is made

        case connectionStatus::CONNECTED:
            if ((autoNextPageTimer == 0) && !keyboardTxtBuffer.empty()) {
                return;
            }

            if (millis() - autoNextPageTimer >= autoNextPageInterval) {
                
                if (autoNextPage) {
                    if (teletextPage.nextSubPage != "") {
                        LOGTXT("Auto next Subpage: %s", teletextPage.nextSubPage.c_str());
                        getTeletextPage(teletextPage.nextPage.c_str());
                    } else if (teletextPage.nextPage != "") {
                        LOGTXT("Auto next page: %s", teletextPage.nextPage.c_str());
                        getTeletextPage(teletextPage.nextPage.c_str());
                    } else {
                        LOGTXT("Auto next page not available, back to page 100.");
                        getTeletextPage("100");
                    }
                    autoNextPageTimer = millis(); // Reset de timer
                } 
                else if (teletextPage.hasSubPage && !autoSubpagesPaused) {
                    LOGTXT("Next sub page: %s", teletextPage.nextSubPage.c_str());
                    if (teletextPage.nextSubPage != "") {
                        getTeletextPage(teletextPage.nextSubPage.c_str());
                    } else {
                        getTeletextPage(teletextPage.number.c_str());
                    }
                    autoNextPageTimer = millis(); // Reset de timer
                }
            }
            break;

        case connectionStatus::DISCONNECTING:
            if (millis() < teletextConnectionTimer) {
                return; // Wait until the timer expires
            }
            setConnectionType(connectionType::DISCONNECTED);
            teletextStatus = connectionStatus::IDLE;
            break;
    }
}


void handleTeletext(uint8_t data)
{
    switch(data)
    {
        case CHAR_BACKSPACE:  // Handle Backspace
            if (!keyboardTxtBuffer.empty()) {
                keyboardTxtBuffer.pop_back();
                handleSendByte('\b');
            }
            break;
        
        case '\x5f': // = # 
        case CHAR_CR: // Handle Carriage Return // Do not handle newline to prevent double handling
            if (keyboardTxtBuffer.length() > 0) {
                if (keyboardTxtBuffer == "*0") 
                {
                    getTeletextPage("100");
                }
                else if (keyboardTxtBuffer == "*90") 
                {
                    LOGTXT("Verbreek lokale verbinding.");
                    disconnectTeletext("          Goodbye");
                }
                else
                {
                    LOGTXT("Geef pagina %s.", keyboardTxtBuffer.c_str());
                    if (keyboardTxtBuffer[0] == '*') {
                       keyboardTxtBuffer.erase(0, 1); // remove * from page number
                    }
                    getTeletextPage(keyboardTxtBuffer.c_str());
                }
                keyboardTxtBuffer = ""; // Clear the keyboard buffer
            } else
            {
                LOGTXT("Geef volgende pagina");
                if (teletextPage.nextPage != "") {
                    getTeletextPage(teletextPage.nextPage.c_str());
                }
            }
            break;
        
        case CHAR_CURSOR_RIGHT:
        case '\x2e': // = >
        case 'n':
        case 'N':
            keyboardTxtBuffer = "";
            if (teletextPage.nextPage != "") {
                getTeletextPage(teletextPage.nextPage.c_str());
            }
            break;

        case CHAR_CURSOR_UP:
        case '\x2c': // = <
        case 'p':
        case 'P':
            keyboardTxtBuffer = "";
            if (teletextPage.prevPage != "") {
                getTeletextPage(teletextPage.prevPage.c_str());
            }
            break;

        case 'A':
        case 'a':
            keyboardTxtBuffer = "";
            // Pause / resume auto subpages
            autoSubpagesPaused = !autoSubpagesPaused; // Toggle auto subpages pause state
            LOGTXT("Auto subpages %s.", autoSubpagesPaused ? "paused" : "resumed");
            setCursorPosition(36, 1, CURSOR_HIDDEN);
            handleSendByte(autoSubpagesPaused ? 'A' : ' ');
            break;
        
        case 'V':
        case 'v':
            keyboardTxtBuffer = "";
            // Pause / resume auto subpages
            autoNextPage = !autoNextPage; // Toggle auto subpages pause state
            LOGTXT("Auto nextpages %s.", autoNextPage ? "resumed" : "paused");
            setCursorPosition(36, 1, CURSOR_HIDDEN);
            handleSendByte(autoNextPage ? 'V' : ' ');
            break;

        case 's':
        case 'S':
            keyboardTxtBuffer = "";
            if (teletextPage.nextSubPage != "") {
                getTeletextPage(teletextPage.nextSubPage.c_str());
            } else {
                getTeletextPage(teletextPage.number.c_str());
            }
            break;

        case 'i':
        case 'I':
            keyboardTxtBuffer = "";
            getTeletextPage("100");
            break;

        case '*':
            if (keyboardTxtBuffer.length() == 0) {
                keyboardTxtBuffer = "*";
                handleSendByte(data);
            }
            break;

        case '0':
        case '1':
        case '2':
        case '3':
        case '4':
        case '5':
        case '6':
        case '7':
        case '8':
        case '9':
            if (keyboardTxtBuffer.length() > 2 || (keyboardTxtBuffer[0] == '*' && keyboardTxtBuffer.length() > 3)) {
                // Ovewrite last number if more than 2 digits are entered
                keyboardTxtBuffer.pop_back();
                handleSendByte('\b');
            }
            keyboardTxtBuffer += data;
            handleSendByte(CHAR_SHOW_CURSOR);
            handleSendByte(data);
            break;

        default:
            break;
    }
}


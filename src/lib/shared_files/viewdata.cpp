/**
 * Viewdata implementation for the Miniware M2009 Modem Cartridge / Viditel Modem Emulator.
 * 
 * Author: Bart Eversdijk
 *
 */
#define ENABLE_LOG_VIEWDATA 1
#include "config.h"

#include "wifi-portal.h"
#include "internal-pages.h"
#include "teletext-proxy.h"
#include "viewdata.h"


// External network connection status
WiFiClient serverClient;

std::string viewdataHost = "";
int viewdataPort = 80;
int iGoodbyeStrIndex = 0;

connectionStatus viewdataStatus = connectionStatus::IDLE;
size_t viewdataConnectionTimer = 0;

std::string GoodByStr = "NO CARRIER";

void parseUrl(const std::string& url, std::string& outHost, int& outPort, int defaultPort = 80) {
    std::string temp = url;
    
    size_t protocolPos = temp.find("://");
    if (protocolPos != std::string::npos) {
        temp = temp.substr(protocolPos + 3);
    }
    size_t slashPos = temp.find('/');
    if (slashPos != std::string::npos) {
        temp = temp.substr(0, slashPos);
    }
    size_t colonPos = temp.find(':');
    if (colonPos != std::string::npos) {
        outHost = temp.substr(0, colonPos);
        std::string portStr = temp.substr(colonPos + 1);
        
        char *end = nullptr;
        long value = strtol(portStr.c_str(), &end, 10);
        outPort = (end != portStr.c_str() && value > 0 && value <= 65535) ? (int)value : defaultPort;
    } else {
        outHost = temp;
        outPort = defaultPort;
    }
}

void disconnectViewdata()
{
    // Implementation for disconnecting the viewdata connection
    LOGVD("Disconnecting from viewdata server");
    if (serverClient.connected()) {
        serverClient.stop();
    } 
    char buf[] = "\n\r  CARRIER LOST  \n";
    for (size_t i = 0; i < sizeof(buf) - 1; ++i) {
        handleSendByte(buf[i]);
    }
    viewdataStatus = connectionStatus::DISCONNECTING;
    viewdataConnectionTimer = millis() + 2000;
    disconnect();
}

void startViewdataConnection(int serverId)
{
    // Implementation for starting a viewdata connection
    if ((serverId < 0 || serverId > MAX_PHONEBOOK_ITEMS) && phonebook[serverId].type != ServerType::VIEWDATA) {
        LOGVD("Invalid server ID: %d", serverId);
        teletextInfoPage(ERROR_PAGE_TITLE, "Onbekende server ID", "Maak een nieuwe keuze aub");
        return;
    }

    std::string fullUrl = std::string(phonebook[serverId].url);
    parseUrl(fullUrl, viewdataHost, viewdataPort, 80);
    
    char fullUrlCStr[40];
    snprintf(fullUrlCStr, sizeof(fullUrlCStr), "%s @ %d", viewdataHost.c_str(), viewdataPort);
    
    LOGVD("Starting viewdata connection to: %s", fullUrlCStr);
    teletextInfoPage(CONNECTING_TITLE, phonebook[serverId].name, fullUrlCStr);

    iGoodbyeStrIndex = 0;

    // Tell that the next poll we want to start a viewdata connection
    // In between we inform the user that we are connecting to the teletext server
    // Wait 4 seconds before attempting the connection to finish page loading
    viewdataConnectionTimer = millis() + 4000;

    setConnectionType(connectionType::VIEWDATA);
    viewdataStatus = connectionStatus::CONNECTING;
}

bool openViewdataConnection(std::string url)
{
    parseUrl(url, viewdataHost, viewdataPort, 80);

    digitalWrite(PIN_LED_RED, LED_ON);
    if (serverClient.connect(viewdataHost.c_str(), viewdataPort)) {
        LOGVD("Connection established");
        viewdataStatus = connectionStatus::CONNECTED;
        digitalWrite(PIN_LED_RED, LED_OFF);
        return true;
    } 

    LOGVD("Connection failed");
    digitalWrite(PIN_LED_RED, LED_OFF);
    return false;
    
}

void phoneOnHookViewdata()
{
    if (serverClient.connected()) {
        serverClient.stop();
    } 
}

void handleViewdata(uint8_t data)
{
    // Implementation for handling incoming viewdata data
    if (serverClient.connected())
    {
        // Write the byte directly to the server socket
        serverClient.write(data);
    } else {
        // Handle the case when the server is not connected
        LOGVD("No connection to Viewdata server");
        disconnectViewdata();
    }
}

void handleViewdataPageReload()
{
    handleViewdata('\x3A');
    handleViewdata('0');
    handleViewdata('0');
    handleViewdata('\x5F');
}

void pollViewdata()
{
    // Implementation for polling the viewdata connection
    switch (viewdataStatus) {

        case connectionStatus::IDLE:
            // No active connection
            break;
    
        case connectionStatus::CONNECTING:
            if (millis() < viewdataConnectionTimer) {
                return; // Wait until the timer expires
            }
            digitalWrite(PIN_LED_RED, LED_ON);
            if (serverClient.connect(viewdataHost.c_str(), viewdataPort)) {
                LOGVD("Connection established");
                viewdataStatus = connectionStatus::CONNECTED;
            } else {
                LOGVD("Connection failed");
                teletextInfoPage("Failed to connect", "Pas serverinstellingen aan", "");
                viewdataStatus = connectionStatus::DISCONNECTING;
                viewdataConnectionTimer = millis() + 4000;
            }
            digitalWrite(PIN_LED_RED, LED_OFF);
            return; // Exit the function no polling until connection is made
    
        case connectionStatus::CONNECTED:
            // Connection established
            if (serverClient.connected()) {
                if (serverClient.available()) {
                    uint8_t internetByte = serverClient.read() & 0x7F;

                    // Intercept "\nNO CARRIER\n" OR "\rNO CARRIER\r" OR "\r\nNO CARRIER\r\n" sequences
                    static bool bLastCR = false;
                    
                    if (bLastCR || iGoodbyeStrIndex > 0) {
                        //LOGVD("%s [%c] (iGoodbyeStrIndex: %d)", GoodByStr.c_str(), GoodByStr[iGoodbyeStrIndex], iGoodbyeStrIndex);
                        if (internetByte == GoodByStr[iGoodbyeStrIndex] || iGoodbyeStrIndex == GoodByStr.length()) {
                            LOGVD("Matching NO CARRIER sequence");
                            iGoodbyeStrIndex++;
                            if (iGoodbyeStrIndex > GoodByStr.length() && (internetByte == '\r' || internetByte == '\n')) {
                                LOGVD("Detected NO CARRIER sequence");
                                // Last character of the "NO CARRIER" sequence is not send to keep to terminal online
                                handleSendByte('-');
                                handleSendByte('-');
                                disconnectViewdata();
                                return;
                            }
                        } else {
                            iGoodbyeStrIndex = 0;
                        }
                    }
                    bLastCR = (internetByte == '\r') || (internetByte == '\n');
                    handleSendByte(internetByte);
                }
            } else {
                // Handle the case when the server is not connected
                LOGVD("Lost connection to Viewdata server");
                disconnectViewdata();
            }
            break;

        case connectionStatus::DISCONNECTING:
            viewdataStatus = connectionStatus::IDLE;
            setConnectionType(connectionType::DISCONNECTED);
            break;
    }
}
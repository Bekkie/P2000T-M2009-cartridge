/**
 * Other server implementation for the Miniware M2009 Modem Cartridge / Viditel Modem Emulator.
 *
 * Author: Bart Eversdijk
 */
#define ENABLE_LOG_OTHER 1
#include "config.h"

#include "wifi-portal.h"
#include "internal-pages.h"
#include "teletext-proxy.h"
#include "viewdata.h"
#include "otherserver.h"

// External network connection status
WiFiClient otherClient;

std::string otherHost = "";
int otherPort = 80;

connectionStatus otherStatus = connectionStatus::IDLE;
size_t otherConnectionTimer = 0;

void disconnectOther()
{
    // Implementation for disconnecting the other connection
    if (otherClient.connected()) {
        otherClient.stop();
    } 
    otherStatus = connectionStatus::DISCONNECTING;
    disconnect();
}

void phoneOnHookOther()
{
    if (otherClient.connected()) {
        otherClient.stop();
    }
}

void startOtherConnection(int serverId)
{
    // Implementation for starting an other connection
    if ((serverId < 0 || serverId >= MAX_PHONEBOOK_ITEMS) && phonebook[serverId].type != ServerType::OTHER) {
        LOGOTH("Invalid server ID: %d", serverId);
        teletextInfoPage(ERROR_PAGE_TITLE, "Onbekende server ID", "Maak een nieuwe keuze aub");
        return;
    }

    std::string fullUrl = std::string(phonebook[serverId].url);
    parseUrl(fullUrl, otherHost, otherPort, 80);
    
    char fullUrlCStr[40];
    snprintf(fullUrlCStr, sizeof(fullUrlCStr), "%s @ %d", otherHost.c_str(), otherPort);
    
    LOGOTH("Starting other connection to: %s", fullUrlCStr);
    teletextInfoPage(CONNECTING_TITLE, phonebook[serverId].name, fullUrlCStr);

    // Tell that the next poll we want to start other connection
    // In between we inform the user that we are connecting to the teletext server
    // Wait 4 seconds before attempting the connection to finish page loading
    otherConnectionTimer = millis() + 4000;

    setConnectionType(connectionType::OTHER);
    otherStatus = connectionStatus::CONNECTING;
}

bool openOtherConnection(std::string url)
{
    parseUrl(url, otherHost, otherPort, 80);

    digitalWrite(PIN_LED_RED, LED_ON);
    if (otherClient.connect(otherHost.c_str(), otherPort)) {
        LOGOTH("Connection established");
        otherStatus = connectionStatus::CONNECTED;
        digitalWrite(PIN_LED_RED, LED_OFF);
        return true;
    } 

    LOGOTH("Connection failed");
    digitalWrite(PIN_LED_RED, LED_OFF);
    return false;
}

void handleOther(uint8_t data)
{
    // Implementation for handling incoming other data
    if (otherClient.connected())
    {
        // Write the byte directly to the server socket
        otherClient.write(data);
    } else {
        // Handle the case when the server is not connected
        LOGOTH("No connection to Other server");
        disconnectOther();
    }
}

void pollOther()
{
    // Implementation for polling the other connection
    switch (otherStatus) {

        case connectionStatus::IDLE:
            // No active connection
            break;
    
        case connectionStatus::CONNECTING:
            if (millis() < otherConnectionTimer) {
                return; // Wait until the timer expires
            }
            digitalWrite(PIN_LED_RED, LED_ON);
            if (otherClient.connect(otherHost.c_str(), otherPort)) {
                LOGOTH("Connection established");
                otherStatus = connectionStatus::CONNECTED;
            } else {
                LOGOTH("Connection failed");
                teletextInfoPage("Failed to connect", "Pas serverinstellingen aan", "");
                otherStatus = connectionStatus::DISCONNECTING;
                otherConnectionTimer = millis() + 4000;
            }
            digitalWrite(PIN_LED_RED, LED_OFF);
            return; // Exit the function no polling until connection is made
    
        case connectionStatus::CONNECTED:
            // Connection established
            if (otherClient.connected()) {
                if (otherClient.available()) {
                    uint8_t internetByte = otherClient.read();
                    handleSendByteAlways(internetByte);
                }
            } else {
                LOGOTH("Lost connection to Other server");
                disconnectOther();
                otherStatus = connectionStatus::IDLE;
            }
            // Handle the case when the server is not connected
            if (!otherClient.connected()) {
                LOGOTH("Lost connection to Other server");
                disconnectOther();
            }
            break;

        case connectionStatus::DISCONNECTING:
            otherStatus = connectionStatus::IDLE;
            setConnectionType(connectionType::DISCONNECTED);
            break;
    }  
}
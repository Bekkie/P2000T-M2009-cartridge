/**
 * FTP server handling implementation for the Miniware M2009 Modem Cartridge
 *
 * Author: Bart Eversdijk
 */

#if defined(M2009EMULATOR)  // Only include this code for the M2009 emulator
#define ENABLE_LOG_FTP 1
#include "config.h"

#include <algorithm>
#include <cctype>

#include "wifi-portal.h"
#include "network.h"
#include "internal-pages.h"
#include "p2000ftp.h"
#include "viewdata.h"  // parseUrl function

// External network connection status
WiFiClient ftpClient;

std::string ftpHost = "";
int ftpPort = DEFAULT_FTP_PORT;
std::string ftpCommandCapture = "";
bool qotationMarkFound = false;

int selectedServerIdx = -1;
std::string customServerUrl = "";

connectionStatus ftpStatus = connectionStatus::IDLE;

static bool isValidServerUrl(const std::string& url)
{
    const size_t hostStart = url.compare(0, 7, "http://") == 0 ? 7 : 0;
    const size_t colon = url.find(':', hostStart);
    const size_t hostEnd = colon == std::string::npos ? url.size() : colon;
    if (hostEnd == hostStart || hostEnd - hostStart > 253) {
        return false;
    }

    if (colon != std::string::npos) {
        unsigned int port = 0;
        for (size_t pos = colon + 1; pos < url.size(); ++pos) {
            if (url[pos] < '0' || url[pos] > '9') {
                return false;
            }
            port = port * 10 + (url[pos] - '0');
            if (port > 65535) {
                return false;
            }
        }
        if (port == 0) {
            return false;
        }
    }

    bool numericHost = true;
    size_t labelStart = hostStart;
    size_t labelCount = 0;
    unsigned int octet = 0;
    bool validIpv4 = true;
    for (size_t pos = hostStart; pos <= hostEnd; ++pos) {
        if (pos == hostEnd || url[pos] == '.') {
            const size_t labelLength = pos - labelStart;
            if (labelLength == 0 || labelLength > 63 || url[labelStart] == '-' || url[pos - 1] == '-') {
                return false;
            }
            if (labelLength > 3 || octet > 255) {
                validIpv4 = false;
            }
            ++labelCount;
            labelStart = pos + 1;
            octet = 0;
        } else {
            const char character = url[pos];
            if (character >= '0' && character <= '9') {
                if (pos - labelStart < 3) {
                    octet = octet * 10 + (character - '0');
                }
            } else if ((character >= 'a' && character <= 'z') ||
                       (character >= 'A' && character <= 'Z') || character == '-') {
                numericHost = false;
            } else {
                return false;
            }
        }
    }
    return !numericHost || (validIpv4 && labelCount == 4);
}

void disconnectFtp()
{
    // Implementation for disconnecting the ftp connection
    if (ftpClient.connected()) {
        ftpClient.stop();
    } 
    ftpCommandCapture = "";
    ftpStatus = connectionStatus::DISCONNECTING;
    disconnect();
    digitalWrite(PIN_LED_RED, LED_OFF);
}

void phoneOnHookFtp()
{
    if (ftpClient.connected()) {
        ftpClient.stop();
    }
}

bool sendFtpCommand(const std::string& command)
{
    if (ftpStatus == connectionStatus::CONNECTING) {
        LOGFTP("FTP not connected, open a connection first host: %s : %d", ftpHost.c_str(), ftpPort);
        if (!ftpClient.connect(ftpHost.c_str(), ftpPort)) {
            LOGFTP("Connection failed");
            disconnectFtp();
            return false;
        } else {
            LOGFTP("Connection established");
            ftpStatus = connectionStatus::CONNECTED;
        }
    }

    if (ftpStatus != connectionStatus::CONNECTED) {
        LOGFTP("Wrong connection status: %d", static_cast<int>(ftpStatus));
        return false;
    }

    LOGFTP("Sending command: %s", command.c_str());
    ftpClient.print(command.c_str());
    return true;
}


bool openFtpConnection(std::string url)
{
    if (!isValidServerUrl(url)) {
        LOGFTP("Invalid server URL");
        return false;
    }
    LOGFTP("Opening localFTP connection with url: %s", url.c_str());
    customServerUrl = url;
    selectedServerIdx = -1;

    parseUrl(url, ftpHost, ftpPort, DEFAULT_FTP_PORT);
    digitalWrite(PIN_LED_RED, LED_ON);
    ftpCommandCapture = "";
    ftpStatus = connectionStatus::CONNECTING;
    return true;    
}

bool openFtpConnection()
{
    LOGFTP("Opening localFTP connection with default settings");
    digitalWrite(PIN_LED_RED, LED_ON);
    ftpCommandCapture = "";
    ftpStatus = connectionStatus::CONNECTING;
    return true;
}

void sendStringToP2000(const std::string& str, bool startHdr=true, bool closeHdr=true)
{
    if (startHdr) {
        handleSendByteAlways('T');
    }   
    for (char c : str) {
        handleSendByteAlways(c);
    }
    if (closeHdr) {
        handleSendByteAlways('\0');
    }
}


void sendServerInfoToP2000(int serverIdx, bool sndHdr)
{
    char buffer[128];

    if (serverIdx >= 0 && serverIdx < MAX_PHONEBOOK_ITEMS) {
        char stype = ServertypeToStr(phonebook[serverIdx].type)[0];

        snprintf(buffer, sizeof(buffer)-1, "%s%i%sNaam:%s%-16s (%c)\r\n", P2000T_CYAN, serverIdx+1, P2000T_GREEN,
                 P2000T_YELLOW, phonebook[serverIdx].name, ServertypeToStr(phonebook[serverIdx].type)[0]);
        sendStringToP2000(buffer, sndHdr, false);
        snprintf(buffer, sizeof(buffer)-1, "%s  %s\r\n", P2000T_BLUE, phonebook[serverIdx].url);
        sendStringToP2000(buffer, false, sndHdr);

    } else if (isValidServerUrl(customServerUrl)) {
        snprintf(buffer, sizeof(buffer)-1, "%sUrl:%s", P2000T_GREEN, customServerUrl, P2000T_YELLOW);
        sendStringToP2000(buffer, sndHdr, sndHdr);

    } else  {
        snprintf(buffer, sizeof(buffer)-1, "%sGeen server geselecteerd", P2000T_YELLOW);
        sendStringToP2000(buffer, sndHdr, sndHdr);
    }
}

void handleFtpServersCommand(const std::string& command)
{
    // Implementation for handling :#FSERVERS commands
    LOGFTP("Handling :#FSERVERS command: %s", command.c_str());

    sendStringToP2000(P2000T_CYAN, true, false);
    sendStringToP2000("Servers:\r\n", false, false);
    for (int i = 0; i < MAX_PHONEBOOK_ITEMS; ++i) {
        if (phonebook[i].url == nullptr || phonebook[i].url[0] == '\0') {
            continue;
        }
        LOGFTP("Server %d: %s", i, phonebook[i].name);
        sendStringToP2000(selectedServerIdx == i ? "*" : "", false, false); 
        sendServerInfoToP2000(i, false);
    }
    sendStringToP2000(P2000T_YELLOW, false, false);
    sendStringToP2000("Selecteer een server (:#FSERVER <num>)\r\n", false, true);
}

void handleFtpServerCommand(const std::string& command)
{
    char buffer[128];

    // Implementation for handling :#FSERVER commands
    LOGFTP("Handling :#FSERVER srg command: %s ", command.c_str());
    std::string argument = command.substr(7);
    if (!argument.empty() && argument.back() == '\r') {
       argument.pop_back();
    }
    if (argument.empty()) {
        // Handle :#FSERVER  command
        // No new server given, just show the currently selected server
        sendServerInfoToP2000(selectedServerIdx, true);

    } else if (std::all_of(argument.begin(), argument.end(), [](unsigned char character) { return std::isdigit(character); })) {
        // Handle :#FSERVER<num> command
        // Argument is a number, parse it as server index and select the corresponding server
        unsigned int serverNumber = 0;
        for (char digit : argument) {
            if (serverNumber <= MAX_PHONEBOOK_ITEMS) {
                serverNumber = serverNumber * 10 + (digit - '0');
            }
        }
        LOGFTP("Handling :#FSERVER xx arg: %s nr: %u", argument.c_str(), serverNumber);
        if (serverNumber > 0 && serverNumber <= MAX_PHONEBOOK_ITEMS) {
            int serverIdx = static_cast<int>(serverNumber) - 1;
            selectedServerIdx = serverIdx;
            customServerUrl = "";

            parseUrl(std::string(phonebook[serverIdx].url), ftpHost, ftpPort, DEFAULT_FTP_PORT);

            LOGFTP("Selected server %d: %s", selectedServerIdx, phonebook[selectedServerIdx].url);
            snprintf(buffer, sizeof(buffer), "%sUw keuze:%s%-16s (%c)", P2000T_GREEN,
                P2000T_YELLOW, phonebook[serverIdx].name, ServertypeToStr(phonebook[serverIdx].type)[0]);
            sendStringToP2000(buffer);
        } else {
            LOGFTP("Server index out of range in command: %s ", command.c_str());
            snprintf(buffer, sizeof(buffer)-1, "%sServer niet gevonden", P2000T_MAGENTA);
            sendStringToP2000(buffer);
        }
    } else if (argument[0] == '"') {
        // Handle :#FSERVER"<url>" command
        size_t start = 8;
        size_t end = command.find('"', start);
        
        if (end != std::string::npos) {
            std::string url = command.substr(start, end - start);
            LOGFTP("Handling :#FSERVER \"<url>\" url: %s", url.c_str());
            if (isValidServerUrl(url)) {
                customServerUrl = url;
                selectedServerIdx = -1;
                parseUrl(url, ftpHost, ftpPort, DEFAULT_FTP_PORT);

                sendStringToP2000(P2000T_GREEN  "Server:"  P2000T_YELLOW + std::string(customServerUrl));
            } else {
                snprintf(buffer, sizeof(buffer)-1, "%sOngeldige server URL", P2000T_MAGENTA);
                sendStringToP2000(buffer);
            }
        } else {
            snprintf(buffer, sizeof(buffer)-1, "%sOngeldig FTP argument", P2000T_MAGENTA);
            sendStringToP2000(buffer);
        }
    } else {
        LOGFTP("Invalid server index in command: %s ", command.c_str());
        snprintf(buffer, sizeof(buffer)-1, "%sOngeldig FTP commando", P2000T_MAGENTA);
        sendStringToP2000(buffer);
    }
}

void handleFtpByeCommand(const std::string& cmd)
{
    // Implementation for handling :#FBYE command
    LOGFTP("Handling :#FBYE command");

    // Tell remote server we are disconnecting by sending bye command
    sendFtpCommand("FBYE\r");

    char buffer[128];
    snprintf(buffer, sizeof(buffer)-1, "%sVerbinding verbroken", P2000T_YELLOW);
    sendStringToP2000(buffer);

    // Reset server selection
    customServerUrl = "";
    selectedServerIdx = -1;
    digitalWrite(PIN_LED_RED, LED_OFF);
}

void handleFtpHelpCommand(const std::string& cmd)
{
    // Implementation for handling :#HELP command
    LOGFTP("Handling :#FHELP command");

    // Tell remote server we are disconnecting by sending bye command
    if (customServerUrl != "" || selectedServerIdx >= 0) {
        sendFtpCommand("FHELP\r");
    } else {
        LOGFTP("No remote server, send :#FHELP command, send own help text instead");
        sendStringToP2000(P2000T_GREEN "FTP Commandos:\n\r"
                  P2000T_GREEN ":#FDIR  " P2000T_YELLOW "Toon beschikare disks/cass.\n\r"
                  P2000T_GREEN ":#FZOEK " P2000T_YELLOW "Geef disk/cass. inhoud\n\r"
                  P2000T_GREEN ":#FCD\"<naam>\"  " P2000T_YELLOW "Selecteer disk/cass.\n\r"
                  P2000T_GREEN ":#FLOAD\"<naam>\"" P2000T_YELLOW "Laad bestand van srv.\n\r"
                  P2000T_GREEN ":#FSAVE\"<naam>\"" P2000T_YELLOW "Sla bestand op server\n\r"
                  P2000T_GREEN ":#FSERVERS     " P2000T_YELLOW "Toon servers\n\r"
                  P2000T_GREEN ":#FSERVER\"<nr>\"" P2000T_YELLOW "Selecteer server\n\r"
                  P2000T_GREEN ":#FBYE         " P2000T_YELLOW "Stop verbinding\n\r"
                  P2000T_GREEN ":#FHELP        " P2000T_YELLOW "Toon deze help\n\r");
    }
}

void handleOtherFtpCommands(const std::string& cmd)
{
    LOGFTP("Send other command: %s", cmd.c_str());
    if (customServerUrl == "" && selectedServerIdx == -1) {
        LOGFTP("No custom server URL and no server selected");
        sendStringToP2000(P2000T_MAGENTA "Geen server geselecteerd");
        return; // Do not send the command if no server is selected
    }

    // Make sure the command ends with a carriage return before sending
    if (cmd.empty() || cmd.back() != '\r') {
        sendFtpCommand(cmd + '\r');
    } else {
        sendFtpCommand(cmd);
    }
}

void handleFtp(uint8_t data)
{
    // Implementation for handling incoming P2000ftp data
    switch (ftpStatus) {
        case connectionStatus::CONNECTING: 
            {
                LOGFTP("local byte: %d (%c)", data, data > 31 && data < 127 ? data : '*');
                // We are still in command phase
                if (ftpCommandCapture == "") {
                    qotationMarkFound = false;
                }

                if (((data == '\t' || data == ' ') && !qotationMarkFound) ||  data == '\n') {
                    return; // Skip leading whitespace if not inside quotes AND skip newline characters
                }
                
                if (data == '"') {
                    qotationMarkFound = !qotationMarkFound;
                }

                ftpCommandCapture += static_cast<char>(data);

                if (data == '\r') { // carriage return marks end of command
                    LOGFTP("Captured FTP command: %s", ftpCommandCapture.c_str());

                    // Clear RxInterCore buffer on new command (remove leftover bytes of unprocessed commands)
                    clearRxQueue(); 

                    if (ftpCommandCapture.rfind("FSERVERS", 0) == 0) {
                        // Handle :#FSERVERS command (internal)
                        handleFtpServersCommand(ftpCommandCapture);

                    } else if (ftpCommandCapture.rfind("FSERVER", 0) == 0) {
                        // Handle :#FSERVER command (internal)
                        handleFtpServerCommand(ftpCommandCapture);

                    } else if (ftpCommandCapture.rfind("FBYE", 0) == 0) {
                        // Handle :#FBYE command (internal & remote)
                        handleFtpByeCommand(ftpCommandCapture);

                    } else if (ftpCommandCapture.rfind("FHELP", 0) == 0) {
                        // Handle :#FHELP command (remote if no server then internal)
                        handleFtpHelpCommand(ftpCommandCapture);

                    } else  {
                        // All other commands just pass through to the FTP client
                        handleOtherFtpCommands(ftpCommandCapture);
                    }
                    
                    ftpCommandCapture = "";
                }
            } 
            break;

        case connectionStatus::CONNECTED:
            if (ftpClient.connected())  {
                LOGFTP("write byte: %d (%c)", data, data > 31 && data < 127 ? data : '*');
                // Write the byte directly to the server socket
                ftpClient.write(data);
            }   else {
                // Handle the case when the server is not connected
                LOGFTP("Connection lost to P2000 ftp server (handleFtp) (data: %d)", data);
                ftpStatus = connectionStatus::DISCONNECTING;
                
                digitalWrite(PIN_LED_RED, LED_OFF);
            }
            break;

        default:
            LOGFTP("unhandled byte: %d (%c) connection status: %d", data, data > 31 && data < 127 ? data : '*', static_cast<int>(ftpStatus));
            break;
    }
    
}

void pollFtp()
{
    // Implementation for polling the P2000ftp connection
    switch (ftpStatus) {

        case connectionStatus::IDLE:
            // No active connection
            break;
    
        case connectionStatus::CONNECTING:
            // Still in command phase, waiting for command completion
            break; 
    
        case connectionStatus::CONNECTED:
            // Connection established - get all bytes at once, the host will disconnect after each command
            if (ftpClient.connected()) {
                while (ftpClient.available()) {
                    int cnt = 0;
                    while (!isRxQueueFull() && ftpClient.available()) {
                        uint8_t internetByte = ftpClient.read();
                        handleSendByteAlways(internetByte);
                        cnt++;
                    }
                    if (cnt > 0) {
                        LOGFTP("Read byte(s) for P2000 (pollFtp) byte count: %d (queue is %sfull)", cnt, isRxQueueFull() ? "" : "not ");
                    }
                    delay(500); // Give some time for the RX queue to be processed and the FTP host to respond
                }
            } else {
                // Keep connection alive if there are still bytes in the RX queue
                if (isRxQueueEmpty()) {
                    LOGFTP("Lost connection to P2000 ftp server (pollFtp)");
                    ftpStatus = connectionStatus::DISCONNECTING;
                }
                digitalWrite(PIN_LED_RED, LED_OFF);
            }
            break;

        case connectionStatus::DISCONNECTING:
            LOGFTP("Closing connection (pollFtp)");
            disconnectFtp();
            digitalWrite(PIN_LED_RED, LED_OFF);
            ftpStatus = connectionStatus::IDLE;
            setConnectionType(connectionType::DISCONNECTED);
            break;
    }  
}
#endif
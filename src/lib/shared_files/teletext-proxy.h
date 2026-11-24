/**
 * Teletext header definitions and utilities for the Miniware M2009 Modem Cartridge / Viditel Modem Emulator
 * 
 * Author: Bart Eversdijk
 * 
 * https://teletekst-data.nos.nl/json/101
 * 
 */
#ifndef TELETEXT_PROXY_H
#define TELETEXT_PROXY_H

#include <string> 

struct TeletextPage {
    std::string number;
    std::string prevPage;
    std::string nextPage;
    std::string nextSubPage;
    bool hasSubPage;
};

#define PAGE_NOT_FOUND  "      Pagina niet gevonden!"
#define ERROR_PAGE_TITLE "         Melding server:"
#define CONNECTING_TITLE "      Verbinden met server:"

void teletextInfoPage(std::string title, std::string message1, std::string message2);
void disconnectTeletext(const char * reason);
void startTeletextConnection(int serverId);
bool openTeletextConnection(std::string url);
void handleTeletext(uint8_t data);
void pollTeletext();
void phoneOnHookTeletext();
void handleTeletextPageReload();
extern char * getCurrentTime();
extern void disconnect();
extern void handleSendByte(uint8_t data);
extern void setConnectionType(connectionType type);
extern connectionType getConnectionType();

#endif // TELETEXT_PROXY_H
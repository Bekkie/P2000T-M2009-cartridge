/**
 * Internal pages for the Miniware M2009 Modem Cartridge / Viditel Modem Emulator
 *
 * Author: Bart Eversdijk
 */
#include <string>

#define ENABLE_INTERNAL_PAGES_LOG 1
#include "config.h"

#include "wifi-portal.h"
#include "internal-pages.h"
#include "teletext-proxy.h"
#include "viewdata.h"

#if defined(M2009EMULATOR)
#include "..\..\m2009-emulator\download_pages.h"
#endif

extern void handleSendByte(uint8_t data);

int currentPage = 0;
int phonebookIndex = -1;
int phonebookPage = 0;
LineEditEntry localEditLine;
PhoneNumberToSite editPBItem;
bool m_statusPageActive = false;

#if defined(M2009EMULATOR)                            
int currentDownloadPage = 0;
#endif                            

#define NUM_OF_EDIT_INDEX_LINES 4
#define LAST_EDIT_INDEX_LINE (NUM_OF_EDIT_INDEX_LINES - 1)

UARTConfigBlock editCfgItem;

std::string keyboardLocalBuffer = "";


//==================================================================================================
//  LOCAL MENU DEFINITION
//===================================================================================================
#if defined(VIEWDATAEMULATOR)
static const std::string PAGE_MAINMENU = "\x0C"
    CTRL_YELLOW_TXT PAGE_HEADER  CTRL_CR_LF 
    CTRL_GREEN_GRPH + std::string(38, '\x2c') + CTRL_CR_LF 
    CTRL_GREEN_GRPH "\x7F\x7F\x7F\x7F\x7F\x7F\x7F\x7F\x7F\x7F\x7F\x7F\x7F\x7F\x7F\x7F\x7F\x7F\x7F\x7F\x7F\x7F\x7F\x7F\x7F\x7F\x7F\x7F\x7F\x7F\x7F\x7F\x7F\x7F\x7F\x7F\x7F\x7F" CTRL_CR_LF 
    CTRL_GREEN_GRPH "\x7F\x7F\x7F\x7F\x7F\x7F\x7F\x7F\x7F\x7F\x7F\x35\x20\x7F\x7F\x7F\x7F\x20\x6A\x35\x20\x7F\x20\x6A\x7F\x7F\x7F\x7F\x7F\x7F\x7F\x7F\x7F\x7F\x7F\x7F\x7F\x7F" CTRL_CR_LF 
    CTRL_GREEN_GRPH "\x7F\x7F\x7F\x7F\x7F\x7F\x23\x6B\x7F\x23\x6B\x37\x23\x7F\x3F\x23\x2B\x20\x6A\x37\x23\x7F\x20\x20\x20\x7F\x27\x23\x23\x6F\x7F\x20\x6A\x7F\x7F\x7F\x7F\x7F" CTRL_CR_LF 
    CTRL_RED_GRPH   "\x7F\x7F\x7F\x7F\x7F\x7F\x20\x6A\x7F\x20\x6A\x35\x20\x7F\x20\x68\x7C\x20\x6A\x35\x20\x7F\x20\x6A\x7F\x35\x20\x7C\x34\x20\x7F\x20\x6A\x7F\x7F\x7F\x7F\x7F" CTRL_CR_LF 
    CTRL_WHITE_GRPH "\x7F\x7F\x7F\x7F\x7F\x7F\x20\x6A\x7F\x20\x6A\x35\x20\x7F\x20\x6A\x7F\x20\x6A\x35\x20\x7F\x20\x6A\x7F\x35\x20\x23\x21\x20\x7F\x20\x6A\x7F\x7F\x7F\x7F\x7F" CTRL_CR_LF 
    CTRL_CYAN_GRPH  "\x7F\x7F\x7F\x7F\x7F\x7F\x30\x20\x21\x20\x7A\x35\x20\x7F\x20\x2A\x2F\x20\x6A\x35\x20\x7F\x20\x2A\x2F\x35\x20\x2F\x27\x23\x7F\x20\x6A\x7F\x7F\x7F\x7F\x7F" CTRL_CR_LF 
    CTRL_GREEN_GRPH "\x7F\x7F\x7F\x7F\x7F\x7F\x7F\x74\x70\x7E\x7F\x75\x70\x7F\x7D\x70\x78\x70\x7A\x75\x70\x7F\x7D\x70\x78\x7F\x74\x70\x70\x7E\x7F\x70\x7A\x7F\x7F\x7F\x7F\x7F" CTRL_CR_LF 
    CTRL_GREEN_GRPH "\x7F\x7F\x7F\x7F\x7F\x7F\x7F\x7F\x7F\x7F\x7F\x7F\x7F\x7F\x7F\x7F\x7F\x7F\x7F\x7F\x7F\x7F\x7F\x7F\x7F\x7F\x7F\x7F\x7F\x7F\x7F\x7F\x7F\x7F\x7F\x7F\x7F\x7F" CTRL_CR_LF 
    CTRL_GREEN_GRPH + std::string(38, '\x2c') + CTRL_CR_LF 
    CTRL_CR_LF 
    CTRL_YELLOW_TXT + "    Maak een keuze uit het menu:" + CTRL_CR_LF
    CTRL_CR_LF 
    CTRL_YELLOW_TXT + " 1 " + CTRL_WHITE_TXT + "- Bel nummer uit telefoonboek" +  CTRL_CR_LF
    CTRL_YELLOW_TXT + " 2 " + CTRL_WHITE_TXT + "- Telefoonboek aanpassen" +  CTRL_CR_LF
    CTRL_CR_LF
    CTRL_YELLOW_TXT + " 3 " + CTRL_WHITE_TXT + "- Toon HELP & Kleurentest" +  CTRL_CR_LF
    CTRL_YELLOW_TXT + " 4 " + CTRL_WHITE_TXT + "- Toon huidige status" +  CTRL_CR_LF
    CTRL_CR_LF
    CTRL_YELLOW_TXT + " 9 " + CTRL_WHITE_TXT + "- Verbinding verbreken" +  CTRL_CR_LF
    CTRL_CR_LF
    CTRL_GREEN_GRPH + std::string(38, '\x2c') +  CTRL_CR_LF
    CTRL_CYAN_TXT + "Geef uw keuze aub... " + CTRL_WHITE_TXT;
#endif

#if defined(M2009EMULATOR)
static const std::string PAGE_MAINMENU = "\x0C"
    CTRL_YELLOW_TXT PAGE_HEADER  CTRL_CR_LF
    CTRL_BLUE_GRPH + std::string(37, '\x2c') +  CTRL_CR_LF
    CTRL_CYAN_TXT CTRL_BACKG_COLOR CTRL_BLUE_GRPH "\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20\x20" CTRL_BACKG_BLACK CTRL_CR_LF
    CTRL_CYAN_TXT CTRL_BACKG_COLOR CTRL_BLUE_GRPH "\x6A\x6A\x6A\x6A\x6A\x68\x20\x68\x6A\x6A\x6A\x68\x20\x68\x6A\x6A\x6A\x6A\x68\x20\x68\x6A\x6A\x6A\x6A\x68\x20\x68\x6A\x6A\x6A\x6A\x68\x20\x20" CTRL_BACKG_BLACK CTRL_CR_LF
    CTRL_CYAN_TXT CTRL_BACKG_COLOR CTRL_BLUE_GRPH "\x6A\x6A\x20\x20\x6A\x6A\x20\x22\x22\x20\x6A\x6A\x20\x6A\x6A\x20\x20\x6A\x6A\x20\x6A\x6A\x20\x20\x6A\x6A\x20\x6A\x6A\x20\x20\x6A\x6A\x20\x20" CTRL_BACKG_BLACK CTRL_CR_LF
    CTRL_CYAN_TXT CTRL_BACKG_COLOR CTRL_BLUE_GRPH "\x6A\x6A\x6A\x6A\x6A\x2A\x20\x20\x60\x6A\x6A\x22\x20\x6A\x6A\x20\x20\x6A\x6A\x20\x6A\x6A\x20\x20\x6A\x6A\x20\x6A\x6A\x20\x20\x6A\x6A\x20\x20" CTRL_BACKG_BLACK CTRL_CR_LF
    CTRL_CYAN_TXT CTRL_BACKG_COLOR CTRL_BLUE_GRPH "\x6A\x6A\x20\x20\x20\x20\x20\x60\x6A\x22\x20\x20\x20\x6A\x6A\x20\x20\x6A\x6A\x20\x6A\x6A\x20\x20\x6A\x6A\x20\x6A\x6A\x20\x20\x6A\x6A\x20\x20" CTRL_BACKG_BLACK CTRL_CR_LF
    CTRL_CYAN_TXT CTRL_BACKG_COLOR CTRL_BLUE_GRPH "\x6A\x6A\x20\x20\x20\x20\x20\x6A\x6A\x6A\x6A\x6A\x20\x2A\x6A\x6A\x6A\x6A\x2A\x20\x2A\x6A\x6A\x6A\x6A\x2A\x20\x2A\x6A\x6A\x6A\x6A\x2A\x20\x20" CTRL_BACKG_BLACK CTRL_CR_LF
    CTRL_CYAN_TXT CTRL_BACKG_COLOR CTRL_BLUE_TXT  CTRL_DOUBLE_SIZE + " M2009 Miniware Modem Emulator    " + CTRL_BACKG_BLACK + CTRL_CR_LF 
    CTRL_CYAN_TXT CTRL_BACKG_COLOR "\x20\x20\x20\x20"
    CTRL_CR_LF 
    CTRL_BLUE_GRPH + std::string(37, '\x2c') +  CTRL_CR_LF
    CTRL_CR_LF 
    CTRL_YELLOW_TXT + "    Maak een keuze uit het menu:" + CTRL_CR_LF
    CTRL_CR_LF 
    CTRL_YELLOW_TXT + " 1 " + CTRL_WHITE_TXT + "- Bel nummer uit telefoonboek" +  CTRL_CR_LF
    CTRL_YELLOW_TXT + " 2 " + CTRL_WHITE_TXT + "- Telefoonboek aanpassen" +  CTRL_CR_LF
    CTRL_CR_LF
    CTRL_YELLOW_TXT + " 3 " + CTRL_WHITE_TXT + "- Toon HELP & Kleurentest" +  CTRL_CR_LF
    CTRL_YELLOW_TXT + " 4 " + CTRL_WHITE_TXT + "- Toon huidige status" +  CTRL_CR_LF
    CTRL_YELLOW_TXT + " 5 " + CTRL_WHITE_TXT + "- FTP client downloaden" +  CTRL_CR_LF
    CTRL_CR_LF
    CTRL_YELLOW_TXT + " 9 " + CTRL_WHITE_TXT + "- Verbinding verbreken" +  CTRL_CR_LF
    CTRL_BLUE_GRPH + std::string(37, '\x2c') +  CTRL_CR_LF
    CTRL_GREEN_TXT + "Geef uw keuze aub... " + CTRL_WHITE_TXT;
#endif

static const std::string PAGE_MAINMENU3 = "\x0C"
    CTRL_YELLOW_TXT PAGE_HEADER  CTRL_CR_LF
    CTRL_GREEN_GRPH + std::string(38, '\x2c') +  CTRL_CR_LF
    CTRL_CR_LF
    CTRL_DOUBLE_SIZE + "Welkom op de Viditel modem emulator!" + CTRL_CR_LF 
    CTRL_CR_LF
    CTRL_CR_LF 
    CTRL_YELLOW_TXT + "    Maak een keuze uit het menu:" + CTRL_CR_LF
    CTRL_CR_LF 
    CTRL_YELLOW_TXT + " 1 " + CTRL_WHITE_TXT + "- Bel nummer uit telefoonboek" +  CTRL_CR_LF
    CTRL_YELLOW_TXT + " 2 " + CTRL_WHITE_TXT + "- Telefoonboek aanpassen" +  CTRL_CR_LF
    CTRL_CR_LF
    CTRL_YELLOW_TXT + " 3 " + CTRL_WHITE_TXT + "- Toon HELP & Kleurentest" +  CTRL_CR_LF
    CTRL_CR_LF
    CTRL_CR_LF 
    CTRL_CR_LF 
    CTRL_CR_LF 
    CTRL_CR_LF 
    CTRL_CR_LF
    CTRL_YELLOW_TXT + " 9 " + CTRL_WHITE_TXT + "- Verbinding verbreken" +  CTRL_CR_LF
    CTRL_CR_LF
    CTRL_CR_LF
    CTRL_CR_LF
    CTRL_GREEN_GRPH + std::string(38, '\x2c') +  CTRL_CR_LF
    CTRL_CYAN_TXT + "Geef uw keuze aub... " + CTRL_WHITE_TXT;


static std::string charRange(int from, int to)
{
    std::string s;
    for (int c = from; c <= to; c++)
        s += static_cast<char>(c);
    return s;
}

static const std::string PAGE_COLOR_AND_HELP = "\x0C"
   CTRL_YELLOW_TXT   "         HELP & KLEURENTEST      "  CTRL_CR_LF
   CTRL_BLUE_GRPH + std::string(38, '\x2c') + CTRL_CR_LF
   CTRL_YELLOW_TXT + " Ophangen " + CTRL_WHITE_TXT + "*90\x5f      " + CTRL_YELLOW_TXT + "Hoofdmenu" + CTRL_WHITE_TXT + "*0\x5f " +  CTRL_CR_LF
   CTRL_YELLOW_TXT + " Edit save" + CTRL_WHITE_TXT + "ESC-J     "    + CTRL_YELLOW_TXT + "Cancel   " + CTRL_WHITE_TXT + "ESC-K " +  CTRL_CR_LF
   CTRL_YELLOW_TXT + " Info     " + CTRL_WHITE_TXT + "ESC-ESC-? " + CTRL_YELLOW_TXT + "(CODE=ESC)" +  CTRL_CR_LF
   CTRL_YELLOW_TXT + " Volgende " + CTRL_WHITE_TXT + "n > p \x5D \x5F " + CTRL_YELLOW_TXT + "Vorige   " + CTRL_WHITE_TXT + "p < \x5E" +  CTRL_CR_LF
   CTRL_BLUE_GRPH + std::string(38, '\x2c') + CTRL_CR_LF
   CTRL_RED_TXT     + " Rood    (" + CTRL_WHITE_TXT + "ESC-A" + CTRL_RED_TXT     + ")   " + CTRL_RED_GRPH     + "Grafisch ESC-Q" +  CTRL_CR_LF
   CTRL_GREEN_TXT   + " Groen   (" + CTRL_WHITE_TXT + "ESC-B" + CTRL_GREEN_TXT   + ")   " + CTRL_GREEN_GRPH   + "Grafisch ESC-R" +  CTRL_CR_LF
   CTRL_YELLOW_TXT  + " Geel    (" + CTRL_WHITE_TXT + "ESC-C" + CTRL_WHITE_TXT   + ")   " + CTRL_YELLOW_GRPH  + "Grafisch ESC-S" +  CTRL_CR_LF
   CTRL_BLUE_TXT    + " Blauw   (" + CTRL_WHITE_TXT + "ESC-D" + CTRL_BLUE_TXT    + ")   " + CTRL_BLUE_GRPH    + "Grafisch ESC-T" +  CTRL_CR_LF
   CTRL_MAGENTA_TXT + " Magenta (" + CTRL_WHITE_TXT + "ESC-E" + CTRL_MAGENTA_TXT + ")   " + CTRL_MAGENTA_GRPH + "Grafisch ESC-U" +  CTRL_CR_LF
   CTRL_CYAN_TXT    + " Cyaan   (" + CTRL_WHITE_TXT + "ESC-F" + CTRL_CYAN_TXT    + ")   " + CTRL_CYAN_GRPH    + "Grafisch ESC-V" +  CTRL_CR_LF
   CTRL_WHITE_TXT   + " Wit     (" + CTRL_WHITE_TXT + "ESC-G" + CTRL_WHITE_TXT   + ")   " + CTRL_WHITE_GRPH   + "Grafisch ESC-W" +  CTRL_CR_LF
   CTRL_FLASH_ON    + " Knipper ( ESC-H )   " + CTRL_FLASH_OFF      + "Stil     ESC-I" +  CTRL_CR_LF
   CTRL_DOUBLE_SIZE + " Hoog    ( ESC-M )   " + CTRL_NORMAL_SIZE    + "Laag     ESC-L" +  CTRL_CR_LF
   CTRL_CR_LF
   CTRL_CYAN_TXT + CTRL_BACKG_COLOR  + CTRL_MAGENTA_TXT  + "kleur  ( ESC-] )  " + CTRL_BACKG_BLACK   + " Zwart    ESC-/" +  CTRL_CR_LF
   CTRL_GREEN_GRPH + CTRL_SMALL_GRPH + "\x7f\x7fpp\x2c\x2c/// ESC-Z     " + CTRL_NORMAL_GRPH + "\x7f\x7fpp\x2c\x2c// ESC-Y" + CTRL_CR_LF
   " " + charRange(33, 64) + CTRL_CR_LF
   " " + charRange(65, 102) + CTRL_CR_LF
   " " + charRange(103, 127) + CTRL_CR_LF
   CTRL_BLUE_GRPH + std::string(38, '\x2c') + CTRL_CR_LF
   CTRL_YELLOW_TXT  + "Geef 0 voor het hoofdmenu.";


//==================================================================================================
//  LOCAL MENU HANDLING
//===================================================================================================

void handleSendLocalPage(std::string page)
{
    for (char c : page) {
        handleSendByte(static_cast<uint8_t>(c));
    }   
}


const char * SERVER_TYPE_STRING[] = {
    VIEWDATA_TXT,
    TELETEXT_TXT,
    BBS_TXT,
    OTHER_TXT
#if defined(M2009EMULATOR)
    , P2000FTP_TXT
    , INTERNAL_TXT
#endif

};

const char * ServertypeToStr(ServerType type) {
    if (type >= NUM_SERVER_TYPES) {
        return OTHER_TXT;
    }
    return SERVER_TYPE_STRING[(int)type];
}

ServerType StrToServertype(const std::string& str)
{
    for (int i = 0; i < NUM_SERVER_TYPES; i++) {
        if (SERVER_TYPE_STRING[i][0] == str[0]) {
            return static_cast<ServerType>(i);
        }
    }
    return ServerType::OTHER;
}

const char * BaudToStr(unsigned long baud) {
    for (size_t i = 0; i < NUM_BAUD_RATES; i++) {
        if (baudRates[i] == baud) {
            return baudRatesTxt[i];
        }
    }
    return "Unknown";
}

unsigned long StrToBaud(const std::string& str) {
    for (size_t i = 0; i < NUM_BAUD_RATES; i++) {
        if (baudRatesTxt[i] == str) {
            return baudRates[i];
        }
    }
    return 0;
}


void setCursorPosition(uint8_t xpos, uint8_t ypos, uint8_t visible)
{
    LOGIP("Set cursor position to (%d, %d), visible: %d", xpos, ypos, visible);
    // Viewdata has no absolute addressing: home (0x1E), then step down (0x0A) and right (0x09)
    handleSendByte(0x1E);
    for (uint8_t r = 0; r < (ypos-1); r++) {
        handleSendByte(0x0A);
    }        
    for (uint8_t c = 0; c < (xpos-1); c++) {
        handleSendByte(0x09);
    }    
    if (visible != CURSOR_KEEP_AS_IS) {
        handleSendByte(visible > 0 ? CHAR_SHOW_CURSOR : CHAR_HIDE_CURSOR);
    }    
}


void sendStatusPage()
{
    static const std::string PAGE_STATUS_HDR = "\x0C"
    CTRL_YELLOW_TXT PAGE_HEADER CTRL_CR_LF
    CTRL_BLUE_GRPH + std::string(38, '\x2c') + CTRL_CR_LF;

    static const std::string PAGE_STATUS_FOOT = 
    CTRL_BLUE_GRPH + std::string(38, '\x2c')  + CTRL_CR_LF;
    
    handleSendLocalPage(PAGE_STATUS_HDR);
    char buffer[MAX_URL_LEN+6];
    bool connected = WiFi.status() == WL_CONNECTED;
    
    handleSendLocalPage("\x1B\x43Verbonden met:\x1B\x47");
    if ((getConnectionType() == connectionType::VIEWDATA) || getConnectionType() == connectionType::TELETEXT) {
                handleSendLocalPage(phonebook[phonebookIndex].name);
                handleSendLocalPage("\n\r "); 
                snprintf(buffer, sizeof(buffer), "%-54s", phonebook[phonebookIndex].url);
                handleSendLocalPage(buffer);
    } else if (getConnectionType() == connectionType::LOCALMENU) {
            handleSendLocalPage("intern menu\r\n\r\n");
    } else {
            handleSendLocalPage("-\r\n\r\n"); 
    }
    handleSendLocalPage(CTRL_CR_LF);
    
    handleSendLocalPage(CTRL_BLUE_GRPH + std::string(38, '\x2c')  + CTRL_CR_LF);

#if defined(VIEWDATAEMULATOR)
    snprintf(buffer, sizeof(buffer), "%sBaudrate:      -\r\n", CTRL_YELLOW_TXT);
    snprintf(buffer, sizeof(buffer), "%sBaudrate:     %s%s\r\n", CTRL_YELLOW_TXT, CTRL_WHITE_TXT, BaudToStr(uartConfig.baudrateUp));
    handleSendLocalPage(buffer);
    snprintf(buffer, sizeof(buffer), "%sProtocol:     %s%d data, parity %c %d stop\r\n", CTRL_YELLOW_TXT, CTRL_WHITE_TXT, uartConfig.dataBits8 ? 8 : 7, uartConfig.parity, uartConfig.stopBits1 ? 1 : 2);
    handleSendLocalPage(buffer);
#else
    snprintf(buffer, sizeof(buffer), "%sSim Baudrate:  -\r\n", CTRL_YELLOW_TXT);
    snprintf(buffer, sizeof(buffer), "%sSim Baudrate: %s%s\r\n", CTRL_YELLOW_TXT, CTRL_WHITE_TXT, BaudToStr(uartSimConfig.baudrateSim));
    handleSendLocalPage(buffer);     
    snprintf(buffer, sizeof(buffer), "%sLuisterpoort: %s%s\r\n", CTRL_YELLOW_TXT, CTRL_WHITE_TXT, uartSimConfig.port > 0 ? String(uartSimConfig.port).c_str() : "Uitgeschakeld");
    handleSendLocalPage(buffer);
#endif
    handleSendLocalPage(CTRL_CR_LF);

    handleSendLocalPage(CTRL_BLUE_GRPH + std::string(38, '\x2c')  + CTRL_CR_LF);
    snprintf(buffer, sizeof(buffer), "%sNetwerk:      %s%-23s\r\n", CTRL_YELLOW_TXT, CTRL_WHITE_TXT, connected ?  WiFi.SSID().c_str() : "Niet verbonden");
    handleSendLocalPage(buffer);
    snprintf(buffer, sizeof(buffer), "%sMode:         %s%-10s\r\n", CTRL_YELLOW_TXT, CTRL_WHITE_TXT, connected ?  (useStaticIPaddress ? "Static IP" : "DHCP") : "Niet verbonden");
    handleSendLocalPage(buffer);
    snprintf(buffer, sizeof(buffer), "%sIP adres:     %s%-20s\r\n", CTRL_YELLOW_TXT, CTRL_WHITE_TXT, connected ?  WiFi.localIP().toString().c_str() : "n/a");
    handleSendLocalPage(buffer);
    snprintf(buffer, sizeof(buffer), "%sGateway:      %s%-20s\r\n", CTRL_YELLOW_TXT, CTRL_WHITE_TXT, connected ?  WiFi.gatewayIP().toString().c_str() : "n/a");
    handleSendLocalPage(buffer);
    snprintf(buffer, sizeof(buffer), "%sDNS:          %s%-20s\r\n", CTRL_YELLOW_TXT, CTRL_WHITE_TXT, connected ?  WiFi.dnsIP().toString().c_str() : "n/a");
    handleSendLocalPage(buffer);
    snprintf(buffer, sizeof(buffer), "%sMAC-adres:    %s%-20s\r\n", CTRL_YELLOW_TXT, CTRL_WHITE_TXT, WiFi.macAddress().c_str());
    handleSendLocalPage(buffer);
    snprintf(buffer, sizeof(buffer), "%sWifi signaal: %s%d dBm\r\n", CTRL_YELLOW_TXT, CTRL_WHITE_TXT, connected ?  WiFi.RSSI() : 0);
    handleSendLocalPage(buffer);

    handleSendLocalPage(CTRL_CR_LF);
    snprintf(buffer, sizeof(buffer), "%sDatum / Tijd: %s%-20s\r\n", CTRL_YELLOW_TXT, CTRL_WHITE_TXT, getCurrentTime());
    handleSendLocalPage(buffer);

    handleSendLocalPage("\r\n\r\n"); 
    // Copyright information + version
    snprintf(buffer, sizeof(buffer), " %sSoftware V%s %s\r\n", CTRL_MAGENTA_TXT, version.c_str(), author.c_str());
    handleSendLocalPage(buffer);
    handleSendLocalPage(PAGE_STATUS_FOOT); 

    if (m_statusPageActive) {
        handleSendLocalPage(CTRL_CYAN_TXT "Terug met 0 of 1 verbinding verbreken");
    } else {
        handleSendLocalPage(CTRL_CYAN_TXT "Geef 0 voor menu");
    }
        

}

void sendStatusPage(bool duringDialoutConnection)
{
    currentPage = 4;
    m_statusPageActive = duringDialoutConnection;
    sendStatusPage();
}

void sendPhoneBookPage(bool editMode, uint8_t pagenr) 
{
    #define ITEMS_PER_PAGE 10

    std::string CTRLH = (editMode ? CTRL_BLUE_GRPH : CTRL_GREEN_GRPH);

    std::string PAGE_PHONEBOOK_HDR = "\x0C"
        CTRL_YELLOW_TXT PAGE_HEADER CTRL_CR_LF + CTRLH + std::string(38, '\x2c') + CTRL_CR_LF;

    uint8_t maxPages = (MAX_PHONEBOOK_ITEMS / ITEMS_PER_PAGE) - 1;
    if (((MAX_PHONEBOOK_ITEMS / ITEMS_PER_PAGE) - 1) < 0) pagenr = maxPages;
    if (pagenr > maxPages) pagenr = 0;
    phonebookPage = pagenr; 

    uint8_t startIndex = ITEMS_PER_PAGE * pagenr;
    if (startIndex >= MAX_PHONEBOOK_ITEMS) {
        startIndex = MAX_PHONEBOOK_ITEMS - ITEMS_PER_PAGE;
    }
    uint8_t endIndex = startIndex + ITEMS_PER_PAGE;
    if (endIndex > MAX_PHONEBOOK_ITEMS) {
        endIndex = MAX_PHONEBOOK_ITEMS;
    }

    handleSendLocalPage(PAGE_PHONEBOOK_HDR);
    char buffer[MAX_URL_LEN+6];
    for (int i = startIndex; i < endIndex; ++i) {
        if (phonebook[i].name[0] != '\0'){ 
            snprintf(buffer, sizeof(buffer)-1, "\x1B\x43%c\x1B\x47%-20s %-12s\x1B\x45%c\r\n", (i < 9 ? i+'1' : 'A'+i-9), phonebook[i].name,   
                                                    phonebook[i].number, ServertypeToStr(phonebook[i].type)[0]);
            handleSendLocalPage(buffer); 
            snprintf(buffer, sizeof(buffer)-1, "%s  %s", (editMode ? CTRL_GREEN_TXT : CTRL_BLUE_TXT),   phonebook[i].url);
            handleSendLocalPage(buffer);
            handleSendLocalPage(CTRL_CR_LF);
            
        } else {
            snprintf(buffer, sizeof(buffer)-1, "\x1B\x43%c\x1B\x47%-20s\r\n", (i < 9 ? i+'1' : 'A'+i-9), "<leeg>");
            handleSendLocalPage(buffer);
            handleSendLocalPage(CTRL_CR_LF);
        }
    }

    // Send footer
    handleSendLocalPage(CTRLH + "\x2c\x2c\x2c" +
        + CTRL_YELLOW_TXT "<" + CTRLH + std::string(12, '\x2c')
        + CTRL_YELLOW_TXT "0" + CTRLH + std::string(11, '\x2c')
        + CTRL_YELLOW_TXT ">" + CTRLH + "\x2c\x2c\x2c" + CTRL_CR_LF); 

    handleSendLocalPage(CTRL_CYAN_TXT);
    if (editMode) {
        handleSendLocalPage(CTRL_CYAN_TXT "Geef 1 t/m U om gegevens te wijzigen");
    } else {
        handleSendLocalPage("Bel met nummer 1..U" CTRL_MAGENTA_TXT "(Viewd/Teltxt/BBS)");
    }
}

void sendPhoneBookItem(int i) 
{
    static const std::string PAGE_PHONEBOOK_HDR = "\x0C"
    CTRL_YELLOW_TXT PAGE_HEADER CTRL_CR_LF
    CTRL_BLUE_GRPH + std::string(38, '\x2c') + CTRL_CR_LF
    CTRL_CR_LF + 
    CTRL_DOUBLE_SIZE + " Gegevens wijzigen" +  CTRL_CR_LF  +
    CTRL_CR_LF +
    CTRL_CR_LF +
    CTRL_CR_LF;
    
    static const std::string PAGE_PHONEBOOK_FOOT = CTRL_CR_LF 
    CTRL_CR_LF 
    CTRL_CR_LF 
    CTRL_CR_LF 
    CTRL_CR_LF 
    CTRL_CR_LF 
    CTRL_CR_LF
    CTRL_CR_LF 
    CTRL_CR_LF 
    CTRL_BLUE_GRPH + std::string(38, '\x2c') + CTRL_CR_LF
    CTRL_CYAN_TXT + "Opslaan met" + CTRL_WHITE_TXT + "ESC-J"    + CTRL_CYAN_TXT + "Afbreken met" + CTRL_WHITE_TXT + "ESC-K";

    handleSendLocalPage(PAGE_PHONEBOOK_HDR);

    std::string name = std::string(phonebook[i].name);
    std::string number = std::string(phonebook[i].number);
    std::string url = std::string(phonebook[i].url);

    handleSendLocalPage("\x1B\x43Naam:  \x1B\x47" + 
            name.substr(0, MAX_NAME_LEN) + 
            std::string((name.length() < MAX_NAME_LEN) ?  (MAX_NAME_LEN - name.length()) : 0, '.') + 
            "\r\n");
    handleSendLocalPage("\x1B\x43Nummer:\x1B\x47" +
            number.substr(0, MAX_NUMBER_LEN) + 
            std::string((number.length() < MAX_NUMBER_LEN) ?  (MAX_NUMBER_LEN - number.length()) : 0, '.') + 
            "\r\n");
    handleSendLocalPage("\x1B\x43URL:   \x1B\x47" +
            url.substr(0, MAX_URL_LEN) + 
            std::string((url.length() < MAX_URL_LEN) ?  (MAX_URL_LEN - url.length()) : 0, '.') + 
            "\r\n");
    handleSendLocalPage("\x1B\x43Soort: \x1B\x47" +  std::string(ServertypeToStr(phonebook[i].type)) + "\r\n");
    
    handleSendLocalPage(PAGE_PHONEBOOK_FOOT);
    setCursorPosition(localEditLine.startXpos + strlen(localEditLine.content), localEditLine.startYpos, CURSOR_VISIBLE);
}

#if defined(M2009EMULATOR)
void fillEditConfigLine(uint8_t index)
{
    // Implement the logic to fill the local edit line with the content of the given page
    LOGIP("Filling edit line for index %d", index);
    localEditLine.index = index;
    switch (index) {
        case 0:
            char dumStr[MAX_NAME_LEN];
            snprintf(dumStr, sizeof(dumStr), "%s", BaudToStr(editCfgItem.baudrateSim));
            strlcpy(localEditLine.content, dumStr, sizeof(localEditLine.content) - 1);
            localEditLine.type = FIELDTYPE_BAUDRATE;
            localEditLine.maxlength = strlen(localEditLine.content);
            localEditLine.cursorPos = 0;
            localEditLine.startXpos = 10;
            localEditLine.startYpos = 12;
            break;
        default:
            return;
    }
}
#endif

void fillEditPhoneBookLine(uint8_t index)
{
    // Implement the logic to fill the local edit line with the content of the given page
    LOGIP("Filling edit line for index %d", index);
    localEditLine.index = index;
    switch (index) {
        case 0:
            strlcpy(localEditLine.content, editPBItem.name, sizeof(editPBItem.name));
            localEditLine.type = FIELDTYPE_STR;
            localEditLine.maxlength = MAX_NAME_LEN;
            localEditLine.cursorPos = strlen(localEditLine.content);
            localEditLine.startXpos = 10;
            localEditLine.startYpos = 8;
            break;
        case 1:
            strlcpy(localEditLine.content, editPBItem.number, sizeof(editPBItem.number));
            localEditLine.type = FIELDTYPE_NUMBER;
            localEditLine.maxlength = MAX_NUMBER_LEN;
            localEditLine.cursorPos = strlen(localEditLine.content);
            localEditLine.startXpos = 10;
            localEditLine.startYpos = 9;
            break;
        case 2:
            strlcpy(localEditLine.content, editPBItem.url, sizeof(editPBItem.url));
            localEditLine.type = FIELDTYPE_URL;
            localEditLine.maxlength = MAX_URL_LEN;
            localEditLine.cursorPos = strlen(localEditLine.content);
            localEditLine.startXpos = 10;
            localEditLine.startYpos = 10;
            break;
        case 3:
            char dumStr[MAX_NAME_LEN];
            snprintf(dumStr, sizeof(dumStr), "%s", ServertypeToStr(editPBItem.type));
            strlcpy(localEditLine.content, dumStr, sizeof(localEditLine.content) - 1);
            localEditLine.type = FIELDTYPE_SERVERTYPE;
            localEditLine.maxlength = strlen(localEditLine.content);
            localEditLine.cursorPos = 0;
            localEditLine.startXpos = 10;
            localEditLine.startYpos = 12;
            break;
        default:
            return;
    }
}

void storeEditLine(uint8_t index)
{
    // Implement the logic to fill the local edit line with the content of the given page
    LOGIP("Storing edit line for index %d", index);
    switch (index) {
        case 0:
            strlcpy(editPBItem.name, localEditLine.content, sizeof(editPBItem.name));
            break;
        case 1:
            strlcpy(editPBItem.number, localEditLine.content, sizeof(editPBItem.number));
            break;
        case 2:
            strlcpy(editPBItem.url, localEditLine.content, sizeof(editPBItem.url));
            break;
        case 3:
            editPBItem.type = StrToServertype(localEditLine.content);
            LOGIP("Server type for index %d: %s", index, localEditLine.content);
            break;
    }
}

void handleLocalMenus(uint8_t data)
{
    // Capture *00 for page reload
    if (data == '0' || data == '*' || data == CHAR_BACKSPACE) {
        switch (data)  {
            case CHAR_BACKSPACE: 
                if (!keyboardLocalBuffer.empty()) {
                    keyboardLocalBuffer.pop_back();
                    handleSendByte(CHAR_BACKSPACE);
                    return;
                }
                break;
            case '*':
                if (keyboardLocalBuffer.length() == 0) {
                    keyboardLocalBuffer = "*";
                    handleSendByte(data);
                    return;
                }
                break;
            case '0':
                if (keyboardLocalBuffer == "*") {
                    keyboardLocalBuffer += "0";
                    handleSendByte(data);
                    return;
                }
                if (keyboardLocalBuffer == "*0") {
                    keyboardLocalBuffer = "";
                    // Reload the page
                    LOGIP("*00 reload page");
                    switch (currentPage)   {
                        case 1:
                            sendPhoneBookPage(false, phonebookPage);
                            break;
                        case 2:
                            sendPhoneBookPage(true, phonebookPage);
                            break;
                        case 3:
                            handleSendLocalPage(PAGE_COLOR_AND_HELP);
                            break;
                        case 4:
                            sendStatusPage();
                            break;
#if defined(M2009EMULATOR)                            
                        case 5:
                            handleSendLocalPage(*DOWNLOAD_PAGES[currentDownloadPage]);
                            break;
#endif                            
                        case 0:
                        default:
                            handleSendLocalPage(PAGE_MAINMENU);
                            break;
                    };
                    return;
                }
                break;
        }
    }
    keyboardLocalBuffer = "";

    // No *00 sequence detected, handle normal input
    switch(data)
    {
        case '\b':  // Handle Backspace
            break;

        case '0':
        case '#':
        case '\x5F':  // # 
        case CHAR_CR: // Handle Carriage Return // Do not handle newline to prevent double handling
            LOGIP("Show page 0: Main menu");
            // Reset the phonebook index and cursor visibility for the main menu page
            currentPage = 0;
            phonebookIndex = -1;
            handleSendLocalPage(PAGE_MAINMENU);
            break;
        case '1':
            LOGIP("Show page 1: Dial phonebook");
            currentPage = 1;
            phonebookIndex = -1;
            sendPhoneBookPage(false, 0);
            break;
        case '2':
            LOGIP("Show page 2: Edit phonebook");
            currentPage = 2;
            phonebookIndex = -1;
            sendPhoneBookPage(true, 0);
            break;
        case '3':
            LOGIP("Show page 3: Help & Color Test");
            currentPage = 3;
            handleSendLocalPage(PAGE_COLOR_AND_HELP);
            break;
        case '4':
            LOGIP("Show page 4: Status page");
            currentPage = 4;
            sendStatusPage();
            break;
#if defined(M2009EMULATOR)            
        case '5':
            LOGIP("Show page 5: Download page");
            currentPage = 5;
            currentDownloadPage = 0;
            handleSendLocalPage(*DOWNLOAD_PAGES[0]);
            break;    
#endif        
        case '9': 
            LOGIP("Verbreek lokale verbinding");
            disconnectLocal();
            break;    

        default:
            break;
    }
}

void handleLocalEditline(uint8_t data)
{
    LOGIP("Handling local edit line input: %02x cursorPos: %d maxlength: %d", data, localEditLine.cursorPos, localEditLine.maxlength);

    if (data == CHAR_BACKSPACE) {
        if (localEditLine.cursorPos > 0 &&
            (localEditLine.cursorPos < (localEditLine.maxlength - 1) || localEditLine.content[localEditLine.cursorPos] == 0)) {
            handleSendByte(0x08); // Send Backspace
        }

        handleSendByte('.');
        handleSendByte(0x08); // Send Backspace again to move cursor back
        
        if (localEditLine.cursorPos > 0 &&
            (localEditLine.cursorPos < (localEditLine.maxlength - 1) || localEditLine.content[localEditLine.cursorPos] == 0)) {
                localEditLine.cursorPos--;
        }
        localEditLine.content[localEditLine.cursorPos] = 0;
        LOGIP("Local edit line (cursorPos: %d): %s", localEditLine.cursorPos, localEditLine.content);
        return;
    } else  {
        switch (localEditLine.type) {
            case FIELDTYPE_STR:
                // Handle string input type
                if (!strchr("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 ", data)) {
                    return; // Ignore invalid characters for string input type
                }
                break;
            case FIELDTYPE_NUMBER:
                // Handle numeric input type
                if (!strchr("0123456789", data)) {
                    return; // Ignore invalid characters for string input type
                }
                break;
            case FIELDTYPE_URL:
                // Handle URL input type
                if (!strchr("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-._~:/?@&'+=%", data)) {
                    return; // Ignore invalid characters for URL input type
                }
                break;
            case FIELDTYPE_SERVERTYPE:
                {
                    // Handle Teletext input type
                    if (!strchr("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 ", data)) {
                        return; // Ignore invalid characters for Teletext input type
                    }
                    uint8_t serverType = StrToServertype(localEditLine.content);
                    serverType = (serverType + 1) % NUM_SERVER_TYPES;  // Loop through server types
                    strcpy(localEditLine.content, ServertypeToStr(static_cast<ServerType>(serverType)));

                    setCursorPosition(localEditLine.startXpos, localEditLine.startYpos, CURSOR_VISIBLE);
                    handleSendLocalPage(std::string(localEditLine.content));
                    return;
                }
                break;
            case FIELDTYPE_BAUDRATE:
                {
                    // Handle Baudrate input type
                    if (!strchr("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789", data)) {
                        return; // Ignore invalid characters for Baudrate input type
                    }
                    uint32_t baudrate = StrToBaud(localEditLine.content);
                    baudrate = (baudrate + 1) % NUM_BAUD_RATES;  // Loop through baud rates
                    strcpy(localEditLine.content, BaudToStr(baudrate));

                    setCursorPosition(localEditLine.startXpos, localEditLine.startYpos, CURSOR_VISIBLE);
                    handleSendLocalPage(std::string(localEditLine.content));
                    return;
                }
                break;
            default:
                // Handle other input types
                break;
        }

        // Return entered chararter
        handleSendByte(data);

        // Update cursor position and content
        localEditLine.content[localEditLine.cursorPos] = data;
        if (localEditLine.cursorPos < (localEditLine.maxlength - 1)) {
            localEditLine.cursorPos++;
            localEditLine.content[localEditLine.cursorPos + 1] = 0; 
        } else {
            // End of line move cursor back to the last position
            handleSendByte('\b');
        }
        LOGIP("Local edit line (cursorPos: %d): %s", localEditLine.cursorPos, localEditLine.content);
    }
}


void handleLocalPhonebookPage(uint8_t data)
{
    LOGIP("Edit item index %d data: %02x (%c)", localEditLine.index, data, data);
    if (localEditLine.bEscape || data == CHAR_ESCAPE) {

        // Handle escape character  ESC-J is store ESC-K is cancel
        if (localEditLine.bEscape) {
            localEditLine.bEscape = false; 
            switch(data) {
                case 'j':
                case 'J':
                case 'k':
                case 'K':
                    LOGIP("Handling escape sequence: %c to stop edditing", data);
                    if (data == 'J' || data == 'j') {
                        // Handle save action
                        storeEditLine(localEditLine.index); // Save the current edit line

                        LOGIP("Saved phone index %d", phonebookIndex);
                        strncpy(phonebook[phonebookIndex].url,    editPBItem.url,    sizeof(phonebook[phonebookIndex].url));
                        strncpy(phonebook[phonebookIndex].number, editPBItem.number, sizeof(phonebook[phonebookIndex].number));
                        strncpy(phonebook[phonebookIndex].name,   editPBItem.name,   sizeof(phonebook[phonebookIndex].name));
                        phonebook[phonebookIndex].type = editPBItem.type;

                        #if defined(M2009EMULATOR)
                        if (phonebook[phonebookIndex].type == ServerType::P2000FTP) {
                            strncpy(phonebook[phonebookIndex].number,  "---", sizeof(phonebook[phonebookIndex].number));
                        }
                        if (phonebook[phonebookIndex].type == ServerType::INTERNALMENU) {
                            strncpy(phonebook[phonebookIndex].url,  "localhost/intern:menu", sizeof(phonebook[phonebookIndex].url));
                        }
                        #endif
                        
                        storePhonebook(); // Save the updated phonebook to EEPROM
                    }
                    phonebookIndex = -1;
                    handleSendByte(CHAR_HIDE_CURSOR);
                    sendPhoneBookPage(true, phonebookPage);
                    break;
                    
                case '\x5D': // insert character
                    for (int i = localEditLine.maxlength - 1; i > localEditLine.cursorPos; i--) {
                        localEditLine.content[i] = localEditLine.content[i - 1];
                    }
                    localEditLine.content[localEditLine.cursorPos + 1] = ' '; 
                    setCursorPosition(localEditLine.startXpos, localEditLine.startYpos, CURSOR_KEEP_AS_IS);
                    handleSendLocalPage(localEditLine.content);
                    setCursorPosition(localEditLine.startXpos + localEditLine.cursorPos, localEditLine.startYpos, CURSOR_KEEP_AS_IS);
                    break;
                case '\x5B': // delete character
                    for (int i = localEditLine.cursorPos; i < localEditLine.maxlength - 1; i++) {
                        localEditLine.content[i] = localEditLine.content[i + 1];
                    }
                    localEditLine.content[localEditLine.maxlength - 1] = ' ';
                    setCursorPosition(localEditLine.startXpos, localEditLine.startYpos, CURSOR_KEEP_AS_IS);
                    handleSendLocalPage(localEditLine.content);
                    setCursorPosition(localEditLine.startXpos + localEditLine.cursorPos, localEditLine.startYpos, CURSOR_KEEP_AS_IS);
                    break;

                default:
                    handleSendByte(CHAR_SHOW_CURSOR);
                    handleSendByte(CHAR_BELL);
                    return;
            }
        } else {
            handleSendByte(CHAR_HIDE_CURSOR);
            localEditLine.bEscape = true; 
        }
        
    } else {
        switch(data)
        {
            case '\x5F': // # == ENTER character
            case '#':
            case CHAR_CURSOR_DOWN: //case CHAR_CR: // Handle Carriage Return // Do not handle newline to prevent double handling
                // Store the edited content into the phonebook entry
                storeEditLine(localEditLine.index);
                localEditLine.index = (localEditLine.index + 1) % NUM_OF_EDIT_INDEX_LINES;
                LOGIP("Stored edit line for index %d", localEditLine.index);
                
                // Prep next edit line for the same phonebook entry
                fillEditPhoneBookLine(localEditLine.index);
                setCursorPosition(localEditLine.startXpos + strlen(localEditLine.content), localEditLine.startYpos, CURSOR_VISIBLE);
                break;

            case CHAR_CURSOR_UP: 
                // Store the edited content into the phonebook entry
                storeEditLine(localEditLine.index);
                if (localEditLine.index == 0) {
                    localEditLine.index = LAST_EDIT_INDEX_LINE;
                } else {
                    localEditLine.index = localEditLine.index - 1;
                }
                LOGIP("Moving to previous edit line index %d", localEditLine.index);
                // Prep next edit line for the same phonebook entry
                fillEditPhoneBookLine(localEditLine.index);
                setCursorPosition(localEditLine.startXpos + strlen(localEditLine.content), localEditLine.startYpos, CURSOR_VISIBLE);
                break;

            default:
                handleLocalEditline(data);
                break;
        }
    }
}

void disconnectLocal() {
    LOGIP("Disconnecting local connection.");
    handleSendLocalPage("\x0C\x1B\x47Verbinding verbreken...\r\n\r\n");
    handleSendLocalPage(GoodByStr);
    handleSendLocalPage("\r\n");
    setConnectionType(connectionType::DISCONNECTED);
    disconnect();
}

void startLocalConnection() {
    LOGIP("Starting local connection.");
    // Implement the logic to start a local connection here
    handleSendLocalPage(PAGE_MAINMENU);
    currentPage = 0;
    phonebookIndex = -1;
}

void pollLocal()
{
    // Implement polling logic for local menu here
    // For timerhandling, you can implement periodic checks or updates related to teletext here.
}

int charToPhonebookIndex(uint8_t data) {
    // Convert '1'-'9', 0-8 and 'A'-'Z', 'a'-'z' to 9+
    if (data >= '1' && data <= '9') {
        return data - '1';
    }
    if (data >= 'A' && data <= 'Z') {
        return data - 'A' + 9;
    }
    if (data >= 'a' && data <= 'z') {
        return data - 'a' + 9;
    }
    return -1; // Invalid character
}

void handleSendLocal(uint8_t data)
{
    // Implement the logic to send data to the local connection
    LOGIP("Handle local %02x (%c) page: %d.", data, data, currentPage);

    switch (currentPage)
    {
        case 1: 
           {    // Handle page 1: phonebook dial page
                int idx = charToPhonebookIndex(data); 
                if (idx >= 0 && idx < MAX_PHONEBOOK_ITEMS) {
                    phonebookIndex = idx;
                    LOGIP("Connecting to item %d %s ", phonebookIndex, phonebook[phonebookIndex].name);
                    if (phonebook[phonebookIndex].type == ServerType::TELETEXT) {
                        startTeletextConnection(phonebookIndex);
                    } else if (phonebook[phonebookIndex].type == ServerType::VIEWDATA) {
                        startViewdataConnection(phonebookIndex); 
                    } else if (phonebook[phonebookIndex].type == ServerType::BBS) {
                        // startBbsConnection(phonebookIndex);
                        teletextInfoPage(ERROR_PAGE_TITLE, "BBS not supported yet", "Sorry...");
#if defined(M2009EMULATOR)                        
                    } else if (phonebook[phonebookIndex].type == ServerType::P2000FTP) {
                        teletextInfoPage(ERROR_PAGE_TITLE, "P2000 FTP server does not support dialing", "Only accessible via FTP-client");
#endif                        
                    } else {
                        // startOtherConnection(phonebookIndex);
                        teletextInfoPage(ERROR_PAGE_TITLE, "Other server types not supported yet", "Sorry...");
                    }
                } else if (data == '0') {
                    currentPage = 0;
                    phonebookIndex = -1;
                    handleSendLocalPage(PAGE_MAINMENU);
                } else if ((data == '<') || (data == CHAR_CURSOR_UP) || (data == CHAR_CURSOR_LEFT)) {
                    // Go to previous phonebook page (over/undergflow is handled in sendPhoneBookPage)
                    sendPhoneBookPage(false, phonebookPage-1);
                } else if ((data == '>') || (data == '#') || (data == '\x5F') || (data == CHAR_CURSOR_DOWN) || (data == CHAR_CURSOR_RIGHT)) {
                    // Go to previous phonebook page (over/undergflow is handled in sendPhoneBookPage)
                    sendPhoneBookPage(false, phonebookPage+1);
                }
            }
            break;

        case 2: // Handle page 2: phonebook edit page
            if (phonebookIndex < 0) {
                int idx = charToPhonebookIndex(data); 
                if (idx >= 0 && idx < MAX_PHONEBOOK_ITEMS) {
                    phonebookIndex = idx;
                    LOGIP("Edit item %d", phonebookIndex);

                    // Save a local copy of the phonebook item for editing (if user wants cancel changes)
                    strncpy(editPBItem.url, phonebook[phonebookIndex].url, sizeof(phonebook[phonebookIndex].url));
                    strncpy(editPBItem.number, phonebook[phonebookIndex].number, sizeof(phonebook[phonebookIndex].number));
                    strncpy(editPBItem.name, phonebook[phonebookIndex].name, sizeof(phonebook[phonebookIndex].name));
                    editPBItem.type = phonebook[phonebookIndex].type;
                    fillEditPhoneBookLine(0);

                    sendPhoneBookItem(phonebookIndex);
                } else if (data == '0') {
                    // Return to main menu
                    currentPage = 0;
                    phonebookIndex = -1;
                    handleSendLocalPage(PAGE_MAINMENU);
                } else if ((data == '<') || (data == CHAR_CURSOR_UP) || (data == CHAR_CURSOR_LEFT)) {
                    // Go to previous phonebook page (over/undergflow is handled in sendPhoneBookPage)
                    sendPhoneBookPage(true, phonebookPage-1);
                } else if ((data == '>') || (data == '#') || (data == '\x5F') || (data == CHAR_CURSOR_DOWN) || (data == CHAR_CURSOR_RIGHT)) {
                    // Go to previous phonebook page (over/undergflow is handled in sendPhoneBookPage)
                    sendPhoneBookPage(true, phonebookPage+1);
                }
            } else {
                handleLocalPhonebookPage(data);
            }
            break;

        case 4:
            // Handle page 4: status page
            LOGIP("Handling status page input: %c", data);
            if (data == '1') {
                if (m_statusPageActive) {
                    LOGIP("User wants to disconnect");
                    // Handle specific logic for data '1' on page 4
                    switch (getConnectionType())
                    {
                        case connectionType::VIEWDATA:
                            disconnectViewdata();
                            break;
                        case connectionType::TELETEXT:
                            disconnectTeletext("          Goodbye");
                            break; 
                        case connectionType::LOCALMENU:
                            disconnectLocal();
                            break;
                    default:
                        break;
                    }
                    m_statusPageActive = false;
                    currentPage = 0;
                    phonebookIndex = -1;
                }
            } if (data == '0') {
                 if (m_statusPageActive) {
                    LOGIP("User wants to return");
                     m_statusPageActive = false;
                     switch (getConnectionType())
                     {
                        case connectionType::TELETEXT:
                            handleTeletextPageReload();
                            break;
                        
                        case connectionType::VIEWDATA:
                            handleViewdataPageReload();
                            break;

                        default:
                            break;
                     }
                 } else {
                    currentPage = 0;
                    phonebookIndex = -1;
                    handleSendLocalPage(PAGE_MAINMENU);
                }
            } else {
                // TODO: Implement logic for editing the status page config
                // IP address and speeds
            }
            break;

#if defined(M2009EMULATOR)
        case 5:
            // Handle page 5: donwload pages  (*00 reload is handled globally)
            LOGIP("Download page (%02x) page: %d.", data, currentDownloadPage);
            if (data == 0x5F) {  // Start downloading
                currentDownloadPage = 1;
                handleSendLocalPage(*DOWNLOAD_PAGES[currentDownloadPage]);
            }
            if (data == 0x23) { // Send next page
                currentDownloadPage++;
                if (currentDownloadPage < NUM_OF_DOWNLOAD_PAGES) {
                    handleSendLocalPage(*DOWNLOAD_PAGES[currentDownloadPage]);
                } else {
                    data = 0; // Return to main menu
                }
            }
            if (data == '0') { // Back to main menu
                currentPage = 0;
                handleSendLocalPage(PAGE_MAINMENU);
            }
            break;        
#endif                            

        case 0: // Handle page 0: main menu
        case 3: // Handle page 3: help & color test page
            // Handle page 0, and 3 specific logic here
            handleLocalMenus(data);
            break;

        default:
            currentPage = 0;
            phonebookIndex = -1;
            handleSendLocalPage(PAGE_MAINMENU);
            break;
        
    }
}

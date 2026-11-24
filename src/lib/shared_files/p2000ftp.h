/**
 * P2000ftp header file for the Miniware M2009 Modem Cartridge / Viditel Modem Emulator.
 * 
 * Author: Bart Eversdijk
 * 
 * Contains declarations and definitions related to P2000ftp functionality.
 */

#if defined(M2009EMULATOR)
#pragma

#define SPECIAL_FTP_SERVER 31


#define P2000T_RED        "\x81"
#define P2000T_GREEN      "\x82"
#define P2000T_YELLOW     "\x83"
#define P2000T_BLUE       "\x84"
#define P2000T_MAGENTA    "\x85"
#define P2000T_CYAN       "\x86"
#define P2000T_WHITE      "\x87"

#define P2000T_GR_RED     "\x91"
#define P2000T_GR_GREEN   "\x92"
#define P2000T_GR_YELLOW  "\x93"
#define P2000T_GR_BLUE    "\x94"
#define P2000T_GR_MAGENTA "\x95"
#define P2000T_GR_CYAN    "\x96"
#define P2000T_GR_WHITE   "\x97"

bool openFtpConnection();
bool openFtpConnection(std::string url);
void disconnectFtp();
void handleFtp(uint8_t data);
void pollFtp();
void phoneOnHookFtp();
extern void disconnect();
extern void handleSendByte(uint8_t data);
extern void handleSendByteAlways(uint8_t data);
extern void setConnectionType(connectionType type);
#endif
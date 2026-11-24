/**
 * Viewdata header file for the Miniware M2009 Modem Cartridge / Viditel Modem Emulator.
 * 
 * Author: Bart Eversdijk
 * 
 * Contains declarations and definitions related to viewdata functionality.
 */

extern std::string GoodByStr;

void startViewdataConnection(int serverId);
bool openViewdataConnection(std::string url);
void disconnectViewdata();
void handleViewdata(uint8_t data);
void pollViewdata();
void phoneOnHookViewdata();
void handleViewdataPageReload();
extern void disconnect();
extern void handleSendByte(uint8_t data);
extern void setConnectionType(connectionType type);
void parseUrl(const std::string& url, std::string& outHost, int& outPort, int defaultPort);
/**
 * Other header file for the Miniware M2009 Modem Cartridge / Viditel Modem Emulator.
 * 
 * Author: Bart Eversdijk
 * 
 * Contains declarations and definitions related to other functionality.
 */

void startOtherConnection(int serverId);
bool openOtherConnection(std::string url);
void disconnectOther();
void handleOther(uint8_t data);
void pollOther();
void phoneOnHookOther();
extern void disconnect();
extern void handleSendByte(uint8_t data);
extern void handleSendByteAlways(uint8_t data);
extern void setConnectionType(connectionType type);
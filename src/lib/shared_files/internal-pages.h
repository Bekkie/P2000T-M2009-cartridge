/**
 * Internal pages for the Miniware M2009 Modem Cartridge / Viditel Modem Emulator
 * 
 * Author: Bart Eversdijk
 */

#if defined(M2009EMULATOR)
  #define PAGE_HEADER "   === M2009 MODEM EMULATOR ==="
#endif
#if defined(VIEWDATAEMULATOR)
  #define PAGE_HEADER "   === VIDITEL MODEM EMULATOR ==="
#endif

#define CTRL_BLACK_TXT       "\x1B\x40"
#define CTRL_RED_TXT         "\x1B\x41"
#define CTRL_GREEN_TXT       "\x1B\x42"
#define CTRL_YELLOW_TXT      "\x1B\x43"
#define CTRL_BLUE_TXT        "\x1B\x44"
#define CTRL_MAGENTA_TXT     "\x1B\x45"
#define CTRL_CYAN_TXT        "\x1B\x46"
#define CTRL_WHITE_TXT       "\x1B\x47"

#define CTRL_FLASH_ON        "\x1B\x48"
#define CTRL_FLASH_OFF       "\x1B\x49"

#define CTRL_INVERSE_ON       "\x1B\x4A"
#define CTRL_INVERSE_OFF      "\x1B\x4B"

#define CTRL_NORMAL_SIZE     "\x1B\x4C"
#define CTRL_DOUBLE_SIZE     "\x1B\x4D"

#define CTRL_BLACK_GRPH      "\x1B\x50"
#define CTRL_RED_GRPH        "\x1B\x51"
#define CTRL_GREEN_GRPH      "\x1B\x52"
#define CTRL_YELLOW_GRPH     "\x1B\x53"
#define CTRL_BLUE_GRPH       "\x1B\x54"
#define CTRL_MAGENTA_GRPH    "\x1B\x55"
#define CTRL_CYAN_GRPH       "\x1B\x56"
#define CTRL_WHITE_GRPH      "\x1B\x57"

#define CTRL_NORMAL_GRPH     "\x1B\x59"
#define CTRL_SMALL_GRPH      "\x1B\x5A"
#define CTRL_BACKG_BLACK     "\x1B\x5C"
#define CTRL_BACKG_COLOR     "\x1B\x5D"
#define CTRL_REPEAT_GRPH     "\x1B\x5E"
#define CTRL_NOREPEAT_GRPH   "\x1B\x5F"

#define CTRL_CR_LF           "\r\n"

#define CHAR_BELL             '\x07'
#define CHAR_BACKSPACE        '\x08'
#define CHAR_CURSOR_LEFT      '\x08'
#define CHAR_CURSOR_RIGHT     '\x09'
#define CHAR_TAB              '\x09'
#define CHAR_CURSOR_DOWN      '\x0A'
#define CHAR_NEWLINE          '\x0A'
#define CHAR_CURSOR_UP        '\x0B'
#define CHAR_FORM_FEED        '\x0C'
#define CHAR_CR               '\x0D'
#define CHAR_BACKTAB          '\x0E'

#define CHAR_SHOW_CURSOR      '\x11'
#define CHAR_REPEAT           '\x12'
#define CHAR_HIDE_CURSOR      '\x14'
#define CHAR_CANCEL           '\x18'
#define CHAR_CURSOR_HOME      '\x1E'
#define CHAR_ESCAPE           '\x1B'

#define VIEWDATA_TXT "Videotext  "
#define TELETEXT_TXT "Teletext   "
#define BBS_TXT      "BBS        "
#define OTHER_TXT    "Overige    "
#if defined(M2009EMULATOR)
  #define P2000FTP_TXT "FTP (P2000)"
  #define INTERNAL_TXT "Intern Menu"
#endif

#define FIELDTYPE_STR 1
#define FIELDTYPE_NUMBER 2
#define FIELDTYPE_URL 3
#define FIELDTYPE_SERVERTYPE 4
#define FIELDTYPE_BAUDRATE 5

#define CURSOR_VISIBLE 1
#define CURSOR_HIDDEN  0 
#define CURSOR_KEEP_AS_IS 2

struct LineEditEntry {
    uint8_t type;  // 1 str 2 number 3 url 4 viewdata/teletext
    uint8_t index;
    bool bEscape; // Flag to indicate if the escape character was pressed
    char content[125];
    uint8_t maxlength;
    uint8_t cursorPos;
    uint8_t startXpos;
    uint8_t startYpos;
};

void startLocalConnection();
void disconnectLocal();
void handleSendLocal(uint8_t data);
void pollLocal();
void sendStatusPage(bool duringDialoutConnection);
extern bool m_statusPageActive;

void setCursorPosition(uint8_t x, uint8_t y, uint8_t visible);
            
const char * ServertypeToStr(ServerType type);
ServerType StrToServertype(const std::string& str);

extern void disconnect();
extern String author;
extern String version;

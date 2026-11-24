/*
 *  WiFi Settings and Phone Book Handling for Miniware M2009 Modem Cartridge / Viditel Modem Emulator
 * 
 *  Author: Bart Eversdijk
 *
 *  Description: Handles reading and saving phone book data to NVM and updating it via a web interface.
*/
#include <EEPROM.h>
#include <WebServer.h>
#include <DNSServer.h>

#define ENABLE_LOG_WIFI 1
#define ENABLE_LOG_UART 1
#define ENABLE_LOG_PHONE_BOOK 1
#include "config.h"

#include "wifi-portal.h"
#include "internal-pages.h"
#include "network.h" 

WebServer server(80);
DNSServer dnsServer;
IPAddress apIP(192, 168, 4, 1);
String htmlForm;
bool ledStatus = false;
int ledOnCount = 0;
unsigned long newFlashTimestamp = 0, lastFlashTimestamp = 0;

bool useStaticIPaddress = true;


String curSsid = "";
String curPassword = "";
bool curUseStatic = false;
String curStaticIp = "192.168.1.";
String curGateway = "192.168.1.1";
String curSubnet = "255.255.255.0";

#if defined(M2009EMULATOR) 
  #define PORTAL_NAME "M2009-Config-Portal"
  UARTConfigBlock uartSimConfig;
  uint16_t baudRates[NUM_BAUD_RATES] =  {300, 75, 1200, 2400, 9600, 0};
  const char * baudRatesTxt[NUM_BAUD_RATES] = {"300 baud (V.21)", "1200/75 baud (V.23)", "1200 baud (V.23)", "2400 baud (V.22bis)", "9600 baud (V.32)", "Max. speed"};
#endif
#if defined(VIEWDATAEMULATOR)
   #define PORTAL_NAME "Viditel-Config-Portal"
   UARTConfig uartConfig;
   uint16_t baudRates[NUM_BAUD_RATES] =  {300, 75, 1200, 2400, 9600, 14400, 19200, 28800, 33600, 56000};
   const char * baudRatesTxt[NUM_BAUD_RATES] = { "300 baud (V.21)", "1200/75 baud (V.23)", "1200 baud (V.23)", "2400 baud (V.22bis)", "9600 baud (V.32)", "14.4k (V.32bis)", "19200 baud", "28.8k (V.34)", "36.6K baud", "56k (V.90/V.92)"};
#endif

// ============================================================================
// STRUCTURE FOR THE PHONE BOOK & DIGITAL BBS
// ============================================================================
PhoneNumberToSite phonebook[MAX_PHONEBOOK_ITEMS];
bool forcedPortal = false;

// ============================================================================
// EEPROM (flash emulated) layout: [WifiConfig][PhoneBookBlock]
// ============================================================================
#define EEPROM_SIZE 2048
#define WIFI_MAGIC  0x57494649UL
#define PHONE_MAGIC 0x50486F4EUL
#define UART_MAGIC  0x55524154UL

constexpr  int WIFI_ADDR  = 0;
constexpr  int UART_ADDR  = WIFI_ADDR + sizeof(WifiConfig);
constexpr  int PHONE_ADDR = UART_ADDR + sizeof(UARTConfigBlock);
constexpr  int PHONE_ADDR_END = PHONE_ADDR + sizeof(CompressedPhoneItem) * MAX_PHONEBOOK_ITEMS;
static_assert(PHONE_ADDR_END <= EEPROM_SIZE, "ERROR: De data past niet binnen de EEPROM_SIZE van 2048 bytes!");

String htmlHeader =   "<!DOCTYPE html><html><head><meta name='viewport' content='width=device-width, initial-scale=1'>"
               "<style>body{font-family:Arial; margin:20px;} input, select{display:block; width:100%; margin-bottom:15px; padding:10px; box-sizing:border-box;} "
              ".btn{color:white; border-radius: 8px; border: none; padding: 8px 12px;} "
              ".radio-group{display:flex; gap:20px; margin-bottom:15px;} .radio-group label{display:inline; margin-left:5px;} "
              ".radio-group input{display:inline; width:auto; margin-bottom:0;} "
              "#static-fields {background:#f9f9f9; padding:15px; border-radius:8px; margin-bottom:15px; border:1px solid #ddd;}</style>"
#if defined(M2009EMULATOR) 
               "<h1>M2009 emulator for P2000T</h1>"; 
#endif
#if defined(VIEWDATAEMULATOR)
              "<h1>Viditel modem emulator for P2000T</h1>";
#endif

// ============================================================================
// WIFI CONFIG HANDLING
// ============================================================================
static bool loadWifiConfig(String &ssid, String &password, bool &useStatic, String &staticIp, String &gateway, String &subnet) {
  EEPROM.begin(EEPROM_SIZE);
  
  WifiConfig cfg;
  EEPROM.get(WIFI_ADDR, cfg);
  EEPROM.end(); 

  if (cfg.magic != WIFI_MAGIC) return false;
  
  // Zorg voor nette string-afsluitingen
  cfg.ssid[sizeof(cfg.ssid) - 1] = '\0';
  cfg.password[sizeof(cfg.password) - 1] = '\0';
  cfg.staticIP[sizeof(cfg.staticIP) - 1] = '\0';
  cfg.gateway[sizeof(cfg.gateway) - 1] = '\0';
  cfg.subnet[sizeof(cfg.subnet) - 1] = '\0';
  
  ssid = cfg.ssid;
  password = cfg.password;
  useStatic = cfg.useStaticIP;
  staticIp = cfg.staticIP;
  gateway = cfg.gateway;
  subnet = cfg.subnet;
  return true;
}

static void saveWifiConfig(const String &ssid, const String &password, bool useStatic, const String &staticIp, const String &gateway, const String &subnet) {
  EEPROM.begin(EEPROM_SIZE);
  WifiConfig cfg;
  memset(&cfg, 0, sizeof(cfg));
  
  cfg.magic = WIFI_MAGIC;
  strncpy(cfg.ssid, ssid.c_str(), sizeof(cfg.ssid) - 1);
  strncpy(cfg.password, password.c_str(), sizeof(cfg.password) - 1);
  
  cfg.useStaticIP = useStatic;
  strncpy(cfg.staticIP, staticIp.c_str(), sizeof(cfg.staticIP) - 1);
  strncpy(cfg.gateway, gateway.c_str(), sizeof(cfg.gateway) - 1);
  strncpy(cfg.subnet, subnet.c_str(), sizeof(cfg.subnet) - 1);
  
  EEPROM.put(WIFI_ADDR, cfg);
  EEPROM.commit();
  EEPROM.end(); 
}

static void clearWifiConfig() {
  EEPROM.begin(EEPROM_SIZE);
  WifiConfig cfg;
  memset(&cfg, 0, sizeof(cfg));
  // Door memset is cfg.magic nu 0, wat de configuratie ongeldig maakt bij de volgende boot
  EEPROM.put(WIFI_ADDR, cfg);
  EEPROM.commit();
}

void deviceRestart() {
  #if defined(ARDUINO_ARCH_RP2040)
    rp2040.restart();
  #endif
  #if defined(ARDUINO_ARCH_RP2050)
    rp2050.restart();
  #endif
  #if defined(ARDUINO_ARCH_ESP32)
    ESP.restart();
  #endif
}

// ============================================================================
// Wi-Fi settings page
// ============================================================================
void generateHtmlForm() {
  LOGWIFI("Scanning for WiFi networks...");
  int n = WiFi.scanNetworks();
  if (n < 0) {
    LOGWIFI("WiFi scan failed (error %d).\n", n);
  } else {
    LOGWIFI("%d networks found.", n);
  }

  htmlForm = "";
  htmlForm.reserve(24000);

  // Start building the HTML page
  htmlForm += htmlHeader;

  // JavaScript to toggle visibility of static IP fields
  htmlForm += "<script>function toggleIpFields(isStatic){document.getElementById('static-fields').style.display = isStatic ? 'block' : 'none';}</script>";
  htmlForm += "</head><body>";

  htmlForm += "<h2>Wi-Fi Settings</h2>";
  htmlForm += "<form method='POST' action='/save'>";
  
  htmlForm += "<label>Select Wi-Fi Network (SSID):</label>";
  htmlForm += "<select name='ssid'>";
  
  if (n <= 0) {
    htmlForm += n < 0
        ? "<option value=''>Wi-Fi scan failed (" + String(n) + ")</option>"
        : "<option value=''>No networks found</option>";
  } else {
    for (int i = 0; i < n; ++i) {
      String ssid = WiFi.SSID(i);
      int rssi = WiFi.RSSI(i);
#if ENABLE_LOG_WIFI       
      Serial.print("Found network: ");
      Serial.print(ssid);
      Serial.print(" (");
      Serial.print(rssi);
      Serial.println(" dBm)");
#endif      
      htmlForm += "<option value='" + ssid + "'" + (ssid == curSsid ? " selected" : "") + ">" + ssid + " (" + String(rssi) + " dBm)</option>";
    }
  }
  htmlForm += "</select>";
  
  htmlForm += "<label>Wifi password:</label><input type='password' name='password' required>";
  
  // RADIO BUTTONS: DHCP vs Static IP
  htmlForm += "<label>IP Configuration:</label>";
  htmlForm += "<div class='radio-group'>";
  
  htmlForm += "<input type='radio' id='dhcp' name='ip_mode' value='dhcp' " + String(!curUseStatic ? "checked" : "") + " onclick='toggleIpFields(false)'><label for='dhcp'>DHCP (Automatic)</label>";
  htmlForm += "<input type='radio' id='static' name='ip_mode' value='static' " + String(curUseStatic ? "checked" : "") + " onclick='toggleIpFields(true)'><label for='static'>Static IP</label>";
  htmlForm += "</div>";

  // STATIC IP CONFIGURATION FIELDS
  htmlForm += "<div id='static-fields' style='display:" + String(curUseStatic ? "block" : "none") + ";'>";
  htmlForm += "<label>Static IP Address:</label><input type='text' name='static_ip' value='" + curStaticIp + "' placeholder='e.g. 192.168.1.150'>";
  htmlForm += "<label>Gateway:</label><input type='text' name='gateway' value='" + curGateway + "' placeholder='e.g. 192.168.1.1'>";
  htmlForm += "<label>Subnet Mask:</label><input type='text' name='subnet' value='" + curSubnet + "' placeholder='e.g. 255.255.255.0'>";
  htmlForm += "</div>";

  htmlForm += "<input type='submit' value='Save and restart' class='btn' style='background-color:#4CAF50;'>";
  htmlForm += "</form>"; 

  // Clean up scan results from memory
  WiFi.scanDelete();

  htmlForm += "<form method='POST' action='/confirmClearwifi' style='margin-top:10px;'>";
  htmlForm += "<input type='submit' value='Erase Wi-Fi settings' class='btn' style='background-color:#ff4d4d;'>";
  htmlForm += "</form>";

#if defined(M2009EMULATOR)
  // SECTION: UART Simulation configuration
  htmlForm += "<div class='card'><h2>Simulation Configuration</h2>";
  htmlForm += "<form method='POST' action='/saveconfig'>";
  htmlForm += "<label>Baudrate:</label><select name='baudrate'>";
  for (uint16_t i = 0; i < NUM_BAUD_RATES; i++) {
    htmlForm += "<option value='" + String(baudRates[i]) + "'" + (baudRates[i] == uartSimConfig.baudrateSim ? " selected" : "") + ">" + String(baudRatesTxt[i]) + "</option>";
  }
  htmlForm += "</select>";
  htmlForm += "<label>Socket listen port:</label><input type='text' name='socket_port' value='" + String(uartSimConfig.port) + "' maxlength='5' max='65535' min='0'>";
  htmlForm += "<small>If set to 0, no socket will be listened on (default " + String(DEFAULT_HOSTPORT) + ")</small>";
  htmlForm += "<input type='submit' value='Save Config' class='btn' style='background-color:#4CAF50;'>";
  htmlForm += "</form></div>";
#endif

#if defined(VIEWDATAEMULATOR)
  // SECTION: UART configuration
  htmlForm += "<div class='card'><h2>UART Configuration</h2>";
  htmlForm += "<form method='POST' action='/saveconfig'>";
  htmlForm += "<label>Baudrate:</label><select name='baudrate'>";
  for (uint16_t i = 0; i < NUM_BAUD_RATES; i++) {
    htmlForm += "<option value='" + String(baudRates[i]) + "'" + (baudRates[i] == uartConfig.baudrateUp ? " selected" : "") + ">" + String(baudRatesTxt[i]) + "</option>";
  }
  htmlForm += "</select>";
  htmlForm += "<label>Data Bits:</label><div class='radio-group'>";
  htmlForm +=  "<input type='radio' id='dataBits' name='data_bits' value='dataBits8' " + String(uartConfig.dataBits8 ? "checked" : "") + "> 8";
  htmlForm +=  "<input type='radio' id='dataBits' name='data_bits' value='dataBits7' " + String(uartConfig.dataBits8 ? "" : "checked") + "> 7";
  htmlForm += "</div>";
  htmlForm += "<label>Stop Bits:</label><div class='radio-group'>";
  htmlForm +=  "<input type='radio' id='stopBits' name='stop_bits' value='stopBits1' " + String(uartConfig.stopBits1 ? "checked" : "") + "> 1";
  htmlForm +=  "<input type='radio' id='stopBits' name='stop_bits' value='stopBits2' " + String(uartConfig.stopBits1 ? "" : "checked") + "> 2";
  htmlForm += "</div>";
  htmlForm += "<label>Parity:</label><div class='radio-group'>";
  htmlForm +=  "<input type='radio' id='parity' name='parity' value='parityN' " + String(uartConfig.parity == 'N' ? "checked" : "") + "> None";
  htmlForm +=  "<input type='radio' id='parity' name='parity' value='parityE' " + String(uartConfig.parity == 'E' ? "checked" : "") + "> Even";
  htmlForm +=  "<input type='radio' id='parity' name='parity' value='parityO' " + String(uartConfig.parity == 'O' ? "checked" : "") + "> Odd";
  htmlForm += "</div>";
  htmlForm += "<input type='submit' value='Save UART Config' class='btn' style='background-color:#4CAF50;'>";
  htmlForm += "</form></div>";
#endif


  // SECTION: Edit phone book entries
  htmlForm += "<div class='card'><h2>Phone numbers to servers</h2>";
  htmlForm += "<small>The number can by use to dial (ATDT&lt;number&gt;)</small>";
  htmlForm += "<form method='POST' action='/savesettings'>";
  htmlForm += "<table><tr><th>Name</th><th>Number</th><th>Server Address</th><th>Server Type</th></tr>";
  
  for (int i = 0; i < MAX_PHONEBOOK_ITEMS; i++) {
    htmlForm += "<tr>";
    htmlForm += "<td><input type='text' name='name_" + String(i) + "' value='" + String(phonebook[i].name) + "' maxlength='" + String(MAX_NAME_LEN) + "'></td>";
    htmlForm += "<td><input type='text' name='nr_" + String(i) + "' value='" + String(phonebook[i].number) + "' maxlength='" + String(MAX_NUMBER_LEN) + "'></td>";
    htmlForm += "<td><input type='text' name='srv_" + String(i) + "' value='" + String(phonebook[i].url) + "' maxlength='" + String(MAX_URL_LEN) + "'></td>";
    htmlForm += "<td><select name='srv_type_" + String(i) + "'>";
    htmlForm += "<option value='VIEWDATA'" + String(phonebook[i].type == ServerType::VIEWDATA ? " selected" : "") + ">Videotext</option>";
    htmlForm += "<option value='TELETEXT'" + String(phonebook[i].type == ServerType::TELETEXT ? " selected" : "") + ">Teletext</option>";
    htmlForm += "<option value='BBS'" + String(phonebook[i].type == ServerType::BBS ? " selected" : "") + ">BBS</option>";
    htmlForm += "<option value='OTHER'" + String(phonebook[i].type == ServerType::OTHER ? " selected" : "") + ">Overige</option>";
#if defined(M2009EMULATOR)
    htmlForm += "<option value='FTP'" + String(phonebook[i].type == ServerType::P2000FTP ? " selected" : "") + ">ftp (P2000T)</option>";
    htmlForm += "<option value='INTERNALMENU'" + String(phonebook[i].type == ServerType::INTERNALMENU ? " selected" : "") + ">Intern menu</option>";
#endif
    
    htmlForm += "</select></td>";
    htmlForm += "</tr>";
  }
  
  htmlForm += "</table>";
  htmlForm += "<input type='submit' value='Store list' class='btn' style='background-color:#1a75ff;'>";
  htmlForm += "</form>";

  // SECTION: Restart
  htmlForm += "<form method='POST' action='/confirmRestart' style='margin-top:10px;'>";
  htmlForm += "<input type='submit' value='Restart emulator' class='btn' style='background-color:#ff8000;'>";
  htmlForm += "</form></div>";

  htmlForm += "<footer><p>Copyright: " + author + " | Version: " + version + "</p></footer>";
  htmlForm += "</body></html>";
}

void confirmRestart() {
  // Always regenerate or serve the list when the root is accessed
  String htForm = htmlHeader;
  htForm += "<h3>Confirm Restart</h3>";
  htForm += "<p>Are you sure you want to restart the emulator?</p>";
  htForm += "<form method='POST' action='/' style='margin-top:10px;'>";
  htForm += "<input type='submit' value='Cancel' class='btn' style='background-color:#4CAF50;'>";
  htForm += "</form>"; 
  htForm += "<form method='POST' action='/restart' style='margin-top:10px;'>";
  htForm += "<input type='submit' value='Restart Emulatie' class='btn' style='background-color:#ff4d4d;'>";
  htForm += "</form>";
  htForm += "</body></html>";

  server.send(200, "text/html", htForm);
}


void handleRestart() {
  server.send(200, "text/html", "<!DOCTYPE html><html><body><h2>Restarting... The modem emulator</h2></body></html>");
  delay(2000);
  deviceRestart();
}


void confirmClearWifi() {
  // Always regenerate or serve the list when the root is accessed
  String htForm = htmlHeader;
  htForm += "<h2>Confirm Wi-Fi Erase</h2>";
  htForm += "<p>Are you sure you want to erase the Wi-Fi settings?</p>";
  htForm += "<form method='POST' action='/' style='margin-top:10px;'>";
  htForm += "<input type='submit' value='Cancel' class='btn' style='background-color:#4CAF50;'>";
  htForm += "</form>"; 
  htForm += "<form method='POST' action='/clearwifi' style='margin-top:10px;'>";
  htForm += "<input type='submit' value='Erase Wi-Fi settings' class='btn' style='background-color:#ff4d4d;'>";
  htForm += "</form>";
  htForm += "</body></html>";

  server.send(200, "text/html", htForm);
}

void handleClearWifi() {
  clearWifiConfig();
  
  LOGWIFI("Wi-Fi-setting erased. Restarting...");
  server.send(200, "text/html", "<!DOCTYPE html><html><body><h2>Wifi settings are erased, the Pico will restart...</h2></body></html>");
  delay(2000);
  deviceRestart();
}

void handleRoot() {
  // Always regenerate or serve the list when the root is accessed
  if (htmlForm == "") {
    generateHtmlForm();
  }
  server.send(200, "text/html", htmlForm);
}

void handleSave() {
  String ssid = server.arg("ssid");
  String password = server.arg("password");
  String ipMode = server.arg("ip_mode"); // 'dhcp' or 'static'

  bool isStatic = (ipMode == "static");
  String staticIpStr = "";
  String gatewayStr = "";
  String subnetStr = "";

  if (isStatic) {
    staticIpStr = server.arg("static_ip");
    gatewayStr = server.arg("gateway");
    subnetStr = server.arg("subnet");
  }
  
  server.send(200, "text/html", "<!DOCTYPE html><html><body><h2>Data received! Saving and restarting...</h2></body></html>");
  delay(2000);
  
  // Store Wi-Fi and IP settings
  saveWifiConfig(ssid, password, isStatic, staticIpStr, gatewayStr, subnetStr);
  
  LOGWIFI("Data saved to EEPROM/Flash successfully. Restarting...");
  delay(500);
  
  // Restart to connect using the new credentials
  deviceRestart();
}

void startCaptivePortal() {
  LOGWIFI("\n[WIFI] Starting captive portal...");
  digitalWrite(PIN_LED_GREEN, LED_OFF); // Turn off the GREEN LED (normal operation stopped)
  digitalWrite(PIN_LED_RED, LED_ON);  // Turn on the RED LED when starting the captive portal

  readUARTConfig();
  
  WiFi.mode(WIFI_AP_STA);
  WiFi.disconnect();
  delay(100);

  WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));

  bool apStarted = WiFi.softAP(PORTAL_NAME);
  LOGWIFI("Captive AP start %s.\n", apStarted ? "succeeded" : "FAILED");

  loadWifiConfig(curSsid, curPassword, curUseStatic, curStaticIp, curGateway, curSubnet);

  // Scan after the portal access point has started.
  generateHtmlForm();
  
  dnsServer.start(DNS_PORT, "*", apIP);
  
  server.on("/generate_204", handleRoot);
  server.on("/fwlink", handleRoot);
  server.on("/", handleRoot);
  server.on("/save", HTTP_POST, handleSave);
  server.on("/savesettings", HTTP_POST, handleSaveSettings);
  server.on("/saveconfig", HTTP_POST, handleSaveUARTConfig);
  server.on("/confirmClearwifi", HTTP_POST, confirmClearWifi);
  server.on("/clearwifi", HTTP_POST, handleClearWifi);
  server.on("/confirmRestart", HTTP_POST, confirmRestart);
  server.on("/restart", HTTP_POST, handleRestart);

  server.onNotFound([]() {
    server.sendHeader("Location", String("http://") + apIP.toString(), true);
    server.send(302, "text/plain", "");
  });
  
  server.begin();
  LOGWIFI("Captive portal is active. Connect to '%s'.", PORTAL_NAME);
  
#if defined(M2009EMULATOR)
  bResetPending = false;
#endif

  // Stay in this loop until the portal is submitted and the ESP restarts
  while(true) {
    dnsServer.processNextRequest();
    server.handleClient();

    newFlashTimestamp = millis();
    if (newFlashTimestamp - lastFlashTimestamp > 100) {
        if (!ledStatus && ledOnCount > 4) { 
          ledStatus = true;
          ledOnCount = 0;
        } else  {
          ledStatus = false;
          ledOnCount++;
        }
        digitalWrite(PIN_LED_GREEN, ledStatus ? LED_ON : LED_OFF);
        lastFlashTimestamp = newFlashTimestamp;

        #if defined(M2009EMULATOR)
          if (bResetPending) {
            Serial.println("P2000T RESET detected (GP14), quit captive portal.");
            bResetPending = false;
            deviceRestart();
          } 
        #endif
    }
    delay(1);
  }
}

void wifiStartup() {
  LOGWIFI("Press the button within 3 seconds to reset Wi-Fi...");
  delay(1000);
  
  // Brief startup check: is the button being held down?
  for (int i = 0; i < 10; i++) {
    #if defined(ARDUINO_ARCH_RP2040)
      if (BOOTSEL) {
    #endif
    #if defined(VIEWDATAEMULATOR)
      if (digitalRead(RESET_WIFI_BUTTON_PIN) == LOW) {
    #endif
      LOGWIFI("Button pressed! Forcing the configuration portal to open...");
      forcedPortal = true;
      break; // Exit the loop and open the portal
    }
    delay(100);
  }

  if (!forcedPortal) {
    String ssid = "";
    String password = "";
    String staticIpStr = "";
    bool useStatic = false;
    String gatewayStr = "";
    String subnetStr = "";

    // Check whether usable credentials have been stored
    if (loadWifiConfig(ssid, password, useStatic, staticIpStr, gatewayStr, subnetStr)) {
      LOGWIFI("Stored Wi-Fi credentials found. SSID: %s", ssid.c_str());

      if (useStatic) {
          IPAddress local_IP;
          IPAddress gateway;
          IPAddress subnet;

          // Converteer strings naar IPAddress objecten
          if (local_IP.fromString(staticIpStr) && 
                gateway.fromString(gatewayStr) && 
                subnet.fromString(subnetStr)) {
                
              LOGWIFI("Static IP: %s", staticIpStr.c_str());
              WiFi.config(local_IP, gateway, subnet);
              useStaticIPaddress = true;
          } else {
              LOGWIFI("Wrong static IP setting! Fall back to DHCP.");
              useStaticIPaddress = false;
              WiFi.config(IPAddress(0,0,0,0), IPAddress(0,0,0,0), IPAddress(0,0,0,0)); // Force DHCP
          }
      } else {
          LOGWIFI("DHCP mode.");
          WiFi.config(IPAddress(0,0,0,0), IPAddress(0,0,0,0), IPAddress(0,0,0,0)); // Force DHCP
          useStaticIPaddress = false;
      }

      WiFi.mode(WIFI_STA);
      // Wi-Fi Persistentie in. remerber last channel and BSSID for fast reconnect
      WiFi.persistent(true); 
      WiFi.begin(ssid.c_str(), password.c_str());
      
      // Wait up to 15 seconds (30 * 500 ms) for a connection
      int counter = 0;
      while (WiFi.status() != WL_CONNECTED && counter < 30) {
         delay(500);
         Serial.print(".");
         counter++;
      }
      
      if (WiFi.status() == WL_CONNECTED) {
         Serial.println("\n[WiFi] Connected successfully!");
         Serial.print("[WiFi] IP address: ");
         Serial.println(WiFi.localIP());
         return; // Stop setup() here; the ESP32 is online

      } else {
         LOGWIFI("Failed to connect to the stored network.");
      }
    } else {
       LOGWIFI("No stored Wi-Fi credentials found.");
    }
  }
  // If there are no credentials or the connection fails, start the portal
  startCaptivePortal(); 
}

void handleStartCaptivePortal() {
  // During normal operation, also check whether the button is held for more than 2 seconds
    delay(2000); // Wait 2 seconds to avoid accidental presses
    #if defined(ARDUINO_ARCH_RP2040)
      if (BOOTSEL) {   // BOOTSEL is very slow down, not suitable for quick checks
    #endif
    #if defined(VIEWDATAEMULATOR)
      if (digitalRead(RESET_WIFI_BUTTON_PIN) == LOW) {
    #endif
      LOGWIFI("Physical reset triggered during runtime. Starting portal...");
      startCaptivePortal(); 

      deviceRestart();
    }
}

// ============================================================================
// Phone book handling
// ============================================================================
// Convert a normal digit string to BCD bytes
void stringToBcd(const char* src, uint8_t* dest, size_t destLen) {
  memset(dest, 0xFF, destLen); // Fill with 0xFF (F is the 'empty/end' marker)
  size_t len = strlen(src);
  for (size_t i = 0; i < len && i < (destLen * 2); i++) {
    uint8_t digit = (src[i] >= '0' && src[i] <= '9') ? (src[i] - '0') : 0;
    if (i % 2 == 0) {
      dest[i / 2] = (dest[i / 2] & 0x0F) | (digit << 4); // High nibble
    } else {
      dest[i / 2] = (dest[i / 2] & 0xF0) | digit;        // Low nibble
    }
  }
}

// Convert BCD bytes back to a normal string
void bcdToString(const uint8_t* src, char* dest, size_t destLen) {
  size_t outIdx = 0;
  for (size_t i = 0; i < (destLen - 1); i++) {
    // High nibble
    uint8_t high = (src[i >> 1] >> 4) & 0x0F;
    if (high == 0x0F) break;
    dest[outIdx++] = '0' + high;

    // Low nibble
    uint8_t low = src[i >> 1] & 0x0F;
    if (low == 0x0F) break;
    dest[outIdx++] = '0' + low;
    i++; // Increment because we process 2 digits per iteration
  }
  dest[outIdx] = '\0';
}

// Convert a character to a 6-bit value
uint8_t charTo6Bit(char c) {
  if (c >= 'A' && c <= 'Z') return c - 'A';          // 0 - 25
  if (c >= 'a' && c <= 'z') return (c - 'a') + 26;    // 26 - 51
  if (c >= '0' && c <= '9') return (c - '0') + 52;    // 52 - 61
  if (c == ' ') return 62;
  return 63;                                          // Punt '.' of fallback remaining characters
}

// Convert a 6-bit value back to a normal character
char bit6ToChar(uint8_t b) {
  if (b <= 25) return 'A' + b;
  if (b >= 26 && b <= 51) return 'a' + (b - 26);
  if (b >= 52 && b <= 61) return '0' + (b - 52);
  if (b == 62) return ' ';
  return '.';
}

// Pack a normal string into a 12-byte array (max 16 characters)
void packName6Bit(const char* src, uint8_t* dest) {
  memset(dest, 0xFF, MAX_COMPRESSED_NAME_LEN); 
  uint32_t buffer = 0;
  int bitsInBuffer = 0;
  int destIdx = 0;

  for (int i = 0; i < MAX_NAME_LEN; i++) {
    char c = src[i];
    uint8_t val = (c == '\0') ? 63 : charTo6Bit(c);

    buffer = (buffer << 6) | (val & 0x3F);
    bitsInBuffer += 6;

    while (bitsInBuffer >= 8) {
      bitsInBuffer -= 8;
      dest[destIdx++] = (buffer >> bitsInBuffer) & 0xFF;
    }
    if (c == '\0') break;
  }

  if (bitsInBuffer > 0 && destIdx < MAX_COMPRESSED_NAME_LEN) {
    buffer = buffer << (8 - bitsInBuffer); 
    dest[destIdx++] = buffer & 0xFF;
  }
}

// Unpack a 12-byte array back to a normal string (max 16 chars buffer)
void unpackName6Bit(const uint8_t* src, char* dest, size_t maxLen) {
  uint32_t buffer = 0;
  int bitsInBuffer = 0;
  int srcIdx = 0;
  int outIdx = 0;

  for (int i = 0; i < MAX_NAME_LEN && outIdx < maxLen; i++) {
    while (bitsInBuffer < 6 && srcIdx < MAX_COMPRESSED_NAME_LEN) {
      buffer = (buffer << 8) | src[srcIdx++];
      bitsInBuffer += 8;
    }

    if (bitsInBuffer >= 6) {
      bitsInBuffer -= 6;
      uint8_t val = (buffer >> bitsInBuffer) & 0x3F;
      
      // Stop on the stop-marker (63)
      if (val == 63) {
        break;
      }
      
      dest[outIdx++] = bit6ToChar(val);
    }
  }
  dest[outIdx] = '\0'; // Nette null-terminator aan het einde
}

void readPhoneBook() {
  memset(phonebook, 0, sizeof(phonebook)); // Clear the entire array in RAM

  LOGPB("Loading data from EEPROM...");
  EEPROM.begin(EEPROM_SIZE);

  CompressedPhoneItem compressedItems[MAX_PHONEBOOK_ITEMS];
  uint32_t storedMagic = 0;
  
  EEPROM.get(PHONE_ADDR, storedMagic);

  if (storedMagic == PHONE_MAGIC) {
    // Skip the magic number and read the compressed items
    EEPROM.get(PHONE_ADDR + 4, compressedItems);
    EEPROM.end();

    for (int i = 0; i < MAX_PHONEBOOK_ITEMS; i++) {
      // Unpack the name from 6-bit compressed format
      unpackName6Bit(compressedItems[i].packedName, phonebook[i].name, MAX_NAME_LEN);
      
      // Convert the BCD number to a string
      if (compressedItems[i].bcdNumber[0] == 0xAA) {
        strlcpy(phonebook[i].number, "---", sizeof(phonebook[i].number));
      } else {
        bcdToString(compressedItems[i].bcdNumber, phonebook[i].number, sizeof(phonebook[i].number));
      }
      
      // Compose the ServerType from the lower 3 bits of typeAndPrefix (bits 0-2)
      phonebook[i].type = static_cast<ServerType>(compressedItems[i].typeAndPrefix & 0x07);

      // Compose the full URL including the http(s) prefix based on the high 2 bits of typeAndPrefix
      String fullUrl = "";
      uint8_t prefix = (compressedItems[i].typeAndPrefix >> 6) & 0x03;
      if (prefix == 1) fullUrl += "http://";
      else if (prefix == 2) fullUrl += "https://";
      
      fullUrl += compressedItems[i].url;
      strlcpy(phonebook[i].url, fullUrl.c_str(), MAX_URL_LEN);
    }
    LOGPB("Loaded and decompressed 30 items successfully!");
  } else {
    LOGPB("No valid phone book data found in NVM. Loading defaults");
   
    memset(&phonebook, 0, sizeof(phonebook)); // clear the phonebook array in RAM
 
    #if defined(M2009EMULATOR)
    // Fill with default values
    strlcpy(phonebook[0].number, "001", sizeof(phonebook[0].number));
    strlcpy(phonebook[0].name, "Internal menu", sizeof(phonebook[0].name));
    strlcpy(phonebook[0].url, "localhost", sizeof(phonebook[0].url));
    phonebook[0].type = ServerType::INTERNALMENU;
    #endif
    #if defined(VIEWDATAEMULATOR)
    // Fill with default values
    strlcpy(phonebook[0].number, "040", sizeof(phonebook[0].number));
    strlcpy(phonebook[0].name, "Telstar CURRER", sizeof(phonebook[0].name));
    strlcpy(phonebook[0].url, "glasstty.com:6502", sizeof(phonebook[0].url));
    phonebook[0].type = ServerType::VIEWDATA;
    #endif
    
    strlcpy(phonebook[1].number, "041", sizeof(phonebook[0].number)); 
    strlcpy(phonebook[1].name, "Telstar ELLIS", sizeof(phonebook[0].name));
    strlcpy(phonebook[1].url, "http://glasstty.com:6503", sizeof(phonebook[0].url));
    phonebook[1].type = ServerType::VIEWDATA;
    
    strlcpy(phonebook[2].number, "042", sizeof(phonebook[0].number));
    strlcpy(phonebook[2].name, "The Gongpit BBS", sizeof(phonebook[0].name));
    strlcpy(phonebook[2].url, "http://bbs.gongpit.com:2323", sizeof(phonebook[0].url));
    phonebook[2].type = ServerType::VIEWDATA;
    
    strlcpy(phonebook[3].number, "050", sizeof(phonebook[0].number));
    strlcpy(phonebook[3].name, "Teletekstarchief", sizeof(phonebook[0].name));
    strlcpy(phonebook[3].url, "http://teletekstarchief.nl/json/", sizeof(phonebook[0].url));
    phonebook[3].type = ServerType::TELETEXT;
        
    strlcpy(phonebook[4].number, "051", sizeof(phonebook[0].number));
    strlcpy(phonebook[4].name, "NOS Teletekst", sizeof(phonebook[0].name));
    strlcpy(phonebook[4].url, "https://teletekst-data.nos.nl/json/", sizeof(phonebook[0].url));
    phonebook[4].type = ServerType::TELETEXT;

    strncpy(phonebook[5].number, "043", sizeof(phonebook[0].number));
    strncpy(phonebook[5].name, "MyViewDataSvr", sizeof(phonebook[0].name));
    strncpy(phonebook[5].url, "viewdata-server.local:8080", sizeof(phonebook[0].url));
    phonebook[5].type = ServerType::VIEWDATA;

#if defined(M2009EMULATOR)    
    strlcpy(phonebook[6].number, "---", sizeof(phonebook[0].number));
    strlcpy(phonebook[6].name, "P2000Tftp Server", sizeof(phonebook[0].name));
    strlcpy(phonebook[6].url, "p2000ftp.local:8082", sizeof(phonebook[0].url));
    phonebook[6].type = ServerType::P2000FTP;
#endif    

    strlcpy(phonebook[7].number, "044", sizeof(phonebook[0].number));
    strlcpy(phonebook[7].name, "NxTel", sizeof(phonebook[0].name));
    strlcpy(phonebook[7].url, "nx.nxtel.org:23280", sizeof(phonebook[0].url));
    phonebook[7].type = ServerType::VIEWDATA;

    strlcpy(phonebook[8].number, "045", sizeof(phonebook[0].number));
    strlcpy(phonebook[8].name, "Tetrachloromethane", sizeof(phonebook[0].name));
    strlcpy(phonebook[8].url, "fish.ccl4.org:23", sizeof(phonebook[0].url));
    phonebook[8].type = ServerType::VIEWDATA;

    strlcpy(phonebook[9].number, "046", sizeof(phonebook[0].number));
    strlcpy(phonebook[9].name, "EndofLine BBS", sizeof(phonebook[0].name));
    strlcpy(phonebook[9].url, "endofthelinebbs.com:6502", sizeof(phonebook[0].url));
    phonebook[9].type = ServerType::VIEWDATA;

    strlcpy(phonebook[10].number, "047", sizeof(phonebook[0].number));
    strlcpy(phonebook[10].name, "Night Qwl BBS", sizeof(phonebook[0].name));
    strlcpy(phonebook[10].url, "nightowlbbs.ddns.net:6400", sizeof(phonebook[0].url));
    phonebook[10].type = ServerType::VIEWDATA;

    strlcpy(phonebook[11].number, "048", sizeof(phonebook[0].number));
    strlcpy(phonebook[11].name, "Teletext.ca", sizeof(phonebook[0].name));
    strlcpy(phonebook[11].url, "minitel.teletext.ca:516", sizeof(phonebook[0].url));
    phonebook[11].type = ServerType::VIEWDATA;

    strlcpy(phonebook[12].number, "049", sizeof(phonebook[0].number));
    strlcpy(phonebook[12].name, "RetroCampus", sizeof(phonebook[0].name));
    strlcpy(phonebook[12].url, "bbs.retrocampus.com:1651", sizeof(phonebook[0].url));
    phonebook[12].type = ServerType::VIEWDATA;
  }

  // Show the filled-in records in the Serial Monitor
  for (int i = 0; i < MAX_PHONEBOOK_ITEMS; i++) {
    if (phonebook[i].name[0] == '\0' && phonebook[i].number[0] == '\0') continue;
    
    LOGPB("Item [%d] Naam: %s, Nr: %s, Type: %s, URL: %s", 
        i, phonebook[i].name, phonebook[i].number, ServertypeToStr(phonebook[i].type), phonebook[i].url);
  }
}

void handleSaveSettings() {
  Serial.println("[PHONEBOOK] Saving phone book data...");

  LOGPB("Updating with web form data...");
  // Loop through all entries in the HTML form and update the local array
  for (int i = 0; i < MAX_PHONEBOOK_ITEMS; i++) {
    if (server.hasArg("name_" + String(i))) {
      strlcpy(phonebook[i].name, server.arg("name_" + String(i)).c_str(), sizeof(phonebook[i].name));
    }
    if (server.hasArg("nr_" + String(i))) {
      strlcpy(phonebook[i].number, server.arg("nr_" + String(i)).c_str(), sizeof(phonebook[i].number));
    }
    if (server.hasArg("srv_" + String(i))) {
      strlcpy(phonebook[i].url, server.arg("srv_" + String(i)).c_str(), sizeof(phonebook[i].url));
    }
    if (server.hasArg("srv_type_" + String(i))) {
      phonebook[i].type = StrToServertype(server.arg("srv_type_" + String(i)).c_str());
    }
#ifdef M2009EMULATOR
    if (phonebook[i].type == ServerType::P2000FTP) {
      strlcpy(phonebook[i].number, "---", sizeof(phonebook[i].number));
    }
#endif
  }

  // Save the updated struct array to EEPROM in one operation
  storePhonebook();

  LOGPB("Phonebook data updated successfully via the web portal!");
  generateHtmlForm();
  
  // Redirect the user to the home page to view the changes
  server.sendHeader("Location", "/", true);
  server.send(303);
}

void storePhonebook() {
  LOGPB("Saving phonebook to EEPROM...");
  
  CompressedPhoneItem compressedItems[MAX_PHONEBOOK_ITEMS];
  memset(compressedItems, 0, sizeof(compressedItems));

  for (int i = 0; i < MAX_PHONEBOOK_ITEMS; i++) {
    // Pack the name into 6-bit representation
    packName6Bit(phonebook[i].name, compressedItems[i].packedName);
    
    // Pack the phone number into BCD format
    if (strcmp(phonebook[i].number, "---") == 0) {
      memset(compressedItems[i].bcdNumber, 0xFF, MAX_COMPRESSED_NUMBER_LEN);
      compressedItems[i].bcdNumber[0] = 0xAA;
    } else {
      stringToBcd(phonebook[i].number, compressedItems[i].bcdNumber, MAX_COMPRESSED_NUMBER_LEN);
    }
    
    // Determine the server type (bit 0-2)
    uint8_t typeByte = static_cast<uint8_t>(phonebook[i].type) & 0x07;
    
    // Determine the HTTP prefix and strip it from the URL
    String rawUrl = String(phonebook[i].url);
     
    // Make a lowercase copy of the URL for prefix detection
    String lowerUrl = rawUrl;
    lowerUrl.toLowerCase();

    uint8_t prefixBits = 0;
    int startIdx = 0;

    if (lowerUrl.startsWith("http://")) {
      prefixBits = 1;
      startIdx = 7;
    } else if (lowerUrl.startsWith("https://")) {
      prefixBits = 2;
      startIdx = 8;
    }
    
    typeByte |= (prefixBits << 6); 
    compressedItems[i].typeAndPrefix = typeByte;

    // Sla de kern van de URL op (maximaal 34 tekens)
    strlcpy(compressedItems[i].url, rawUrl.substring(startIdx).c_str(), 35);
  }

  // Schrijf naar EEPROM
  EEPROM.begin(EEPROM_SIZE);
  EEPROM.put(PHONE_ADDR, PHONE_MAGIC);
  EEPROM.put(PHONE_ADDR + 4, compressedItems);
  EEPROM.commit();
  EEPROM.end();

  // Show the filled-in records in the Serial Monitor
  for (int i = 0; i < MAX_PHONEBOOK_ITEMS; i++) {
    if (phonebook[i].name[0] == '\0' && phonebook[i].number[0] == '\0') continue;
    LOGPB("Item [%d] -> Naam: %s, Nr: %s, Type:%s, Server: %s", 
        i, phonebook[i].name, phonebook[i].number,  ServertypeToStr(phonebook[i].type), phonebook[i].url);
  }

  LOGPB("Phonebook saved successfully!");
}

PhoneNumberToSite *lookIntoPhoneBook(char *number) {
    for (int i = 0; i < MAX_PHONEBOOK_ITEMS; i++) {
        if (strcmp(phonebook[i].number, number) == 0) {
            LOGPB("Found matching phonebook entry for name: %s", phonebook[i].name);
            return &phonebook[i];
        }
    }
    
    // Return an null pointer if not found
    LOGPB("No matching phonebook entry found for %s", number);
    return nullptr;
}

#if defined(VIEWDATAEMULATOR)
// ============================================================================
// UART settings handling
// ============================================================================
void handleSaveUARTConfig() {

  Serial.println("[UART] Saving uart settings...");
  
  if (server.arg("baudrate")) {
    uint16_t baudrate = server.arg("baudrate").toInt();
    if (baudrate <= 56000) {
      uartConfig.baudrateDown = (baudrate == 75 ? 1200 : baudrate);  // handle 1200/75 special case
      uartConfig.baudrateUp = baudrate;
    }
  }

  String dataBits = server.arg("data_bits"); // 'dataBits8' or 'dataBits7'
  String stopBits = server.arg("stop_bits"); // 'stopBits1' or 'stopBits2'
  String parity = server.arg("parity");      // 'parityN', 'parityE' or 'parityO'

  uartConfig.dataBits8 = (dataBits == "dataBits8");
  uartConfig.stopBits1 = (stopBits == "stopBits1");
  if (parity == "parityE") {
    uartConfig.parity = 'E';
  } else if (parity == "parityO") {
    uartConfig.parity = 'O';
  } else {
    uartConfig.parity = 'N';
  }
  
  // Save the updated struct array to EEPROM in one operation
  storeUARTConfig();

  LOGPB("UART settings updated successfully via the web portal!");
  generateHtmlForm();
  
  // Redirect the user to the home page to view the changes
  server.sendHeader("Location", "/", true);
  server.send(303);
}

void storeUARTConfig() {
  LOGUART("Saving UART configuration to EEPROM...");
  EEPROM.begin(EEPROM_SIZE);
  UARTConfigBlock block;
  memset(&block, 0, sizeof(block));
  
  block.magic = UART_MAGIC;
  block.uart = uartConfig;
  
  EEPROM.put(UART_ADDR, block);
  EEPROM.commit();
  EEPROM.end();

  LOGUART("UART configuration saved successfully!");
}

void readUARTConfig() {
  LOGUART("Reading UART configuration from EEPROM...");
  EEPROM.begin(EEPROM_SIZE);
  UARTConfigBlock block;
  memset(&block, 0, sizeof(block));

  EEPROM.get(UART_ADDR, block);
  EEPROM.end();

  if (block.magic != UART_MAGIC) {
    LOGUART("No valid UART configuration found, using defaults.");
    memset(&block, 0, sizeof(block));
    uartConfig.baudrateUp   = 1200;
    uartConfig.baudrateDown = 1200;
    uartConfig.dataBits8    = true;
    uartConfig.stopBits1    = true;
    uartConfig.parity       = 'N';
  } else {
    uartConfig = block.uart;
  }

  LOGUART("UART configuration: BaudrateDown=%d, BaudrateUp=%d, DataBits=%d, Parity=%c, StopBits1=%d",
          uartConfig.baudrateDown,
          uartConfig.baudrateUp,
          uartConfig.dataBits8 ? 8 : 7,
          uartConfig.parity,
          uartConfig.stopBits1 ? 1 : 2);
}
#endif

#if defined(M2009EMULATOR)
void handleSaveUARTConfig() {

  Serial.println("[UART] Saving uart simulator settings...");
  
  if (server.arg("baudrate")) {
    uint16_t baudrate = server.arg("baudrate").toInt();
    if (baudrate <= 56000) {
      uartSimConfig.baudrateSim = baudrate;  // If 0 no baudrate simulator but full speed
    }
  }
  if (server.arg("socket_port")) {
    uint16_t port = server.arg("socket_port").toInt();
    uartSimConfig.port = (port < 65536) ? port : DEFAULT_HOSTPORT;
  }
  
  // Save the updated struct array to EEPROM in one operation
  storeUARTConfig();

  LOGUART("simulator settings updated successfully via the web portal!");
  generateHtmlForm();
  
  // Redirect the user to the home page to view the changes
  server.sendHeader("Location", "/", true);
  server.send(303);
}

void storeUARTConfig() {
  LOGUART("Saving UART Simulator configuration to EEPROM...");
  EEPROM.begin(EEPROM_SIZE);

  uartSimConfig.magic = UART_MAGIC;
  EEPROM.put(UART_ADDR, uartSimConfig);

  EEPROM.commit();
  EEPROM.end();

  LOGUART("UART Simulator configuration saved successfully!");
}

void readUARTConfig() {
  LOGUART("Reading UART Simulator configuration from EEPROM...");
  EEPROM.begin(EEPROM_SIZE);

  EEPROM.get(UART_ADDR, uartSimConfig);
  EEPROM.end();

  if (uartSimConfig.magic != UART_MAGIC) {
    LOGUART("No valid UART configuration found, using defaults.");
    uartSimConfig.baudrateSim = 0;
    uartSimConfig.port = DEFAULT_HOSTPORT;        // Default port if not specified
  }

  // Calculate the baudrate simulation delays for up and down directions based on the configured baudrate
  m_baudrateSimDelayUp = (uartSimConfig.baudrateSim != 0) ? 9000.0 / uartSimConfig.baudrateSim : 0;
  if (uartSimConfig.baudrateSim == 75) {
     m_baudrateSimDelayDown = 9000.0 / 1200;  // Special case for 1200/75 baud
  } else {
     m_baudrateSimDelayDown = (uartSimConfig.baudrateSim != 0) ? 9000.0 / uartSimConfig.baudrateSim : 0;
  }
  m_answerPort = uartSimConfig.port;

  LOGUART("UART Simulator configuration: BaudrateSim=%d, port=%d, updelay=%u downdelay=%u",
          uartSimConfig.baudrateSim,
          uartSimConfig.port,
          m_baudrateSimDelayUp,
          m_baudrateSimDelayDown);
}
#endif

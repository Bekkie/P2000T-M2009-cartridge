/*
  Miniware M2009 Autodial/Auto answer modem Cartridge for a P2000T
  Author: Bart Eversdijk
  
  Description: Miniware M2009 Autodial/Auto answer modem Cartridge for a P2000T
  Main program for handling the Miniware M2009 modem functions

  In Arduino IDE ->  Preferences  Additional boards manager URLs, 
  add https://github.com/earlephilhower/arduino-pico/releases/download/global/package_rp2040_index.json
  In Boards Manager, install Raspberry Pi Pico/RP2040 by Earle F. Philhower, III. 
  Remove the Arduino Mbed OS RP2024 one if it's installed.
  Select the board, for example Raspberry Pi Pico W under Raspberry Pi RP2040 Boards.

  Help:
  P2000T Reset (GP14) behavior:
  - Long reset (4-7 seconds) will restart the device.
  - Long reset (8-15 seconds) will start the captive portal.
  - Short reset will exit captive portal.

  Dial 001 for internal menu
  Connect to SSID: "M2009-Config-Portal" for modem/Wifi configuration

*/
#include <Arduino.h>
#include <stdarg.h>
#include <WiFi.h>
#include "hardware/pio.h"
#include "hardware/timer.h" 
#include "pico/multicore.h"
#include "pico/time.h"
#include "pico/util/queue.h"


#define ENABLE_BUS_DEBUG 0
#define ENABLE_LOG_DIAL 0
#include "config.h"

#include "z80_io.h"
#include "wifi-portal.h"
#include "network.h"

#if !(defined(M2009EMULATOR))
#error "M2009EMULATOR must be defined in config.h"
#endif

//**************************************************************************
//  Miniware M2009 Autodial/Auto answer modem Cartridge for a P2000T
//**************************************************************************

#if !defined(GIT_VERSION)
  #define GIT_VERSION "1.0"
#endif
String version = GIT_VERSION;
String author = "(c) Bart Eversdijk";

/**********************************************************************
    Ports:
      40-43 Z8630 - Z80SCC (Serial Communication Controller)
        40: cmd/status channel A  
        41: data in/out channel A --> Sending to outside world 75 Baud (or 1200 in host mode)
        42: cmd/status channel B
        43: data in/out channel B --> Receiving from outside world 1200 Baud (or 75 in host mode)
   
      De status registers (0x40 ch-A en 0x42 ch-B)
         bit 0: Rx char available
         bit 1: Zero count (not used)
         bit 2: Tx buffer empty
         bit 3: Data Carrier Detect (Valid host or client connection found)
         bit 4: SYNC Hunt (used when dialing and data carier detection)
         bit 5: Clear To Send
         bit 6: Tx underrun (EOM) (not used)
         bit 7: Break abort (not used)

  Channel A appears to be used for data carrier detection 

    44-47  Phone Line control (official just only port 44 but the M2009 ignores A0 en A1 so practical 0x44-0x47)
        bit 0-4: MC0-MC4 speed config (can be ignored)
        bit 5: Phone off hook (Line open)
        bit 6: Dial active (line short with resistor)
        bit 7: Dial pulse (line short)
**********************************************************************/
#define BIT(x,n) (((x)>>(n))&1)
// ============================================================================
// GLOBAL DECLARATIONS
// ============================================================================

static const int PIN_RD_INT = 13;  // /RD op GP13
static const int PIN_WR_INT = 12;  // /WR op GP12

// ============================================================================
// SCC REGISTERS, MODEM-LOGICA EN HOOFDPROGRAMMA
// ============================================================================
const uint8_t SCC_CHA_CMD = 0x40;
const uint8_t SCC_CHA_DATA = 0x41;
const uint8_t SCC_CHB_CMD = 0x42;
const uint8_t SCC_CHB_DATA = 0x43;
const uint8_t MODEM_PHONE_LINE_CTRL = 0x44;

uint8_t sccPointerA = 0; // Register currently selected for Channel A
uint8_t sccPointerB = 0; // Register currently selected for Channel B

// Internal SCC registers
// Arrays for all write registers (WR0 through WR15) per channel
uint8_t sccWriteRegsA[16] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
uint8_t sccWriteRegsB[16] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x3E, 0, 0, 0};
uint8_t *m_sccWriteRegsA_ptr = sccWriteRegsA;
uint8_t *m_sccWriteRegsB_ptr = sccWriteRegsB;

// Status registers (RR0) as read by the Z80 via IN
// Arrays for all read registers (RR0 through RR15) per channel
uint8_t sccReadRegsA[16] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
// RR12 of channel B reads 0x3E until the Z80 writes WR12: programs use OUT 0x42,0x0C / INP 0x42 to detect the M2009
uint8_t sccReadRegsB[16] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0x3E, 0, 0, 0};

#define M2009_PHONE_HOOK   5
#define M2009_DIALING      6
#define M2009_DIAL_PULSE   7

uint8_t m_port_44 = 0;

// Incoming call and ring state management
volatile bool m_incomingCall = false;  // True if a server is "calling"
bool ringIndicatorActive = false;
unsigned long lastRingTimestamp = 0;
volatile bool m_ringState = false;

// Dialing and data carrier state management
bool dialInProgress = false;
bool dataCarrierFound = false;
uint8_t numberCnt = 0;

int pulseCounter    = 0;
bool isCounting     = false;
//#define PHONE_NUMBER_SIZE 20
char m_phoneNumber[PHONE_NUMBER_SIZE+1] = "\0";

const static unsigned long DIALING_READY_DELAY = 2000;
const static unsigned long CARIER_DETECTION_DELAY = 3000;
static unsigned long carrierDueTime = 0;

unsigned long dialPulseTimerTimeout = 0;
unsigned long lastPulseTimestamp  = 0;
static repeating_timer_t syncTimer;
static bool syncTimerRunning = false;
// Toggles SCC Sync/Hunt (RR0 bit 4) at a fixed rate; runs in alarm IRQ context
uint8_t isrPtr[2] = {0, 0};  // register pointer per SCC channel (A, B) as seen on the bus

enum DialState {
    DIALSTATE_IDLE,
    DIALSTATE_LINE_AVAILABLE,
    DIALSTATE_DIALING,
    DIALSTATE_RINGING,
    DIALSTATE_CARRIER_WAIT,
    DIALSTATE_CARRIER_DETECTED,
    DIALSTATE_BUSY,
    DIALSTATE_WRONG_NUMBER,
    DIALSTATE_NO_LINE
};
DialState dialState = DialState::DIALSTATE_IDLE;
unsigned long lastDialStateTimeStamp  = 0;
bool tonePlaying = false;

enum Port47Commands {
    PORT47_CMD_SPEED_DIAL = 0x80,
    PORT47_CMD_START_DIAL = 0x81,
    PORT47_CMD_END_DIAL = 0x82
};

bool speedDialingStarted = false;
char m_speedDialUrl[MAX_SPEED_DIAL_LENGTH+1] = "\0";
volatile int m_speedDialUrlLength = 0;
volatile uint8_t m_speedDialIndex = -1;
volatile uint8_t m_speedDialserverType = -1;

#if ENABLE_BUS_TRACE
// Event and PIO stall reporting
uint32_t lastReportedEvOverflow = 0;
uint32_t lastReportedWritePioRxStallCount = 0;
unsigned long lastWrteRingDropReport = 0;
#endif

static const uint16_t RX_QUEUE_SIZE = 4096;
queue_t interCoreRxQueue;
static const uint16_t TX_QUEUE_SIZE = 4096;
queue_t interCoreTxQueue;

// Temporary storage for the currently received byte from internet
uint8_t m_newByte = 0;
bool m_byteWaiting = false;
#define RX_BUFFER_SIZE 256
uint8_t rxBufferB[RX_BUFFER_SIZE];
volatile uint16_t rxHeadB = 0, rxTailB = 0;

uint32_t modem_fifodata = 0;

// True while the byte at the front of queue is loaded in the PIO read cache
bool rxByteInCache = false;
volatile bool rxReadByIsr = false;
bool rxHavePending = false;  // byte taken from interCoreRxQueue, not yet in the PIO cache
uint8_t rxPendingByte = 0;

void updateRxCache();
void setDialState(DialState state);
void startDialPulseTimer(unsigned long delay);
void stopDialPulseTimer();
bool isDialPulseTimerActive();
bool isDialPulseTimerExpired();
static bool syncTimerCallback(repeating_timer_t *rt);
void stopSyncTimer();
void startSyncTimer(uint32_t hz);
void phone_on_hook();


// ============================================================================
// Input / output handling for the P2000T
// ============================================================================
// Send to the PIO read cache to the instruction memory (co-processor)
inline void outputP2000T(uint8_t port, uint8_t data) {
    uint8_t address = port & 0x07;
    uint32_t irqState = save_and_disable_interrupts();
    // Command ports are owned by the ISR until loop() has caught up with the event queue
    if ((address != 0 && address != 2) || busEvHead == busEvTail) {
        patchReadCache(address, data);
    }
    restore_interrupts(irqState);
}

// ----------------------------------------------------------------------------
// SCC REGISTERS, MANIPULATION (PORT 0x40-0x43)
// ----------------------------------------------------------------------------
void port_40_w(uint8_t data)
{
    static uint8_t last_port_40_data = 0;
    if (data != last_port_40_data) {
        LOGDBG("Port write 40: %02x", data);
        last_port_40_data = data;
    }
    
    if (sccPointerA == 0) {
        uint8_t regSelect = data & 0x0F;
            
        if (regSelect == 0) {
            // === COMMAND REGISTER 0 (WR0) DIRECT ACTIONS ===
            uint8_t cmd = (data >> 3) & 0x07;
            if (cmd == 0x02) { 
                // Reset User Interrupt / Error -> Set Tx Buffer Empty
                sccReadRegsA[0] |= 0x04; 

                if (m_activeAnswerConnection || m_activeDialoutConnection) {
                    sccReadRegsA[0] |= 0x08; // Set DCD (bit 3)
                    sccReadRegsA[0] |= 0x20; // Set CTS (bit 5)
                }
            }
        } else {
            // Select the target register index for the NEXT write/read cycle
            sccPointerA = regSelect;
            
        }
    } else {
        // === WRITE TO SELECTED REGISTER ===
        sccWriteRegsA[sccPointerA] = data;
        
        switch (sccPointerA) {
            case 2:
            case 12:
            case 13:
                // Handle special actions for WR2/WR12/WR13 --> mirror values in read register
                sccReadRegsA[sccPointerA] = data;
                break;
            case 9:
                {   // HARDWARE INIT: Catch Master Interrupt Control (WR9) Software Resets
                    uint8_t resetCmd = data & 0xC0; // Check bits 7-6
                    if (resetCmd == 0xC0) {
                        // Force total Hardware Reset: clear all configuration memory blocks
                        memset(sccWriteRegsA, 0, sizeof(sccWriteRegsA));
                        memset(sccWriteRegsB, 0, sizeof(sccWriteRegsB));
                        sccReadRegsA[0] = 0x04; // Keep Tx Buffer Empty active
                        sccReadRegsB[0] = 0x04;
                        outputP2000T(SCC_CHA_CMD, sccReadRegsA[0]);
                        outputP2000T(SCC_CHB_CMD, sccReadRegsB[0]);
                        LOG("[SCC Emulator] Master Hardware Reset Executed via WR9!");
                    }
                }
                break;
            default:
                break;
        }
        LOGDBG("[SCC Port 0x40] Register Wrote %d set to: 0x%02X", sccPointerA, data);
        sccPointerA = 0; // Automatically reset pointer back to WR0/RR0
    }
    // The Z80 reads from the PIO cache, so expose RR<n> there for the next read
    outputP2000T(SCC_CHA_CMD, sccReadRegsA[sccPointerA]);
}

void port_40_r()
{
    // If buffer was to full, but now has space, set the "Buffer empty" flag accordingly
    if ((sccReadRegsA[0] & ~0x04) && !queue_is_full(&interCoreTxQueue)) {
        sccReadRegsA[0] |= 0x04; // Set Buffer empty (bit 2 = 1)
    }
    if (sccPointerA != 0) {
        sccPointerA = 0; // Pointers always drop back to RR0 after any read access
        outputP2000T(SCC_CHA_CMD, sccReadRegsA[0]);
        LOGDBG("[SCC Port 0x40] Register Read %d, resetting pointer to 0", sccPointerA);
    }
}

void port_41_w(uint8_t data)
{
    LOGDBG("Port write 41: %02x", data);
    
    // The P2000T wants to send a byte over the transmit channel (Channel A), process it
    if (!queue_try_add(&interCoreTxQueue, &data)) {
         LOG("[PICO] Inter-core TX queue full!");
        // Update the status port (0x40) for hardware polling:
        // Set "Buffer full" (bit 2 = 0)
        sccReadRegsA[0] &= ~0x04; 
        if (sccPointerA == 0) {
            outputP2000T(SCC_CHA_CMD, sccReadRegsA[0]);
        }
    }
    // Set "Buffer empty" (bit 2 = 1)
    sccReadRegsA[0] |= 0x04; // Buffer empty RTS
    LOGDBG("Port write 41: %02x", data);
}

void port_41_r()
{
    updateRxCache();
    LOGDBG("Port read 41");
}

void port_42_w(uint8_t data)
{
    static uint8_t last_port_40_data = 0;
    if (data != last_port_40_data) {
        LOGDBG("Port write 42: %02x", data);
        last_port_40_data = data;
    }
    
    if (sccPointerB == 0) {
        uint8_t regSelect = data & 0x0F;
            
        if (regSelect == 0) {
            // === COMMAND REGISTER 0 (WR0) DIRECT ACTIONS ===
            uint8_t cmd = (data >> 3) & 0x07;
            if (cmd == 0x02) { 
                // Reset User Interrupt / Error -> Set Tx Buffer Empty
                sccReadRegsB[0] |= 0x04;
            }
        } else {
            // Select the target register index for the NEXT write/read cycle
            sccPointerB = regSelect;
        }
    } else {
        sccWriteRegsB[sccPointerB] = data;

        switch (sccPointerB) {
            case 2:
            case 12:
            case 13:
                // Handle special actions for WR2/WR12/WR13 --> mirror values in read register
                sccReadRegsB[sccPointerB] = data;
                break;
            case 9:
                {// HARDWARE INIT: Catch Master Interrupt Control (WR9) Software Resets
                    uint8_t resetCmd = data & 0xC0; // Check bits 7-6
                    if (resetCmd == 0xC0) {
                        // Force total Hardware Reset: clear all configuration memory blocks
                        memset(sccWriteRegsA, 0, sizeof(sccWriteRegsA));
                        memset(sccWriteRegsB, 0, sizeof(sccWriteRegsB));
                        sccReadRegsA[0] = 0x04; // Keep Tx Buffer Empty active
                        sccReadRegsB[0] = 0x04;
                        outputP2000T(SCC_CHA_CMD, sccReadRegsA[0]);
                        outputP2000T(SCC_CHB_CMD, sccReadRegsB[0]);
                        LOGDBG("[SCC Emulator] Master Hardware Reset Executed via WR9!");
                    }
                }
                break;
            default:
                break;
        }
        LOGDBG("[SCC Port 0x42] Register Wrote %d set to: 0x%02X", sccPointerB, data);
        sccPointerB = 0; // Automatically reset pointer back to WR0/RR0
    }
    outputP2000T(SCC_CHB_CMD, sccReadRegsB[sccPointerB]);
}

void port_42_r()
{
    // If buffer was to full, but now has space, set the "Buffer empty" flag accordingly
    if ((sccReadRegsB[0] & ~0x04) && !queue_is_full(&interCoreTxQueue)) {
        sccReadRegsB[0] |= 0x04; // Set Buffer empty (bit 2 = 1)
    }

    if (sccPointerB != 0) {
        sccPointerB = 0; // Pointers always drop back to RR0 after any read access
        outputP2000T(SCC_CHB_CMD, sccReadRegsB[0]);

        LOGDBG("[SCC Port 0x42] Register Read %d, resetting pointer to 0", sccPointerB);
    }
}

void port_43_w(uint8_t data)
{
    if (!queue_try_add(&interCoreTxQueue, &data)) {
         LOG("[PICO] Inter-core TX queue full!");
        // Update the status port (0x42) for hardware polling:
        // Set "Buffer full" (bit 2 = 0), then "Buffer empty" (bit 2 = 1)
        sccReadRegsB[0] &= ~0x04;
        if (sccPointerB) {
            outputP2000T(SCC_CHB_CMD, sccReadRegsB[0]);
        }
    } 

    // Set "Buffer empty" (bit 2 = 1)
    sccReadRegsB[0] |= 0x04; // Buffer empty RTS
    LOGDBG("Port write 43: %02x", data);
}

void port_43_r()
{
    updateRxCache();
    LOGDBG("Port read 43");
}

// ============================================================================
// PROCESS THE PHONE LINE AND PULSE DECODING (Port 0x44 logic)
// ============================================================================
void port_44_w(uint8_t data)
{
    LOGDBG("Port write 44 %02x", data);
    LOGDIAL("[Modem] Hook: %s Dialing %s pulse is %s MCX is %02X", BIT(data, M2009_PHONE_HOOK) ? "off" : "on", BIT(data, M2009_DIALING) ? "yes" : "no", BIT(data, M2009_DIAL_PULSE) ? "short" : "open", data & 0x1F);
    
    if (BIT(data, M2009_PHONE_HOOK) || BIT(data, M2009_DIALING))
    {
        // First time off hook?
        if (!BIT(m_port_44, M2009_PHONE_HOOK) && !BIT(m_port_44, M2009_DIALING))
        {
            LOG("[Modem] -- Off hook --");
            setDialState(DialState::DIALSTATE_LINE_AVAILABLE);

            // SYNC: inject DCD (Bit 3) and Sync/Hunt (Bit 4) 
            sccReadRegsA[0] &= ~0x20; // Clear Ring Indicator (Bit 5)

            if (m_incomingCall) {
                LOG("[Modem] P2000T aswers call! Line activated.");
                m_incomingCall = false;
                ringIndicatorActive = false;
                m_activeAnswerConnection = true;
                
                // Immediately set the carrier (DCD) high so the Z80 knows the connection is established
                sccReadRegsA[0] |= 0x08; 
                sccReadRegsB[0] |= 0x08; 
                
                dataCarrierFound = true;
                
                // Stop Sync/Hunt (Bit 4)
                setDialState(DialState::DIALSTATE_IDLE);
            } else // Normally self-initiated outgoing call (Originate Mode)
            {
                sccReadRegsA[0] |= 0x10;  // Set Sync/Hunt (Bit 4) -> Handshake pulse
        
                // Sync Channel B status as well code can checks both channels
                sccReadRegsB[0] &= ~0x10;

                dialInProgress = false;
                numberCnt = 0;
                m_phoneNumber[numberCnt] = '-';
                m_phoneNumber[numberCnt + 1] = 0;
            }
        }

        // Dialing in progress?
        if (BIT(data, M2009_DIALING))
        {
            // Start Dialing ?
            if (!BIT(m_port_44, M2009_DIALING)) {
                // Start of dial process first pulse
                if (!dialInProgress) {
                    LOG("[Modem] Start dialing new number");
                    dialInProgress = true;
                    sccReadRegsA[0] &= ~0x10; // Clear Sync/Hunt (Bit 4) -> Stop Handshake pulse
                    setDialState(DialState::DIALSTATE_DIALING);
                } else  {
                    LOG("[Modem] Start dialing next digit");
                    numberCnt++;
                }
                m_phoneNumber[numberCnt % PHONE_NUMBER_SIZE] = '-';
                m_phoneNumber[(numberCnt % PHONE_NUMBER_SIZE) + 1] = 0;
            }

            // Rising edge of dial pulse
            if (BIT(data, M2009_DIAL_PULSE) && !BIT(m_port_44, M2009_DIAL_PULSE)) {
                if (m_phoneNumber[numberCnt % PHONE_NUMBER_SIZE] == '-') {
                    m_phoneNumber[numberCnt % PHONE_NUMBER_SIZE] = '1';
                } else {
                    m_phoneNumber[numberCnt % PHONE_NUMBER_SIZE]++;
                    if (m_phoneNumber[numberCnt % PHONE_NUMBER_SIZE] > '9') {
                        // Ten pusles makes a zero (prevent overflow > 10 ==> '#')
                        m_phoneNumber[numberCnt % PHONE_NUMBER_SIZE] =
                            (m_phoneNumber[numberCnt % PHONE_NUMBER_SIZE] == '0' ? '#' : '0');
                    }
                }
            }
        } else {
            if (dialInProgress) {
                LOG("[Modem] Number dialed so far %s", m_phoneNumber);
            }
        }
        // m_dial_pulse_timer->adjust(attotime::from_msec(DIALING_READY_DELAY));
        startDialPulseTimer(DIALING_READY_DELAY); 
    } else {
        // Hook was off now on --> so reset all dial/connections activities
        if (BIT(m_port_44, M2009_PHONE_HOOK)) {
            phone_on_hook();
        }
    }
    m_port_44 = data;
}

void port_44_r()
{
    // For P2000T readonly, but just return last written byte
    // TODO: check what real M2009 hardware returns for port 0x44
    LOGDBG("Port read 44 %02x", m_port_44);
}

void port_47_w(uint8_t data)
{
    LOGDIAL("[Modem] Special Port 47 write byte  %02x", data);
    if (BIT(m_port_44, M2009_PHONE_HOOK))
    {
        if (m_speedDialUrlLength == 0) {
            if ((data & 0xE0) == Port47Commands::PORT47_CMD_SPEED_DIAL) {
                m_speedDialIndex = data & 0x1F;
                LOG("[Modem] Speed dial: #%d", m_speedDialIndex);
                sendFifoCommand(FifoCommand::SPEED_DIAL_NUMBER);
            }
            if ((data & 0xE0) == Port47Commands::PORT47_CMD_START_DIAL) {
                speedDialingStarted = true;
                m_speedDialserverType = (data & 0x1F) - 1;
                LOG("[Modem] Speed dial started with server type: %d\n", m_speedDialserverType);
            }
        }
        if (speedDialingStarted) {
            LOG("[Modem] Speed dial started\n");
            if (strchr("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-._~:/?@&'+=%", data)) {
                if (m_speedDialUrlLength < MAX_SPEED_DIAL_LENGTH)
                {
                    m_speedDialUrl[m_speedDialUrlLength] = data;
                    m_speedDialUrlLength++;
                    m_speedDialUrl[m_speedDialUrlLength] = '\0'; // Null-terminate the string
                }
            } else if ((data & 0xE0) == Port47Commands::PORT47_CMD_END_DIAL) { 
                speedDialingStarted = false;
                LOG("[Modem] Speed dial ended with number: %s\n", m_speedDialUrl);
                sendFifoCommand(FifoCommand::SPEED_DIAL_URL);
            } else {
                LOG("[Modem] Invalid character for speed dial: %c\n", data);
            }
        }
    } else {
        LOG("[Modem] Cannot speed dial, phone is on hook\n");
    }
}

// ----------------------------------------------------------------------------
// DIAL RELAIS
// ----------------------------------------------------------------------------
void phone_on_hook()
{
    // reset all dial/connections activities
    LOG("[modem] -- On hook --");

    // Stop dial process
    stopDialPulseTimer();
    
    setDialState(DialState::DIALSTATE_IDLE);
    
    numberCnt = 0;
    dialInProgress = false;
    dataCarrierFound = false;
    m_incomingCall = false;
    ringIndicatorActive = false;

    m_speedDialUrl[0] = '\0';
    m_speedDialUrlLength = 0;
    speedDialingStarted = false;

    // Empty the inter-core communication queues to prevent stale data from lingering
    uint8_t dummyByte;
    while (queue_try_remove(&interCoreRxQueue, &dummyByte)) {
    }
    while (queue_try_remove(&interCoreTxQueue, &dummyByte)) {
    }
    rxByteInCache = false;
    rxHavePending = false; // Stop Sync/Hunt (Bit 4)

    // Drop Carrier (DCD), Sync and Ring flags safely
    sccReadRegsA[0] &= ~0x08; // Clear DCD (Bit 3)
    sccReadRegsA[0] &= ~0x10; // Clear Sync (Bit 4)
    sccReadRegsA[0] &= ~0x20; // Clear CTS/RI (Bit 5)
    
    sccReadRegsB[0] &= ~0x08; // Clear DCD (Bit 3)
    sccReadRegsB[0] &= ~0x10; // Clear Sync (Bit 4)
    sccReadRegsB[0] &= ~0x20; // Clear CTS/RI (Bit 5)

    // Notify the core 1 that the phone is now on hook
    sendFifoCommand(FifoCommand::PHONE_ON_HOOK);
}

void handleConnectionEstablished()
{
    // Stop dial tone and sync-timer
    setDialState(DialState::DIALSTATE_IDLE);
    
    if (m_activeDialoutConnection) {
        // We activate DCD (Bit 3 = 0x08) AND CTS (Bit 5 = 0x20) on CHANNEL A!
        sccReadRegsA[0] |= 0x08;  // Set DCD Kanaal A
        sccReadRegsA[0] |= 0x20;  // Set CTS Kanaal A 

        // Channel B carries the receive data. The Terminal reads RR1 right after OUT 42,0x11 and
        // treats RR1 bits 4-6 as errors; if that read ever lands on RR0 (late pointer update)
        // a set CTS or sync bit shows up as a block. Keep both clear on B.
        sccReadRegsB[0] |= 0x08;  // Set DCD Kanaal B
        sccReadRegsB[0] |= 0x20;  // Set CTS Kanaal B
        
        uint32_t irqState = save_and_disable_interrupts();
        sccReadRegsB[0] &= ~0x30;
        sccReadRegsA[0] &= ~0x10;
        restore_interrupts(irqState);
        
        // Dialing is finished; without this the handler re-runs every loop and redials
        dialInProgress = false;
        stopDialPulseTimer();
        LOG("[Modem] Connection established");
    } else {
        LOG("[Modem] No Carrier found, close connection");
    
        // Set DCD line low immediately and close the emulation socket properly
        phone_on_hook(); 
    }      
}

void handleConnectionLost()
{
     // CARRIER DETECT DROP: Set bit 3 to 0 immediately
    sccReadRegsA[0] &= ~0x01; // Clear Rx Character Available (no more data)
    sccReadRegsA[0] |= 0x04;  // Set Buffer empty (bit 2 = 1)
    sccReadRegsA[0] &= ~0x08; // Clear DCD (Bit 3)
    sccReadRegsA[0] &= ~0x10; // Clear CTS/Sync (Bit 4)
    sccReadRegsA[0] &= ~0x20; // Clear CTS/RI (Bit 5)
    
    sccReadRegsB[0] &= ~0x01; // Clear Rx Character Available (no more data)
    sccReadRegsB[0] |= 0x04;  // Set Buffer empty (bit 2 = 1)
    sccReadRegsB[0] &= ~0x08; // Clear DCD for Channel B as well!
    sccReadRegsB[0] &= ~0x10; // Clear CTS/Sync (Bit 4)
    sccReadRegsB[0] &= ~0x20; // Clear CTS/RI for Channel B
    

     // Update the P2000T hardware layers immediately for both channels
    outputP2000T(SCC_CHA_CMD, sccReadRegsA[0]); 
    outputP2000T(SCC_CHB_CMD, sccReadRegsB[0]); 

    ringIndicatorActive = false;
    if (m_incomingCall) {
        stopSyncTimer();
        sccReadRegsA[0] &= ~0x10;
        sccReadRegsB[0] &= ~0x10;
    }
    m_incomingCall = false;

    // Notify the core that we have disconnected
    LOG("[Modem] Core 1: we have been disconnected");
}

void handleIncomingCall()
{
    LOG("[PICO] Incoming call");
    ringIndicatorActive = true;
    lastRingTimestamp = millis();
    m_ringState = true;

    // Set bit 5 (CTS/ring indicator) HIGH in Channel A RR0 (0x40)
    sccReadRegsA[0] |= 0x20; // Ring Indicator (RI) high
    outputP2000T(SCC_CHA_CMD, sccReadRegsA[0]); 
    sccReadRegsB[0] |= 0x20; // Ring Indicator (RI) high
    outputP2000T(SCC_CHB_CMD, sccReadRegsB[0]); 

    m_incomingCall = true; // becomes an active connection when the P2000T goes off hook

    // M2009 needs to see that toggling before it goes off hook
    setDialState(DialState::DIALSTATE_LINE_AVAILABLE);
}


// ============================================================================
// Ring timer: 2 seconds on, 4 seconds off, like a real telephone
// ============================================================================
void handleRingTimer()
{
  if (ringIndicatorActive) {
    unsigned long elapsed = millis() - lastRingTimestamp;
    if (m_ringState && elapsed > 2000) {          // Stop ringing after 2 seconds
      m_ringState = false; 
      lastRingTimestamp = millis();
      sccReadRegsA[0] &= ~0x20;                 // Bit 5 (RI) LOW
      outputP2000T(SCC_CHA_CMD, sccReadRegsA[0]); 
      Serial.println("[Host mode] ... (silence between rings)");
    } 
    else if (!m_ringState && elapsed > 4000) {    // Ring again after 4 seconds of silence
      m_ringState = true; 
      lastRingTimestamp = millis();
      sccReadRegsA[0] |= 0x20;                 // Bit 5 (RI) HIGH
      outputP2000T(SCC_CHA_CMD, sccReadRegsA[0]);
      Serial.println("[Host mode] RING! RING! (bit 5 of 0x40 HIGH)");
    }
  }
}

void setDialState(DialState state)
{
    dialState = state;
    lastDialStateTimeStamp = millis();
    switch (dialState) {
        case DIALSTATE_IDLE:
            // Stop any tone
            stopSyncTimer();
            tonePlaying = false;
            LOGDIAL("DIALSTATE_IDLE: Stopped tone");
            break;
        case DIALSTATE_LINE_AVAILABLE:
            startSyncTimer(160); // 160 Hz continous Dial tone 
            tonePlaying = true;
            LOGDIAL("DIALSTATE_LINE_AVAILABLE: Started tone");
            break;

        case DIALSTATE_DIALING:
        case DIALSTATE_RINGING:
            // Start connecting tone  425 1 sec on, 4 secs off
            startSyncTimer(425);
            tonePlaying = true;
            LOGDIAL("DIALSTATE_RINGING: Started tone");
            break;
        case DIALSTATE_BUSY:
            // Start busy tone
            startSyncTimer(425); // 425 Hz 0.5 sec on, 0.5 secs off
            LOGDIAL("DIALSTATE_BUSY: Started tone");
            break;
        case DIALSTATE_WRONG_NUMBER:
            // Start wrong number tone
            startSyncTimer(950); // 950 Hz, 1400 Hz and 1800 Hz each 333mS followed by 1 sec silence
            tonePlaying = true;
            LOGDIAL("DIALSTATE_WRONG_NUMBER: Started tone");
            break;
        case DIALSTATE_NO_LINE:
            // Start no line tone
            startSyncTimer(425); // 425 Hz 0.25 sec on, 0.25 secs off
            tonePlaying = true;
            LOGDIAL("DIALSTATE_NO_LINE: Started tone");
            break;
    }
}

void handleDialState()
{
    unsigned long elapsed = millis() - lastDialStateTimeStamp;
    switch (dialState) {
        case DIALSTATE_IDLE:  // Silence, no tone
            if (tonePlaying) {
                stopSyncTimer();
                tonePlaying = false;
                LOGDIAL("DIALSTATE_IDLE: Stopped tone");
            }
            break;

        case DIALSTATE_RINGING: // 1 sec on, 4 secs off
            if (elapsed >= 1000 && elapsed < 2000) {
                // Stop the tone during the off period
                if (tonePlaying) { 
                    stopSyncTimer();
                    tonePlaying = false;
                    LOGDIAL("DIALSTATE_RINGING: Stopped tone");
                }
            } else if (elapsed >= 5000) {
                // Start the tone after the off period and restart the period
                if (!tonePlaying) { 
                    startSyncTimer(425);
                    tonePlaying = true;
                    LOGDIAL("DIALSTATE_RINGING: Started tone");
                }
                lastDialStateTimeStamp = millis();
            }
            break;

        case DIALSTATE_BUSY: // 0.5 sec on, 0.5 secs off
            if (elapsed >= 500 && elapsed < 700) {
                // Stop the tone during the off period
                if (tonePlaying) { 
                    stopSyncTimer();
                    tonePlaying = false;
                    LOGDIAL("DIALSTATE_BUSY: Stopped tone");
                }
            } else if (elapsed >= 1000) {
                // Start the tone after the off period and restart the period
                if (!tonePlaying) {
                    startSyncTimer(425);
                    tonePlaying = true;
                    LOGDIAL("DIALSTATE_BUSY: Started tone");
                }
                lastDialStateTimeStamp = millis();
            }
            break;

        case DIALSTATE_WRONG_NUMBER:
            {  // 950 Hz, 1400 Hz and 1800 Hz each 333mS followed by 1 sec silence
                static uint8_t toneIndex = 255; // 0: 950 Hz, 1: 1400 Hz, 2: 1800 Hz
                if (elapsed >= 333 && elapsed < 500) {
                    if (!tonePlaying || toneIndex != 1) {
                        startSyncTimer(1400);
                        toneIndex = 1;
                        tonePlaying = true;
                        LOGDIAL("DIALSTATE_WRONG_NUMBER: Started tone 2");
                    }
                } else if (elapsed >= 666 && elapsed < 800) {
                    if (!tonePlaying || toneIndex != 2) {
                        startSyncTimer(1800);
                        toneIndex = 2;
                        tonePlaying = true;
                        LOGDIAL("DIALSTATE_WRONG_NUMBER: Started tone 3");
                    }
                } else if ((elapsed >= 1000) && (elapsed < 2000)) {
                    if (tonePlaying) {
                        stopSyncTimer();
                        tonePlaying = false;
                        toneIndex = 255; // Reset tone index to an invalid value
                        LOGDIAL("DIALSTATE_WRONG_NUMBER: Stopped tone");
                    }
                } else if (elapsed >= 2000) {
                    if (!tonePlaying || toneIndex != 0) {
                        startSyncTimer(950);
                        toneIndex = 0;
                        tonePlaying = true;
                        LOGDIAL("DIALSTATE_WRONG_NUMBER: Started tone 1");
                    }
                    lastDialStateTimeStamp = millis();
                }
            }
            break;

        case DIALSTATE_NO_LINE:
            if (elapsed >= 250 && elapsed < 500) {
                if (tonePlaying) {
                    stopSyncTimer();
                    tonePlaying = false;
                    LOGDIAL("DIALSTATE_NO_LINE: Stopped tone");
                }
            } else if (elapsed > 500) {
                if (!tonePlaying) {
                    startSyncTimer(325);
                    tonePlaying = true;
                    LOGDIAL("DIALSTATE_NO_LINE: Started tone");
                }
                lastDialStateTimeStamp = millis();
            }
            break;
        default:
            break;
    }
}


// ============================================================================
// Check for unexpected disconnection on the internet side && dial timeout
// ============================================================================
uint32_t getSccBaudrate(uint8_t* writeRegs)
{
    uint16_t time_constant = (writeRegs[13] << 8) | writeRegs[12];
    uint8_t clock_mode = (writeRegs[4] >> 6) & 0x03;
    
    uint32_t multiplier = 16; 
    if (clock_mode == 0) multiplier = 1;
    if (clock_mode == 2) multiplier = 32;
    if (clock_mode == 3) multiplier = 64;

    uint32_t scc_pclk = 2457600; 
    if ((time_constant + 2) == 0) return 1200;

    uint32_t calculated_baud = scc_pclk / (2 * multiplier * (time_constant + 2));
    
    // Group to the correct modem speeds of the Am7910
    if (calculated_baud >= 60 && calculated_baud < 90)   return 75;   // V.23 75 baud retour channel
    if (calculated_baud > 200 && calculated_baud < 450)   return 300;  // V.21 / Bell 103
    
    return 1200; // V.23 Main 
}

void handleDialPulseTimer()
{
    // Evaluate the rotary dial total number timeout
    // If dialing and pulses more than 2000 ms have passed since the last pulse,
    // the total number is complete. Setup connection 

    if (!dialInProgress) {
        return;
    }
    
    if (!isDialPulseTimerExpired()) {
        return;
    }

    //if (!dataCarrierFound) {
    switch (dialState) {
        case DialState::DIALSTATE_LINE_AVAILABLE:
        case DialState::DIALSTATE_DIALING:
            LOG("[modem] Number dialed: %s", m_phoneNumber);

            sendFifoCommand(FifoCommand::NUMBER_DIALED);
            setDialState(DialState::DIALSTATE_RINGING);

            startDialPulseTimer(CARIER_DETECTION_DELAY);
            break;

        case DialState::DIALSTATE_CARRIER_WAIT:
            {   // Server found, generate carrier signal for P2000T
                uint32_t carrier_freq = 2100; // Standard CCITT V.23 1200 baud Space tone

                switch (getSccBaudrate(sccWriteRegsB))
                {
                    case 300:
                        // Terminal v4.8 runs at 300 baud (CCITT V.21)
                        // The V.21 Answer/Space frequency is exactly 1850 Hz!
                        carrier_freq = 1850;
                        LOG("[Modem] 300 baud detected. Sync-frequentie on 1850 Hz.");
                        break;
                    
                    case 75:
                        // CCITT V.23 Backward channel uses 390 Hz (Mark) / 450 Hz (Space)
                        carrier_freq = 450; 
                        LOG("[Modem] 75 baud detected. Sync-frequentie on 450 Hz.");
                        break;

                    default:
                    case 1200:
                        // V.23 Answer/Space use op 1200 baud (CCITT V.23)
                        carrier_freq = 2100;
                        LOG("[Modem] 1200 baud detected. Sync-frequentie on 2100 Hz.");
                        break;
                }

                // Stel de sync-timer in op 2x (3 x !!!?) de carrierfrequentie voor een correcte bit-toggle cyclus
                startSyncTimer(carrier_freq * 3);
                
                setDialState(DialState::DIALSTATE_CARRIER_DETECTED);
                dataCarrierFound = true;

                startDialPulseTimer(CARIER_DETECTION_DELAY);
            }   
            break;

        case DialState::DIALSTATE_CARRIER_DETECTED:
            if (m_activeDialoutConnection) {
                LOG("[Modem] Connected with: %s", m_phoneNumber);
                dataCarrierFound = false;
            
                handleConnectionEstablished();

            } else {
                LOG("[Modem] NO Carrier found, close connection");
            
                // Set DCD line low immediately and close the emulation socket properly
                phone_on_hook(); 
            }
            break;
    }
}

// ============================================================================
// Timers handling routines for dial pulse and SCC sync/hunt
// ============================================================================
void startDialPulseTimer(unsigned long delay)
{
    dialPulseTimerTimeout = millis() + delay;
}
void stopDialPulseTimer()
{
    dialPulseTimerTimeout = 0;
}
bool isDialPulseTimerActive()
{
    return (millis() < dialPulseTimerTimeout);
}
bool isDialPulseTimerExpired()
{
    return (millis() >= dialPulseTimerTimeout) && (dialPulseTimerTimeout != 0);
}

static bool syncTimerCallback(repeating_timer_t *rt)
{
    // The bus ISR (higher priority) also edits RR0 bit 0, so the read-modify-write must be atomic
    uint32_t irqState = save_and_disable_interrupts();
    sccReadRegsA[0] ^= 0x10;
    sccReadRegsB[0] ^= 0x10;
    // Patch by the bus-side pointer (isrPtr), not loop()'s: while Z80 reads keep loop() busy the
    // event queue is rarely empty, and a gated patch would hide the toggling from the Z80.
    if (isrPtr[0] == 0) patchReadCache(0, sccReadRegsA[0]);
    if (isrPtr[1] == 0) patchReadCache(2, sccReadRegsB[0]);
    restore_interrupts(irqState);
    return true; // keep repeating
}
void stopSyncTimer()
{
    if (syncTimerRunning) {
        cancel_repeating_timer(&syncTimer);
        syncTimerRunning = false;
    }
}
void startSyncTimer(uint32_t hz)
{
    stopSyncTimer();
    // Negative delay = fixed period between callback starts
    syncTimerRunning = add_repeating_timer_us(-(int64_t)(1000000UL / (hz * 2)), syncTimerCallback, nullptr, &syncTimer);
}


// Z80 reads are served from the PIO cache, so status and data must be pushed there proactively
void updateRxCache()
{
    // Take the byte from the (spinlock protected) queue outside the critical section below:
    // the bus ISR must never be held off for long. The Terminal does OUT 42,11 / IN 42 (RR1) back
    // to back; if the ISR is late the Z80 reads a stale RR0 instead, sees CTS/sync bits as RR1
    // error bits and shows the character as an error block (0x7F).
    if (!rxHavePending && !queue_is_empty(&interCoreRxQueue)) {

        static unsigned long lastRxTime = 0;
        if (millis() < (m_baudrateSimDelayDown + lastRxTime)) {
            return; // Limit RX handling to simulate baud rate delay
        }
        lastRxTime = millis();

        rxHavePending = queue_try_remove(&interCoreRxQueue, &rxPendingByte);
    }

    // Atomic with respect to the bus ISR, otherwise a data read in between could be overwritten
    // and the same byte would be offered to the Z80 twice
    uint32_t irqState = save_and_disable_interrupts();
    if (rxReadByIsr) {
        rxReadByIsr = false;
        rxByteInCache = false;
    }

    if (!rxByteInCache && rxHavePending) {
        patchReadCache(SCC_CHA_DATA & 0x07, rxPendingByte);
        patchReadCache(SCC_CHB_DATA & 0x07, rxPendingByte);
        rxHavePending = false;
        rxByteInCache = true;
    }

    if (rxByteInCache) {
        sccReadRegsA[0] |= 0x01;
        sccReadRegsB[0] |= 0x01;
    } else {
        sccReadRegsA[0] &= ~0x01;
        sccReadRegsB[0] &= ~0x01;
    }
    if (isrPtr[0] == 0) patchReadCache(0, sccReadRegsA[0]);
    if (isrPtr[1] == 0) patchReadCache(2, sccReadRegsB[0]);
    restore_interrupts(irqState);
}

void scc_setup()
{
    digitalWrite(PIN_LED_GREEN, LED_ON);
    
    for(int i = 0; i <= 10; i++) {
        pinMode(i, INPUT); 
    }

    pinMode(PIN_RD_INT, INPUT);    
    pinMode(PIN_WR_INT, INPUT);
    
    z80_io_init();

    // Init SCC registers and output initial command values
    sccReadRegsA[0] = 0x04;
    sccReadRegsB[0] = 0x04;
    outputP2000T(SCC_CHA_CMD, sccReadRegsA[0]);
    outputP2000T(SCC_CHB_CMD, sccReadRegsB[0]);

    queue_init(&interCoreRxQueue, sizeof(uint8_t),  RX_QUEUE_SIZE);
    queue_init(&interCoreTxQueue, sizeof(uint8_t),  TX_QUEUE_SIZE);

     // Setup complete, turn off the GREEN LED to indicate no errors
    digitalWrite(PIN_LED_GREEN, LED_OFF);   
}


// Setup SCC core
void setup() {
    logInit();

    // Temporary wait for ÙSB Serial to be ready, this is blocking
    // while (!Serial) {
    //     delay(10);
    // }
    // LOG("[Setup] Serial ready was blocking --- PLEASE REMOVE THESE 2 WAITS.");

    pinMode(LED_BUILTIN, OUTPUT);
    pinMode(PIN_LED_GREEN, OUTPUT);
    pinMode(PIN_LED_RED, OUTPUT);

    scc_setup();
}

// Setup serial communication and network core 
void setup1() {
    // Temporary wait for ÙSB Serial to be ready, this is blocking
    // while (!Serial) {
    //     delay(10);
    // }
    // LOG("[Setup1] Serial ready was blocking --- PLEASE REMOVE THESE 2 WAITS.");

     networkSetup();
     Serial.println("Raspberry Pico W M2009 Emulator Started!");

    // Turn on the GREEN LED to indicate the emulator is running
    digitalWrite(PIN_LED_GREEN, LED_ON);   
}

void loop1()
{
    networkLoop();
}

void loop() {
    // Spin on the bus for a while after the last access: Z80 accesses come in bursts and each
    // iteration of the housekeeping below is slow compared to an OUT followed by an INP.
    // The spin is capped: a terminal that polls the status port continuously would otherwise keep
    // us here forever and starve the dial timer, ring timer and network housekeeping below.
    uint32_t lastBusUs = time_us_32();
    const uint32_t spinStartUs = lastBusUs;
    do {
        busTraceNoteService();

        // Writes and reads arrive in true bus order: a register select (OUT 0x42,reg) is applied
        // before the INP that follows it, so the pointer model stays correct.
        while (busEvTail != busEvHead && time_us_32() - spinStartUs < 2000) {
            uint16_t ev = evRing[busEvTail & (EV_RING_SIZE - 1)];
            busEvTail = busEvTail + 1;
            lastBusUs = time_us_32();
            uint8_t z80_port = ((ev >> 8) & 0x07) | 0x40;
            if (ev & EV_READ_FLAG) {
                BUS_TRACE('R', z80_port, z80_read_shadow[z80_port & 0x07]);
                switch (z80_port) {
                    case 0x40: port_40_r(); break;
                    case 0x41: port_41_r(); break;
                    case 0x42: port_42_r(); break;
                    case 0x43: port_43_r(); break;
                    default: port_44_r(); break;
                }
                continue;
            }
            uint8_t z80_data = ev & 0xFF;
            BUS_TRACE('W', z80_port, z80_data);

            switch (z80_port) {
                case 0x40: port_40_w(z80_data); break;
                case 0x41: port_41_w(z80_data); break;
                case 0x42: port_42_w(z80_data); break;
                case 0x43: port_43_w(z80_data); break;

                // Strange ports 0x45-0x47 are not supported (but still handled)
                case 0x45:
                case 0x46:
                    LOGDBG("[PIO Info] %02X", z80_port);
                    /* FALL THROUGH to case 0x44 */
                case 0x44:
                    port_44_w(z80_data);  // processPhoneLine
                    break;
                case 0x47:
                    port_47_w(z80_data);
                    break;
            }
            LOGDBG("[PIO Info] Z80 wrote port 0x%02X with 0x%02X", z80_port, z80_data);
        }

        // Caught up: hand the ISR's pointer model and read cache back in sync with the emulation
        uint32_t irqState = save_and_disable_interrupts();
        if (busEvTail == busEvHead) {
            isrPtr[0] = sccPointerA;
            isrPtr[1] = sccPointerB;
            patchReadCache(0, sccReadRegsA[sccPointerA]);
            patchReadCache(2, sccReadRegsB[sccPointerB]);
        }
        restore_interrupts(irqState);
    } while (time_us_32() - lastBusUs < 20 && time_us_32() - spinStartUs < 200);

#if ENABLE_BUS_TRACE
    if (millis() - lastWriteRingDropReport >= 1000) {
        lastWriteRingDropReport = millis();
        checkWriteRxStall();
        uint32_t dropped = fastEvOverflow;
        if (dropped != lastReportedEvOverflow) {
            LOG("[PIO Warning] Dropped %lu Z80 bus events (loop() fell behind the ISR)",
                static_cast<unsigned long>(dropped));
            lastReportedEvOverflow = dropped;
        }
        uint32_t rxStalls = writePioRxStallCount;
        if (rxStalls != lastReportedWritePioRxStallCount) {
            LOG("[PIO Warning] Write RXSTALL observed %lu times (total)",
                static_cast<unsigned long>(rxStalls));
            lastReportedWritePioRxStallCount = rxStalls;
        }
    }
#endif

    if (rp2040.fifo.pop_nb(&modem_fifodata)) {
        // Handle command data 
        switch(modem_fifodata & 0xFF) {
            case FifoCommand::CONNECTION_LOST:
                handleConnectionLost();
                break;
            case FifoCommand::INCOMING_CALL:
                handleIncomingCall();
                break;

            case FifoCommand::NO_NETWORK_RESPONSE:
                setDialState(DialState::DIALSTATE_NO_LINE);
                break;
            case FifoCommand::WRONG_NUMBER:
                setDialState(DialState::DIALSTATE_WRONG_NUMBER);
                break;
            case FifoCommand::BUSY_LINE:
                setDialState(DialState::DIALSTATE_BUSY);
                break;
            case FifoCommand::SUCCESSFUL_CONNECTION:
                setDialState(DialState::DIALSTATE_CARRIER_WAIT);
                break;

            case FifoCommand::SPEED_DIAL_CONNECTED:
                handleConnectionEstablished();
                break;

            default:
                break;
        }
    }
    
    // Handle dial pulse timer for the modem
    handleDialPulseTimer();

    // Ring timer: 2 seconds on, 4 seconds off, like a real telephone
    handleRingTimer();

    // Handle response tone for the modem (e.g., busy signal, dial tone, etc.)
    handleDialState();

    // Keep the PIO read cache (status + data) in sync with the network side
    updateRxCache();

#if ENABLE_BUS_TRACE
     busTraceFlush();
#endif
}

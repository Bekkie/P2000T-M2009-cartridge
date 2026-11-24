/**
 * Network setup and communication for the Raspberry Pico W M2009 Emulator
 * Author: Bart Eversdijk
 * 
 * Contains declarations for inter-core communication, phone state, and SCC register pointers.
 */

#ifndef NETWORK_H
#define NETWORK_H

extern volatile bool m_activeAnswerConnection;      // Active anwer connection
extern volatile bool m_activeDialoutConnection;     // Active dialout connection
extern volatile bool m_incomingCall;

extern uint8_t *m_sccWriteRegsA_ptr;
extern uint8_t *m_sccWriteRegsB_ptr;

extern volatile bool bReadISR;
extern volatile uint8_t Z80ReadPort;

extern volatile bool bWriteISR;
extern volatile uint8_t Z80WriteData;
extern volatile uint8_t Z80WritePort;
#define PHONE_NUMBER_SIZE  20
extern char m_phoneNumber[PHONE_NUMBER_SIZE+1];

extern queue_t interCoreRxQueue;
extern queue_t interCoreTxQueue;

extern volatile bool bResetPending;
extern unsigned long m_baudrateSimDelayUp;
extern unsigned long m_baudrateSimDelayDown;
extern uint16_t m_answerPort;

#define MAX_SPEED_DIAL_LENGTH 128
extern char m_speedDialUrl[MAX_SPEED_DIAL_LENGTH+1];
extern volatile int m_speedDialUrlLength;
extern volatile uint8_t m_speedDialIndex;
extern volatile uint8_t m_speedDialserverType;

extern uint8_t m_port_44; // Port 44 state, belongs to core 0 ONLY USE fot reset purposes

enum FifoCommand : uint8_t {
    // Bit 0 needs to be set for indicating command
    PHONE_ON_HOOK = 1,
    NUMBER_DIALED = 2,
    CONNECTION_LOST = 3,
    INCOMING_CALL = 4,
    NO_NETWORK_RESPONSE = 5,
    WRONG_NUMBER = 6,
    BUSY_LINE = 7,
    SUCCESSFUL_CONNECTION = 8,
    SPEED_DIAL_NUMBER = 9,
    SPEED_DIAL_URL = 10,
    SPEED_DIAL_CONNECTED = 11
};

inline void sendFifoCommand(FifoCommand cmd)
{
    // Push the command to Core 1 write FIFO
    // Bit 7-0 of the outgoing data bit 8=0 indicates data byte
    // TODO: Handle the case when the FIFO push fails (succes == false)

    uint32_t fifoData = (1 << 8) | (static_cast<uint32_t>(cmd) & 0xFF);
    bool succes = rp2040.fifo.push_nb(uint32_t(fifoData)); // Non-blocking push
}

void networkSetup();
void networkLoop();
bool isRxQueueEmpty();
bool isRxQueueFull();
void clearRxQueue();

#endif // NETWORK_H
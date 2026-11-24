/*
  Z80 I/O Handling for Miniware M2009 Modem Cartridge
  Author: Bart Eversdijk
  
  Description: Handles Z80 bus read/write and output enable programs for the P2000T interface.
*/

#define ENABLE_BUS_TRACE 0  // RAM trace of Z80 bus accesses, printed when the bus is quiet (debug only)

#ifndef Z80_IO_H
#define Z80_IO_H

extern uint8_t sccReadRegsA[16];
extern uint8_t sccReadRegsB[16];
extern uint8_t *m_sccWriteRegsA_ptr;
extern uint8_t *m_sccWriteRegsB_ptr;
extern uint8_t isrPtr[2];
extern volatile bool rxReadByIsr;
#define EV_RING_SIZE 1024  // power of 2
#define EV_READ_FLAG 0x800
extern uint16_t evRing[EV_RING_SIZE];
extern volatile uint16_t busEvHead;
extern volatile uint16_t busEvTail;

void patchReadCache(uint8_t address, uint8_t data);
void z80_io_init();

#if ENABLE_BUS_TRACE
void busTraceFlush();
void busTraceNoteService();
void busTracePush(uint8_t kind, uint8_t port, uint8_t data);
#define BUS_TRACE(kind, port, data) busTracePush(kind, port, data)
#else
static inline void busTraceFlush() {}
static inline void busTraceNoteService() {}
#define BUS_TRACE(kind, port, data) ((void)0)
#endif

#endif // Z80_IO_H
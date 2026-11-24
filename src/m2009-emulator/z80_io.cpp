/**
 * Z80 I/O handling for the Miniware M2009 modem cartridge
 * Author: Bart Eversdijk
 */
#include <Arduino.h>
#include "hardware/dma.h"

#include "z80_io.h"
#include "config.h"

 // The PIO write FIFO is only 8 deep: a Z80 burst (OTIR) fills it in ~70us, shorter than the
// IRQ latency of WiFi/USB. A DMA channel moves samples into a software ring, independent of the CPU.
static const uint16_t WRITE_RING_SIZE = 512;
static const uint WRITE_RING_BITS = 11;  // 512 words * 4 bytes = 2048 bytes = 2^11
static volatile uint32_t writeRing[WRITE_RING_SIZE] __attribute__((aligned(2048)));
static int writeDmaChannel = -1;
static int writeDmaReloadChannel = -1;
static uint32_t writePioRxStallCount = 0;

// The DMA runs in blocks of WRITE_DMA_BLOCK transfers (a multiple of the ring size). When a block
// ends, a second channel reloads the count and restarts the first one, so it never stops.
// Producer/consumer positions are free-running counters, so a lap of the ring is detectable.
static const uint32_t WRITE_DMA_BLOCK = 1u << 24;
static uint32_t writeDmaReloadValue = WRITE_DMA_BLOCK;

// An INP can follow an OUT within ~4 us on the Z80, much faster than loop() can be relied on to
// respond (WiFi/framework work stalls it for up to milliseconds). Two ISRs therefore watch the
// bus: one per write sample (PIO1 IRQ) and one per completed read (PIO0 IRQ). They
//  - keep their own register-pointer model (isrPtr) and expose RR<n> in the PIO read cache at once,
//  - queue every event in true bus order, which loop() then processes with the full emulation.
// Reads and writes live in separate FIFOs, so without this queue their relative order is lost.
// An event is the 11-bit write sample (data | port << 8), or EV_READ_FLAG | port << 8 for a read.
// It carries its own payload, so loop() never has to match it against another counter.
uint16_t evRing[EV_RING_SIZE];
static volatile uint32_t fastEvOverflow = 0;
static uint32_t fastLastIdx = 0;    // next write-ring slot the ISR has not looked at yet

uint sm_write, sm_read, sm_oe;

static void startWriteDma()
{
    writeDmaChannel = dma_claim_unused_channel(true);
    writeDmaReloadChannel = dma_claim_unused_channel(true);

    dma_channel_config c = dma_channel_get_default_config(writeDmaChannel);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_32);
    channel_config_set_read_increment(&c, false);
    channel_config_set_write_increment(&c, true);
    channel_config_set_ring(&c, true, WRITE_RING_BITS);
    channel_config_set_dreq(&c, pio_get_dreq(pio1, sm_write, false));
    channel_config_set_chain_to(&c, writeDmaReloadChannel);
    dma_channel_configure(writeDmaChannel, &c, (void *)writeRing, &pio1->rxf[sm_write], WRITE_DMA_BLOCK, false);

    // Writing the block size to the trigger alias of the count register (al1_transfer_count_trig) restarts the channel;
    // the write address keeps running so the ring continues seamlessly.
    dma_channel_config r = dma_channel_get_default_config(writeDmaReloadChannel);
    channel_config_set_transfer_data_size(&r, DMA_SIZE_32);
    channel_config_set_read_increment(&r, false);
    channel_config_set_write_increment(&r, false);
    dma_channel_configure(writeDmaReloadChannel, &r,
                          &dma_hw->ch[writeDmaChannel].al1_transfer_count_trig,
                          &writeDmaReloadValue, 1, false);

    dma_channel_start(writeDmaChannel);
}

// Producer position is read straight from the DMA write address by the ISR (see z80WriteWork).
#if ENABLE_BUS_TRACE
struct BusTraceEntry {
    uint32_t us;     // time the Pico processed the access (not the bus time)
    uint8_t kind;    // 'R' = Z80 read, 'W' = Z80 write
    uint8_t port;
    uint8_t data;    // R: value that was in the PIO cache; W: value written
    uint8_t ptrB;    // channel B register pointer after processing
    uint8_t cacheB;  // channel B status cache after processing
    uint16_t gapUs;  // longest time between two bus-loop passes since the previous entry
};
static uint32_t busLastServiceUs = 0;
static uint32_t busMaxGapUs = 0;

void busTraceNoteService()
{
    uint32_t now = time_us_32();
    uint32_t gap = now - busLastServiceUs;
    busLastServiceUs = now;
    if (gap > busMaxGapUs) {
        busMaxGapUs = gap;
    }
}
static const uint16_t BUS_TRACE_SIZE = 256;
static BusTraceEntry busTrace[BUS_TRACE_SIZE];
static uint16_t busTraceHead = 0, busTraceTail = 0;
static uint32_t busTraceLost = 0;
static uint32_t busTraceLastUs = 0;

void busTracePush(uint8_t kind, uint8_t port, uint8_t data)
{
    busTraceLastUs = time_us_32();
    uint16_t next = (busTraceHead + 1) % BUS_TRACE_SIZE;
    if (next == busTraceTail) {
        ++busTraceLost;
        return;
    }
    busTrace[busTraceHead] = { time_us_32(), kind, port, data, sccPointerB, z80_read_shadow[2],
                               (uint16_t)(busMaxGapUs > 65535 ? 65535 : busMaxGapUs) };
    busMaxGapUs = 0;
    busTraceHead = next;
}

// Print only when the bus has been quiet for 50 ms: Serial output blocks and would delay the Z80
void busTraceFlush()
{
    if (time_us_32() - busTraceLastUs < 50000) {
        return;
    }
    for (int n = 0; n < 8 && busTraceTail != busTraceHead; ++n) {
        const BusTraceEntry &e = busTrace[busTraceTail];
        Serial.printf("T%lu %c %02X %02X ptrB=%u cacheB=%02X gap=%u\n",
                      (unsigned long)e.us, e.kind, e.port, e.data, e.ptrB, e.cacheB, e.gapUs);
        busTraceTail = (busTraceTail + 1) % BUS_TRACE_SIZE;
    }
    if (busTraceLost) {
        Serial.printf("[TRACE] %lu entries lost\n", (unsigned long)busTraceLost);
        busTraceLost = 0;
    }
}
#endif

static void checkWriteRxStall()
{
    const uint32_t rxStallMask = 1u << (PIO_FDEBUG_RXSTALL_LSB + sm_write);
    if (pio1->fdebug & rxStallMask) {
        pio1->fdebug = rxStallMask;
        ++writePioRxStallCount;
    }
}

static inline void __not_in_flash_func(fastEvent)(uint16_t ev)
{
    if ((uint16_t)(busEvHead - busEvTail) >= EV_RING_SIZE) {
        ++fastEvOverflow;  // loop() is far behind; it resyncs from the SCC model once it catches up
        return;
    }
    evRing[busEvHead & (EV_RING_SIZE - 1)] = ev;
    asm volatile("" ::: "memory");
    busEvHead = busEvHead + 1;
}

// Pointer model shared by write and read events: returns the log reason
static inline char __not_in_flash_func(fastWriteEvent)(uint8_t port, uint8_t data)
{
    if (port != 0 && port != 2) {
        return 'W';
    }
    uint8_t ch = port >> 1;
    const uint8_t *regs = ch ? sccReadRegsB : sccReadRegsA;
    if (isrPtr[ch] == 0) {
        uint8_t sel = data & 0x0F;
        if (sel == 0) {
            return 'Z';
        }
        isrPtr[ch] = sel;
        patchReadCache(port, regs[sel]);
        return 'A';
    }
    isrPtr[ch] = 0;  // data write to the selected register: pointer drops back to RR0
    patchReadCache(port, regs[0]);
    return 'D';
}

// reasons (writes): A=register selected, Z=WR0 command, D=data write (pointer reset), W=other port,
// N=sample not in the DMA ring yet; (reads): r=pointer reset, q=no change
// Everything reachable from here must live in RAM and use raw registers: a call into flash that
// misses the XIP cache (core 1 WiFi code keeps evicting it) costs more than the OUT->INP gap.
static void __not_in_flash_func(z80WriteWork)()
{
    pio1->irq = 1u;

    // Give the DMA a moment to store the sample in the ring
    const uint32_t base = (uint32_t)(uintptr_t)writeRing;
    uint32_t idx = ((dma_hw->ch[writeDmaChannel].write_addr - base) >> 2) & (WRITE_RING_SIZE - 1);
    for (int i = 0; i < 50 && idx == fastLastIdx; ++i) {
        idx = ((dma_hw->ch[writeDmaChannel].write_addr - base) >> 2) & (WRITE_RING_SIZE - 1);
    }
    if (idx == fastLastIdx) {
        return;
    }
    while (fastLastIdx != idx) {
        uint32_t sample = writeRing[fastLastIdx];
        fastLastIdx = (fastLastIdx + 1) & (WRITE_RING_SIZE - 1);
        fastEvent(sample & 0x7FF);
        fastWriteEvent((sample >> 8) & 0x07, sample & 0xFF);
    }
}

static void __not_in_flash_func(z80ReadWork)()
{
    pio0->irq = 1u;
    while (!(pio0->fstat & (1u << (PIO_FSTAT_RXEMPTY_LSB + sm_read)))) {
        uint8_t port = (pio0->rxf[sm_read] >> 1) & 0x07;
        // A status read with the pointer already at RR0 changes nothing for loop(); skip it so
        // a polling Z80 cannot flood the event ring and push out real writes (e.g. OUT 44 off hook)
        if (!((port == 0 || port == 2) && isrPtr[port >> 1] == 0)) {
            fastEvent(EV_READ_FLAG | ((uint16_t)port << 8));
        }
        if (port == 1 || port == 3) {
            // Data port read: drop "Rx character available" at once. loop() only refills the
            // cache after it gets to the event, and a fast polling Z80 would read the same byte twice.
            sccReadRegsA[0] &= ~0x01;
            sccReadRegsB[0] &= ~0x01;
            rxReadByIsr = true;
            if (isrPtr[0] == 0) patchReadCache(0, sccReadRegsA[0]);
            if (isrPtr[1] == 0) patchReadCache(2, sccReadRegsB[0]);
        }
        if ((port == 0 || port == 2) && isrPtr[port >> 1] != 0) {
            isrPtr[port >> 1] = 0;  // any read drops the pointer back to RR0
            patchReadCache(port, (port == 2) ? sccReadRegsB[0] : sccReadRegsA[0]);
        }
    }
}

// Single handler for both PIO IRQs: writes first, because when both are pending the write
// happened earlier (the INP that follows an OUT completes after it)
static void __not_in_flash_func(z80BusIrq)()
{
    if (pio1->irq & 1u) {
        z80WriteWork();
    }
    if (pio0->irq & 1u) {
        z80ReadWork();
    }
}


// PIO0 holds the read response table and read loop. PIO1 only samples inputs.
static const uint PIO_READ_TABLE_ADDR = 0;
static const uint PIO_READ_MAIN_ADDR = 16;
static const uint PIO_READ_COMMON_ADDR = 21;  // jmp-pin check, then the shared output tail
static const uint PIO_READ_PROGRAM_LENGTH = 32;
static const uint PIO_WRITE_PROGRAM_OFFSET = 0;
static const uint PIO_WRITE_PROGRAM_LENGTH = 5;
static const uint PIO_OE_PROGRAM_OFFSET = PIO_WRITE_PROGRAM_LENGTH;
static const uint PIO_OE_PROGRAM_LENGTH = 10;

// Internal flag line: driven by a PIO1 helper SM, sampled by the PIO0 read SM with `jmp pin`.
// Nothing needs to be wired to it, but the GPIO must be free.
static const uint PIN_FASTSEL = 20;
static const uint PIO_FLAG_PROGRAM_OFFSET = PIO_OE_PROGRAM_OFFSET + PIO_OE_PROGRAM_LENGTH;
static const uint PIO_FLAG_PROGRAM_LENGTH = 11;
static const uint PIO_READ_OVERRIDE_ADDR = 29;
static const uint8_t FASTSEL_VALUE = 0x3E;  // RR12 mirror, the answer to the modem probe OUT 0C / IN

static_assert(PIO_READ_PROGRAM_LENGTH <= 32 && PIO_WRITE_PROGRAM_LENGTH <= 32,
              "PIO instruction memory layout exceeds 32 instructions");

static uint16_t z80_bus_read_instructions[PIO_READ_PROGRAM_LENGTH];
static uint16_t z80_bus_write_instructions[PIO_WRITE_PROGRAM_LENGTH];
static uint16_t z80_bus_oe_instructions[PIO_OE_PROGRAM_LENGTH];
static uint16_t z80_bus_flag_instructions[PIO_FLAG_PROGRAM_LENGTH];
static uint sm_flag = 0;
static bool flagSmRunning = false;

bool pioReadProgramLoaded = false;

static const struct pio_program z80_bus_read_program = {
    .instructions = z80_bus_read_instructions,
    .length = PIO_READ_PROGRAM_LENGTH,
    .origin = -1,
};
static const struct pio_program z80_bus_write_program = {
    .instructions = z80_bus_write_instructions,
    .length = PIO_WRITE_PROGRAM_LENGTH,
    .origin = -1,
};
static const struct pio_program z80_bus_oe_program = {
    .instructions = z80_bus_oe_instructions,
    .length = PIO_OE_PROGRAM_LENGTH,
    .origin = -1,
};
static const struct pio_program z80_bus_flag_program = {
    .instructions = z80_bus_flag_instructions,
    .length = PIO_FLAG_PROGRAM_LENGTH,
    .origin = -1,
};
static inline uint16_t encode_read_byte_instruction(uint8_t value) {
    // SET drives GP0-4; sideset carries the upper three data bits on GP5-7.
    return pio_encode_set(pio_pins, value & 0x1F) |
           pio_encode_sideset_opt(3, value >> 5);
}

// Last value placed in the PIO read cache per port (for bus tracing)
volatile uint8_t z80_read_shadow[8];

// PIO instruction for every possible read value, so the ISR never has to call into flash
static uint16_t z80_instr_for_value[256];

// Ordered Z80 event queue (see sketch): the ISRs push, loop() pops. While events are queued,
// loop()'s idea of the register pointers is behind and it must not touch the read cache.
volatile uint16_t busEvHead = 0;
volatile uint16_t busEvTail = 0;

// Raw cache patch; callers guarantee exclusive access (ISR, or interrupts disabled)
void __not_in_flash_func(patchReadCache)(uint8_t address, uint8_t data) {
    uint16_t instr = z80_instr_for_value[data];
    z80_read_shadow[address] = data;
    z80_bus_read_instructions[address * 2] = instr;
    if (pioReadProgramLoaded) {
        pio0->instr_mem[address * 2] = instr;
    }
}

static void build_z80_bus_programs() {
    for (uint v = 0; v < 256; ++v) {
        z80_instr_for_value[v] = encode_read_byte_instruction(v);
    }
    for (uint address = 0; address < 8; ++address) {
        z80_bus_read_instructions[address * 2] =
            encode_read_byte_instruction(0);
        z80_bus_read_instructions[address * 2 + 1] =
            pio_encode_jmp(PIO_READ_COMMON_ADDR);
    }

    z80_bus_read_instructions[16] = pio_encode_wait_gpio(false, 13);
    z80_bus_read_instructions[17] = pio_encode_mov(pio_isr, pio_null);
    z80_bus_read_instructions[18] = pio_encode_in(pio_pins, 3);
    z80_bus_read_instructions[19] = pio_encode_in(pio_null, 1);
    z80_bus_read_instructions[20] = pio_encode_mov(pio_pc, pio_isr);
    // Common tail of every table entry. The port address is already in ISR (pushed below), so the
    // override can replace the data without losing which port was read.
    // Flag line high (OUT 0x42,0x0C just seen): drive 0x3E instead of the table value.
    z80_bus_read_instructions[21] = pio_encode_jmp_pin(PIO_READ_OVERRIDE_ADDR);
    z80_bus_read_instructions[22] = pio_encode_mov(pio_osr, pio_x);
    z80_bus_read_instructions[23] = pio_encode_out(pio_pindirs, 8);
    z80_bus_read_instructions[24] = pio_encode_wait_gpio(true, 13);
    z80_bus_read_instructions[25] = pio_encode_out(pio_pindirs, 8);
    z80_bus_read_instructions[26] = pio_encode_push(false, false);
    // Flag every completed read to the CPU (PIO0 IRQ flag 0) so the ISR can track register pointers
    z80_bus_read_instructions[27] = pio_encode_irq_set(false, 0);
    // The program wraps at the end, so the override below must never be reached by falling through
    z80_bus_read_instructions[28] = pio_encode_jmp(PIO_READ_MAIN_ADDR);
    z80_bus_read_instructions[PIO_READ_OVERRIDE_ADDR] = encode_read_byte_instruction(FASTSEL_VALUE);
    z80_bus_read_instructions[PIO_READ_OVERRIDE_ADDR + 1] = pio_encode_jmp(22);
    for (uint i = PIO_READ_OVERRIDE_ADDR + 2; i < PIO_READ_PROGRAM_LENGTH; ++i) {
        z80_bus_read_instructions[i] = pio_encode_jmp(PIO_READ_MAIN_ADDR);
    }

    // Flag helper: clear the flag on every write, set it when that write is OUT 0x42,0x0C and
    // hold it until the following read is over. Runs entirely in PIO, so the flag is up ~0.2 us
    // after /WR, long before the INP. Jump targets are relative to the load offset.
    z80_bus_flag_instructions[0] = pio_encode_wait_gpio(false, 12) | pio_encode_delay(20);
    z80_bus_flag_instructions[1] = pio_encode_set(pio_pins, 0);
    z80_bus_flag_instructions[2] = pio_encode_mov(pio_isr, pio_null);
    z80_bus_flag_instructions[3] = pio_encode_in(pio_pins, 11);
    z80_bus_flag_instructions[4] = pio_encode_mov(pio_x, pio_isr);
    z80_bus_flag_instructions[5] = pio_encode_jmp_x_ne_y(10);
    z80_bus_flag_instructions[6] = pio_encode_set(pio_pins, 1);
    z80_bus_flag_instructions[7] = pio_encode_wait_gpio(false, 13);
    z80_bus_flag_instructions[8] = pio_encode_wait_gpio(true, 13);
    z80_bus_flag_instructions[9] = pio_encode_set(pio_pins, 0);
    z80_bus_flag_instructions[10] = pio_encode_wait_gpio(true, 12);

    z80_bus_write_instructions[0] = pio_encode_wait_gpio(false, 12) |
                                     pio_encode_delay(20);
    z80_bus_write_instructions[1] = pio_encode_in(pio_pins, 11);
    // Flag the sample to the CPU (PIO1 IRQ flag 0) so a fast ISR can update the read cache
    z80_bus_write_instructions[2] = pio_encode_irq_set(false, 0);
    z80_bus_write_instructions[3] = pio_encode_wait_gpio(true, 12);
    z80_bus_write_instructions[4] = pio_encode_jmp(PIO_WRITE_PROGRAM_OFFSET);

    // OE hoog als /WR (GP12) of /RD (GP13) laag is, anders laag.
    // Input-pins starten op GP11: bit0=OE, bit1=/WR, bit2=/RD.
    // Sprongadressen zijn relatief: pio_add_program_at_offset telt de offset erbij op.
    z80_bus_oe_instructions[0] = pio_encode_mov(pio_osr, pio_pins);
    z80_bus_oe_instructions[1] = pio_encode_out(pio_null, 1);
    z80_bus_oe_instructions[2] = pio_encode_out(pio_x, 1);
    z80_bus_oe_instructions[3] = pio_encode_out(pio_y, 1);
    z80_bus_oe_instructions[4] = pio_encode_jmp_not_x(8);
    z80_bus_oe_instructions[5] = pio_encode_jmp_not_y(8);
    z80_bus_oe_instructions[6] = pio_encode_set(pio_pins, 0);
    z80_bus_oe_instructions[7] = pio_encode_jmp(0);
    z80_bus_oe_instructions[8] = pio_encode_set(pio_pins, 1);
    z80_bus_oe_instructions[9] = pio_encode_jmp(0);
}

static inline void init_z80_write_sm(PIO pio, uint sm, uint offset) {
    pio_sm_config c = pio_get_default_sm_config();
    sm_config_set_wrap(&c, offset, offset + PIO_WRITE_PROGRAM_LENGTH - 1);
    sm_config_set_in_pins(&c, 0);
    sm_config_set_in_shift(&c, false, true, 11);
    sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_RX); // 8-deep FIFO gives the drain timer more slack
    // Input-only PIO1 reads the pads without changing PIO0's GPIO output mux.
    pio_sm_init(pio, sm, offset, &c);
    pio_sm_set_enabled(pio, sm, true);
}

static inline void init_z80_oe_sm(PIO pio, uint sm, uint offset) {
    pio_sm_config c = pio_get_default_sm_config();
    sm_config_set_wrap(&c, offset, offset + PIO_OE_PROGRAM_LENGTH - 1);
    sm_config_set_set_pins(&c, PIN_BUS_OE, 1);
    sm_config_set_in_pins(&c, PIN_BUS_OE);
    pio_gpio_init(pio, PIN_BUS_OE);
    pio_sm_init(pio, sm, offset, &c);
    pio_sm_set_consecutive_pindirs(pio, sm, PIN_BUS_OE, 1, true);
    pio_sm_set_enabled(pio, sm, true);
}

static inline void init_z80_flag_sm(PIO pio, uint sm, uint offset) {
    pio_sm_config c = pio_get_default_sm_config();
    sm_config_set_wrap(&c, offset, offset + PIO_FLAG_PROGRAM_LENGTH - 1);
    sm_config_set_in_pins(&c, 0);
    sm_config_set_in_shift(&c, false, false, 32);
    sm_config_set_set_pins(&c, PIN_FASTSEL, 1);
    pio_gpio_init(pio, PIN_FASTSEL);
    pio_sm_init(pio, sm, offset, &c);
    pio_sm_set_consecutive_pindirs(pio, sm, PIN_FASTSEL, 1, true);
    // y = port 2 (A1 = bit 9 of the sample) with data 0x0C
    pio_sm_put_blocking(pio, sm, (2u << 8) | 0x0C);
    pio_sm_exec_wait_blocking(pio, sm, pio_encode_pull(false, true));
    pio_sm_exec_wait_blocking(pio, sm, pio_encode_mov(pio_y, pio_osr));
    pio_sm_exec_wait_blocking(pio, sm, pio_encode_set(pio_pins, 0));
    pio_sm_set_enabled(pio, sm, true);
}

static inline void init_z80_read_sm(PIO pio, uint sm, uint offset) {
    pio_sm_config c = pio_get_default_sm_config();
    sm_config_set_jmp_pin(&c, PIN_FASTSEL);
    sm_config_set_wrap(&c, offset + PIO_READ_MAIN_ADDR,
                       offset + PIO_READ_PROGRAM_LENGTH - 1);
    sm_config_set_out_pins(&c, 0, 8);
    sm_config_set_in_pins(&c, 8);
    sm_config_set_set_pins(&c, 0, 5);
    sm_config_set_sideset_pins(&c, 5);
    sm_config_set_sideset(&c, 4, true, false);
    sm_config_set_in_shift(&c, false, false, 32);

    for (uint pin = 0; pin < 8; ++pin) {
        pio_gpio_init(pio, pin);
    }

    pio_sm_init(pio, sm, offset + PIO_READ_MAIN_ADDR, &c);
    pio_sm_put_blocking(pio, sm, 0xFFu);
    pio_sm_exec_wait_blocking(pio, sm, pio_encode_pull(false, true));
    pio_sm_exec_wait_blocking(pio, sm, pio_encode_mov(pio_x, pio_osr));
    sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_RX);
    pio_sm_set_config(pio, sm, &c);
    pio_sm_set_enabled(pio, sm, true);
}

// ---------------------------------------------------------------------------------
// SETUP COMMUNICATION with Z80 bus to let P2000T know a M2009 cartridge is present
// ---------------------------------------------------------------------------------
void z80_io_init() {
    pinMode(PIN_BUS_OE, OUTPUT);
    digitalWrite(PIN_BUS_OE, LOW);
    
    // Set up the PIO programs for the Z80 bus communication (co-processor)
    build_z80_bus_programs();

    if (!pio_can_add_program_at_offset(pio0, &z80_bus_read_program, PIO_READ_TABLE_ADDR) ||
        !pio_can_add_program_at_offset(pio1, &z80_bus_write_program, PIO_WRITE_PROGRAM_OFFSET) ||
        !pio_can_add_program_at_offset(pio1, &z80_bus_oe_program,  PIO_OE_PROGRAM_OFFSET)) {
            Serial.println("Failed to add PIO program at offset!");
            while (true) {
                // Blink the RED LED to indicate failure to add PIO program
                digitalWrite(PIN_LED_RED, LED_ON);
                delay(200);
                digitalWrite(PIN_LED_RED, LED_OFF);
                delay(200);
            }
    }

    pio_add_program_at_offset(pio0, &z80_bus_read_program, PIO_READ_TABLE_ADDR);
    pio_add_program_at_offset(pio1, &z80_bus_write_program, PIO_WRITE_PROGRAM_OFFSET);
    pio_add_program_at_offset(pio1, &z80_bus_oe_program, PIO_OE_PROGRAM_OFFSET);
    pioReadProgramLoaded = true;

    sm_read = pio_claim_unused_sm(pio0, true);
    init_z80_read_sm(pio0, sm_read, PIO_READ_TABLE_ADDR);
    sm_write = pio_claim_unused_sm(pio1, true);
    init_z80_write_sm(pio1, sm_write, PIO_WRITE_PROGRAM_OFFSET);
    startWriteDma();
    fastLastIdx = 0;  // the DMA has just started at the ring base
    // Exclusive handlers: the SDK's shared-IRQ dispatcher is extra code on the critical path
    pio_set_irq0_source_enabled(pio1, pis_interrupt0, true);
    pio1->irq = 1u;
    irq_set_exclusive_handler(PIO1_IRQ_0, z80BusIrq);
    irq_set_priority(PIO1_IRQ_0, 0);
    irq_set_enabled(PIO1_IRQ_0, true);
    pio0->irq = 1u;
    pio_set_irq0_source_enabled(pio0, pis_interrupt0, true);
    irq_set_exclusive_handler(PIO0_IRQ_0, z80BusIrq);
    irq_set_priority(PIO0_IRQ_0, 0);
    irq_set_enabled(PIO0_IRQ_0, true);
    sm_oe = pio_claim_unused_sm(pio1, true);
    init_z80_oe_sm(pio1, sm_oe, PIO_OE_PROGRAM_OFFSET);

    // Flag helper (needs the free PIO1 instruction slots right behind the OE program)
    gpio_init(PIN_FASTSEL);
    gpio_pull_down(PIN_FASTSEL);
    if (pio_can_add_program_at_offset(pio1, &z80_bus_flag_program, PIO_FLAG_PROGRAM_OFFSET)) {
        pio_add_program_at_offset(pio1, &z80_bus_flag_program, PIO_FLAG_PROGRAM_OFFSET);
        sm_flag = pio_claim_unused_sm(pio1, true);
        init_z80_flag_sm(pio1, sm_flag, PIO_FLAG_PROGRAM_OFFSET);
        flagSmRunning = true;
    } else {
        Serial.println("PIO1 has no room for the flag helper; using the interrupt path only");
    }
}

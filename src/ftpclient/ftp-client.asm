; ============================================================================
; Philips P2000T FTP-client 
; copyright (c) Bart Eversdijk 2026
;
; Compile with z80asm.exe
; https://www.ticalc.org/archives/files/fileinfo/168/16866.html
; ============================================================================
module ftpclient

; --- PART 1: P2000T .CAS HEADER (256 bytes) ---
org $6447               ; 256 bytes ($0100) before the BASIC startaddress ($6547)

.header_start
defs $30                ; Reserved ($30 bytes)
defw $6547              ; Basic load address: $6547 (default BASIC NL start address) (2 bytes)
defw program_end - program_start ; Exact length of the generated program (2 bytes)
defw program_end - program_start ; Exact length of this datablok: max 1024 bytes ($0400) if smaller than 1024 bytes the real length will be used (2 bytes)
defm "ftp-clie"         ; First 8 bytes of the filename on tape filled with spaces if shorter than 8 bytes (8 bytes)
defm "BAS"              ; File extension (3 bytes)
defm "B"                ; File type: 'B' for BASIC (1 byte)
defm "N"                ; Land code (1 byte)
defw $6547              ; Start address of stand-alone program (no Basic module) (2 bytes)
defw $6547              ; Load address of stand-alone program (no Basic module) (2 bytes)
defm "nt      "         ; Second 8 bytes of the filename on tape filled with spaces if shorter than 8 bytes (8 bytes)
defb $01                ; Number of blocks (1 byte)
.header_end
defs $100 - header_end ; Fill up to 256 bytes for the header

; --- PART 2: BASIC POROGRAM (start on $6547) ---
.program_start          ; At $6547

; 10 A=PEEK(&H605C):Y=&H9FFF:IF A > 1 THEN Y=Y+(&H2000*A) 
; WARNING: 320 should be minimal program_end - start 
;        + 70 byte extra when next program does clear 50 !!!!
defw line_20            ; Pointer to next BASIC-line (2 bytes)
defw  10                ; Linenumber 20 (2 bytes)
defb 'A', $CA, $DB       ;  BASIC code for: "A=PEEK"
defm "(&H605C):"
defb 'Y', $CA           ; "Y="
defm "&H9FFF:"          ; IF A>1 THEN Y=Y+
defb $8A, 'A', $C9, '1', $BA, 'Y', $CA, 'Y', $BD 
defm "(&H2000"
defb $BF                ; *
defm "A)"
defb $00                ; End of  BASIC-line 00 (1 byte)

.line_20
; 20 CLEAR 50,(Y-400)     <--- Minimal program_end - start !!!
defw line_30            ; Pointer to next BASIC-line (2 bytes)
defw  20                ; Linenumber 20 (2 bytes)
defb $AB                ; CLEAR
defm "50,(Y"
defb $BE
defm "400)"             ; WARNING: this 400 should be minimal program_end - start !!!!
defb $00                ; End of  BASIC-line 00 (1 byte)

.line_30
; 30 DEF USR=&H6610:X=USR(0)
defw line_40            ; Pointer to next BASIC-line (2 bytes)
defw 30                 ; Linenumber 10 (2 bytes) "
defb $A3, $B5, $CA      ; BASIC code for: "DEF USR="(3 bytes)
defm "&H6610:"          ; Startadres van de machinecode + colon (8 bytes)
defm "X"                ; "X" (2 bytes)
defb $CA, $B5           ; codes for "=USR" (2 bytes)
defm "(0)"              ; "(0)" (3 bytes)
defb $00                ; End of  BASIC-line 00 (1 byte)

.line_40
; REM FTP-client voor M2009-emulator
defw line_50            ; Pointer to next BASIC-line (2 bytes)
defw  40                ; Linenumber (2 bytes)
defb $8e                ; BASIC code for: "REM" (1 byte)
defm " FTP-client voor M2009-emulator"
defb $00                ; End of  BASIC-line 00 (1 byte)

.line_50
; REM Version-information
defw line_60            ; Pointer to next BASIC-line (2 bytes)
defw  50                ; Linenumber (2 bytes)
defb $8e                ; BASIC code for: "REM" (1 byte)
defm " Versie: 1.0"
defb $00                ; End of  BASIC-line 00 (1 byte)

.line_60
; REM Date-information
defw line_70            ; Pointer to next BASIC-line (2 bytes)
defw  60                ; Linenumber (2 bytes)
defb $8e                ; BASIC code for: "REM" (1 byte)
defm " Datum : 22-11-2026"
defb $00                ; End of  BASIC-line 00 (1 byte)

.line_70
; REM Copyright-information
defw einde_basic        ; Pointer to next BASIC-line (2 bytes)
defw  70                ; Linenumber (2 bytes)
defb $8e                ; BASIC code for: "REM" (1 byte)
defm " (c) Bart Eversdijk"
defb $00                ; End of  BASIC-line 00 (1 byte)

.einde_basic
defw $0000              ; End  BASIC-programma (2 bytes)
.basic_end

; Fill from $6547 until $6610 = 201 bytes ($C9)
defs $C9 - (basic_end - program_start) ; Vul vanaf program_start aan tot $6610

; --- PART 3: Z80 MACHINE CODE BOOTLOADER (start from $6610) ---
; M20009 IO portsPorts:
;   40-43 Z8630 - Z80SCC (Serial Communication Controller)
;         40: cmd/status channel A  
;         41: data in/out channel A --> Sending to outside world 75 Baud2
;         42: cmd/status channel B
;         43: data in/out channel B --> Receiving from outside world 1200 Baud
   
;       De status registers (0x40 ch-A en 0x42 ch-B)
;       1    bit 0: Rx char available
;       2    bit 1: Zero count (not used)
;       4    bit 2: Tx buffer empty
;       8    bit 3: Data Carrier Detect (Valid host or client connection found)
;      10    bit 4: SYNC Hunt (used when dialing ??)
;      20    bit 5: Clear To Send
;      40    bit 6: Tx underrun (EOM) (not used)
;      80    bit 7: Break abort (not used)
;   Channel A appears to be used for data carrier detection 

;     44-47  Phone Line control (official just only port 44 but the M2009 ignores A0 en A1 so practical 0x44-0x47)
;         bit 0-4: MC0-MC4 speed config (can be ignored)
;         bit 5: Phone off hook (Line open)
;         bit 6: Dial active (line short with resistor)
;         bit 7: Dial pulse  (line short)
;
;     47: Special function 
;           bit 7: + number 0-29 is speed dial Phonebook entry 'number'
defc    SSCStatusA           = $40     ; Port for SSC status channel A (0x40) and B (0x42)
defc    SSCDataA             = $41     ; Port for SSC data channel A (0x41) and B (0x43)
defc    SSCStatusB           = $42     ; Port for SSC status channel A (0x40) and B (0x42)
defc    SSCDataB             = $43     ; Port for SSC data channel A (0x41) and B (0x43)
defc    LineControl          = $44     ; Port for phone line control
defc    SpecialCmds          = $47     ; Port for speed dial phonebook entry

; BASIC NL routines
defc    PrintA              = $1386   ; Print character in A
defc    PrintStr            = $3383   ; Print string starting at HL until 0 byte
defc    PrintCasError       = $1053   ; Print Cassette error in A >= 64 (page 147 P3505)
defc    PrintBasicError     = $247D   ; Print Basic NL error in E < 64  (page 149 P3505)
defc    Return2basic        = $1FC6   ; Return to BasicNL routine
defc    OverwriteFile       = $1560   ; Address to handle file overwrite

; Clear N1,N2       N1=Room for variables, N2=Top address for variables, above is room for machine code routines
defc    ClearStringSpace    = $6258   ; Address which marks end BasicNL program area, start or variables area (=N2-N1)
                                      ; on 16k = 0x9FCD, 32k = 0xDFCD, 64k = 0xFFCD (with JWS 0xDFCD)
defc    ClearTopOfRAM       = $63b8

; Addresses in RAM for Basic NL and ROM module
defc    Usr0                = $623E   ; Address of USR0 routine in RAM
defc    SlowList            = $6013   ; Address of SlowList $40 enable slow listing
defc    KladblokMem         = $6151   ; Start address of "Kladblok" memory
defc    StartAdrProgram     = $625C   ; Start address of the Basic program in RAM (default: $6547)
defc    EndAdrProgram       = $6405   ; End address of the Basic program in RAM
defc    CommandHook         = $60D0   ; Address of command hook routine "call $E983" or 00, 00, 00

; Own definitions - free Masic NL memory space
defc    SaveCommandPtr      = $61E0   ; Address to save HL register
defc    Counter             = $61E2   ; Address to save de Counter

; SAA5050 color codes
defc    ColorRed             = $81     ; Color code for red text
defc    ColorGreen           = $82     ; Color code for green text
defc    ColorYellow          = $83     ; Color code for yellow text
defc    ColorBlue            = $84     ; Color code for blue text
defc    ColorMagenta         = $85     ; Color code for magenta text
defc    ColorCyan            = $86     ; Color code for cyan text
defc    ColorWhite           = $87     ; Color code for white text

.start_loader                         ; We start at $6610
    ld a, $9C                         ; Remove USR(0) call
    ld (Usr0),a                       ; Replace by original USR(0) Basic NL code $9C $28
    ld a, $28                         
    ld (Usr0+1),a

    in a,(SSCStatusA)                 ; Check if we have a M2009-wifi module present
    cp $ff   
    jr z, m2009_not_found             ; An empty IO-port will return 255

    ld a,$2                           ; Check if we have M2009-wifi module present
    out (SSCStatusA),a                ; Store $7 in SSCStatus WREG 2 (which is mirrored to RREG 2)
    call wait50ms_init                ; Allow the M2009 to settle between I/O operations
    ld a,$7                     
    out (SSCStatusA),a  
    call wait50ms_init
    ld a,$2                          ; Select again WREG 2 again
    out (SSCStatusA),a
    call wait50ms_init
    in a,(SSCStatusA)                
    cp $7                            ; Read back LineControl to ensure the output has taken effect
    jr z, m2009_found

.m2009_not_found
    ld hl, notfound_text
    call PrintStr
    ret

.m2009_found
    ld de,(ClearTopOfRAM)            ; Move program to upper memory (16k and 32k safe)
    inc de                           ; Room was created by Clear command in Basic part
    ld (relocation_target),de        ; We copy the start of the program to just above this memory location

    ld hl,(CommandHook+1)            ; Check if the program is already loaded (hook was not set already)
    or a                             ; clear carry for SBC
    sbc hl,de                        ; Yes, same as relocation target, program already loaded
    jr nz, move_program_to_upper_memory

    ld hl,allready_loaded_text       ; Inform the user and exit program
    call PrintStr
    ret

.move_program_to_upper_memory
    ld hl,CommandHook               ; Load address of Basic command hook into HL
    ld de,basic_hook_jwscall        ; Load address of our hook location
    ld bc,3
    ldir

    ld hl,(relocation_target)
    ld de,start                     ; Start of FTP-client program in memory
    or a
    sbc hl,de                       ; Calculate relocation delta between start of program and upper memory
    ld (relocation_delta),hl        ; By using "CLEAR x,y" values from BasicNL we are 16k, 32k, or 64k safe

    ld hl,start                     ; Now move program to upper memory
    ld de,(relocation_target)
    ld bc,program_end-start
    ldir                            ; Copy BC-bytes from HL-address to DE-address

    ld hl,relocation_table          ; Make sure that all fixed addresses are part of the relocate table
.relocation_loop                    ; call xxxx, jp xxxx and ld (xxxx),r ld r,(xxxx) 
    ld e,(hl)                       ; (jr is relative no update needed)
    inc hl                          ; Do not add memory address ROM and Basic NL locations, they are fixed
    ld d,(hl)
    inc hl
    ld a,d
    or e                            ; The table ends with 0
    jr z, relocation_done
    push hl                         ; Save relocation table pointer  

    ld a,(de)                       ; Get low byte of address to relocate
    ld l,a                  
    inc de
    ld a,(de)                       ; Get high byte of address to relocate
    ld h,a
    ld bc,(relocation_delta)        ; Load relocation delta into BC
    add hl,bc                       ; Add relocation delta to address
    ex de,hl    
    add hl,bc                       ; And update DE as well to new location
    ex de,hl                        ; Add hl.de does not exist in Z80, so we swap, add, and swap back
    ld a,h
    ld (de),a                       ; Store high byte of relocated address
    dec de
    ld a,l
    ld (de),a                       ; Store low byte of relocated address
    pop hl
    jr relocation_loop

.relocation_done
    ld a,$cd                        ; 'Call' instruction opcode for Z80
    ld hl,(relocation_target)           
    ld (CommandHook+1),hl           ; Set CommandHook to point to relocated program start
    
    ld hl, loaded_text              ; Laad het adres van de tekst in HL (3 bytes)
    call PrintStr

    jp Return2basic

.loaded_text
    defb ColorYellow
    defm "FTP-client in RAM geladen (:#fhelp)"
    defb 0              ; String End

.notfound_text
    defb ColorMagenta
    defm "Geen M2009-wifi module gevonden"
    defb 0              ; String End

.allready_loaded_text
    defb ColorMagenta
    defm "FTP-client was al in RAM geladen"
    defb 0              ; String End


.relocation_table
    defw reloc_send_command_call+1
    defw reloc_initial_receive_byte_call+1
    defw reloc_overwrite_receive_text_call+1
    defw reloc_overwrite_send_file_jump+1
    defw reloc_send_file_jump+1
    defw reloc_connecting_call+1
    defw reloc_connected_receive_text_call+1
    defw reloc_send_command_byte_call+1
    defw reloc_connected_recv_file_jump+1
    defw reloc_crbyte_load+1
    defw reloc_crbyte_send_call+1
    defw reloc_receive_text_byte_call+1
    defw reloc_receive_start_low_call+1
    defw reloc_receive_start_high_call+1
    defw reloc_receive_size_low_call+1
    defw reloc_receive_size_high_call+1
    defw reloc_receive_marker_call+1
    defw reloc_receive_payload_call+1
    defw reloc_receive_done_jump+1
    defw reloc_receive_error_jump+1
    defw reloc_send_start_low_call+1
    defw reloc_send_start_high_call+1
    defw reloc_send_size_low_call+1
    defw reloc_send_size_high_call+1
    defw reloc_send_byte_loop_call+1
    defw reloc_no_byte_yet_call+1
    defw reloc_send_sbyte_load+1
    defw reloc_send_sbyte_call+1
    defw reloc_send_done_jump+1
    defw reloc_send_payload_call+1
    defw 0
.relocation_delta
    defw 0
.relocation_target
    defw 0
    nop
    nop
    nop

.wait50ms_init
    push bc
    ld b,31
.wait50ms_outer
    ld c,$0                 ; loop is 1.636 ms per iteration
.wait50ms_inner
    dec c
    jr nz, wait50ms_inner
    djnz wait50ms_outer
    pop bc
    ret

; --- PART 4: Z80 MACHINE CODE ACTUAL FTP-CLIENT PROGRAM ---
; --- Will be relocated to top of memory with bootloader part
.start
    push hl                  ; Save HL on stack
    ld a,(hl)                ; Do we have ":#F" at the start of the command buffer?
    cp ':'
    jr nz, no_ftp_cmd
    inc hl
    ld a,(hl)
    cp '#'
    jr nz, no_ftp_cmd
    inc hl
    ld a,(hl)
    cp 'F'                   ; Command is uppercased by BasicNL so no need to check for lowercase
    jr z, found_ftp_cmd
.no_ftp_cmd
    pop hl                   ; Restore HL from stack
.basic_hook_jwscall    
    nop                      ; Copy Basic hook from $60D0 to the preserve JWS hook (if present)
    nop
    nop
    ret                      ; No for us, back to BasicNL 

; We have ":#F" at the start of the command buffer
.found_ftp_cmd               ; Found FTP command, sent command to FTP server 
    pop de                   ; Restore DE from stack
    ld a,$20                 ; Set bit 5 in A, line "off hook"
    out (LineControl),a 
    ld a, $80+$1F            ; Speed dial entry 31 (special FTP server)
    ;ld a, $80+$4            ; Speed dial entry 4 (mame server)
    out (SpecialCmds),a

    ld a,$40                 ; Switch to slow listing mode
    ld (SlowList),a

    ld b, $20                ; Set timeout to 16 seconds
.connecting
.reloc_connecting_call
    call wait200ms           ; Wait for 100 milliseconds before retrying connection

    in a,(SSCStatusA)        ; Read SSC status to check connection status
    bit 3,a                  ; Check if connection is established 3 is set (DCD flag)
    jr nz,connected          ; Retry if not connected
    djnz connecting
    ; Timeout reached, handle connection failure here if needed
    ld e,'A'                 ; Generate "Geen cassette" code "A"
    jr error_close_connection     ; Jump to close connection and report error

.connected
.reloc_send_command_call
    call send_command        ; Send FTP command, HL has start of command after :#

.reloc_initial_receive_byte_call
    call receive_byte        ; The first byte of the FRP server is a command code
    ld e,a                   ; Store the received command code in E 
    cp 'Q'                   ; Q or lower means we have an error
    jr c, error_close_connection  ; Jump to close connection if error

    cp 'R'                   ; R = receive a file
.reloc_connected_recv_file_jump
    jp z, receive_file

    cp 'U'                   ; U = send a file, "Overschrijven (J/N)?"
    jr nz, check_for_send_file
.reloc_overwrite_receive_text_call
    call receive_text        ; Get the text from the FTP server
    call OverwriteFile       ; At No functions returns to BasicNL
.reloc_overwrite_send_file_jump
    jp send_file

.check_for_send_file
    cp 'S'                   ; S = send a file
.reloc_send_file_jump
    jp z, send_file

.reloc_connected_receive_text_call
    call receive_text        ; All the rest indicates text from the FTP server
.done    
    ld hl,(SaveCommandPtr)
    ret                      ; Return to BasicNL after handling FTP command

.error_close_connection
    xor a
    out (LineControl),a      ;  "On hook" to close connection
    ld a,e                   ; Do we need to report an error?
    or a                     ; Set zero flag if A is zero (no error) or not (error)
    call nz,PrintCasError
  
    ret                      ; Return to BasicNL after handling error 

;------------ wait routines ------------
.wait200ms
    push bc
    ld b,63
    jr waitB16ms_outer
.wait50ms
    push bc
    ld b,31
.waitB16ms_outer
    ld c,$0                 ; loop is 1.636 ms per iteration
.waitB16ms_inner
    dec c
    jr nz, waitB16ms_inner
    djnz  waitB16ms_outer
    pop bc
    ret


; --- Send byte routine ---
.send_byte
    ld b,$20                 ; Set B for timeout of 9 seconds
.send_byte_loop
    in a,(SSCStatusB)        ; Read SSC B status to check if ready to send
    bit 2,a                  ; Check if bit 2 is set (Tx buffer empty)
    jr nz, send_char_ready

    in a,(SSCStatusA)        ; Read SSC B status to check for carrier status
    bit 3,a                  ; Check if bit 3 is set (still Data Carrier Detect flag)
    jr z, receive_error      ; Jump to receive error if carrier lost

.reloc_send_byte_loop_call
    call wait200ms           ; Give the M2009 time to process before retrying

    djnz send_byte_loop      ; Retry until timeout

    ld e,'I'                 ; Generate "Terugspoeltijd voorbij" (timeout) code "I"
    jr error_close_connection

.send_char_ready
    ld a,(hl)
    out (SSCDataB),a         ; Send character to FTP server
    ret                      ; Continue sending next character

;--- receive byte routine ---
.receive_byte
    ld b,$40                ; Set B for timeout of 10 seconds
    in a,(SSCStatusA)       ; Read SSC status to check if data is available
    bit 0,a                 ; Check if bit 0 is set (Rx buffer full)
    jr z, no_byte_yet

    in a,(SSCDataA)         ; Read a byte from the FTP server
    ret

.no_byte_yet
.reloc_no_byte_yet_call
    call wait200ms           ; Give the M2009 time to process before retrying
    
    in a,(SSCStatusA)       ; Read a byte from the FTP server
    bit 3,a                 ; Check if bit 3 is set (still Data Carrier Detect flag)
    jr z, receive_error 

    djnz receive_byte       ; Retry until timeout

.receive_error              ; Fall through to close connection
    ld e,'C'                ; Generate "Leesfout" (carrier lost) code "C"
    jr error_close_connection

; --- Send string routine ---
.send_command
    ; HL points to start of command after :#
    ld a,(hl)               ; Load character from HL
    or a                    ; Check for end of string
    jr z, send_command_done
.reloc_send_command_byte_call
    call send_byte
    inc hl                  ; Move to next character in HL
    jr send_command

.send_command_done          ; Fall through to close connection
    ld (SaveCommandPtr),hl
.reloc_crbyte_load
    ld hl,crbyte            ; Send carriage return to indicate end of command
.reloc_crbyte_send_call
    call send_byte
    ret

;--- receive string routine ---
.receive_text
    ld e,0 
.reloc_receive_text_byte_call
    call receive_byte       ; Receive a byte from the FTP server
    or a                    ; Check for end of string
    ret z

    call PrintA             ; Print the received character in screen
    jr receive_text         ; Continue receiving text until end of string

;--- receive file routine ---
.receive_file
    ; Implement receiving a file from FTP server here
.reloc_receive_start_low_call
    call receive_byte
    ld (StartAdrProgram),a
.reloc_receive_start_high_call
    call receive_byte
    ld (StartAdrProgram+1),a

.reloc_receive_size_low_call
    call receive_byte
    ld (Counter),a
.reloc_receive_size_high_call
    call receive_byte
    ld (Counter+1),a

.reloc_receive_marker_call
    call receive_byte
    ld e,'B'                ; Set error code to 'B' (geen begin gevonden)
    cp 'R'                  ; If not "R", go to receive error
.reloc_receive_error_jump
    jp nz,error_close_connection       

    ld hl,(StartAdrProgram)
    ld de,(Counter)
   
.receive_loop
.reloc_receive_payload_call
    call receive_byte
    ld (hl),a
    inc hl
    dec de
    ld a,d
    or e
    jr nz, receive_loop

    ld (EndAdrProgram),hl   
.reloc_receive_done_jump    
    jp done

; --- Send file routine ---
.send_file
    ld hl,StartAdrProgram
.reloc_send_start_low_call
    call send_byte              ; Start address, little-endian
    inc hl                
.reloc_send_start_high_call          
    call send_byte              

    ld de,(StartAdrProgram)
    ld hl,(EndAdrProgram)       ; Get 
    or a
    sbc hl,de                   ; HL = end address - start address
    ld (Counter),hl

    ld hl,Counter
.reloc_send_size_low_call
    call send_byte              ; Send File length, little-endian
    inc hl
.reloc_send_size_high_call
    call send_byte

.reloc_send_sbyte_load
    ld hl,Sbyte                 ; Send 'S' byte indicating start of file
.reloc_send_sbyte_call
    call send_byte

    ld hl,(StartAdrProgram)
    ld de,(Counter)
.send_file_loop
    ld a,d
    or e
.reloc_send_done_jump
    jp z, done                  ; Done sending file
    push de
.reloc_send_payload_call
    call send_byte
    pop de
    inc hl
    dec de
    jr send_file_loop

.Sbyte
    defb 'S'
.crbyte
    defb $d
    
.program_end

defs $400 + $100 - program_end  ; Fill up to 1024 bytes (+ 256 bytes for header) 



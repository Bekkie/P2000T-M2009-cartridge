'''
MyFTPServer - Python socket server for M2009 Emulator

Author: Bart Eversdijk
Description: Handles FTP-like file transfers for the M2009 Emulator.

Works with both disk and cassette media images.
Disk media images should be in the format of JWS disk files. (*.dsk)
Cassette media images should be in the P2000T tape file format. (*.cas) as stored in P2000 preservation side (https://github.com/p2000t/software)

Use ftp-client in combination with the M2009 Emulator to transfer files, with commands like:
  :#fservers         : list available servers
  :#fserver <nr>     : select server by number (from phonebook)
  :#fserver"<url>"   : select server by URL "url:port"
  :#fdir             : list available disks/cassettes
  :#fcd"<naam>"      : change selected disk/cassette-dir. to "<naam>"
  :#fzoek            : show current directory contents
  :#fload"<naam>"    : load file "<naam>" from server
  :#fsave"<naam>"    : save file "<naam>" to server (only cassette media is supported)
  :#fbye             : disconnect from server
  :#fhelp            : show this help message

Start the server with the specified configuration and make it ready to handle FTP-like requests from the M2009 Emulator.
Make sure the firewall allows connections to the specified host and port.
Usage:
  python myftpserver.py [options] 
  Options:
    -h, --help      Show this help message and exit
    -f, --file      Specify the directory to serve media files from (multiple files and wildcard supported)
    -v, --version   Show the server version and exit
    
    -H, --host      Specify the host address for the server (default: own IP)
    -p, --port      Specify the port to run the server on (default: 21)
    -r, --readonly  Do not allow modifications to the served files
    
    -V, --verbose   Enable verbose logging
    -S, --silent    Show only errors and critical information
    -D, --debug     Show detailed and debug information
           

Communicatieprotocol: Between P2000T M2009 emulator and this FTP-like server
    - Each command starts with a new socket connection after the command is issued in the M2009 Emulator.
      After the command is processed, the socket connection is closed by the server and/or the client.
    - Each command from the M2009 Emulator starts with ":#f" followed by the specific command and its arguments 
      and ends with a new line character.
      (e.g., ":#fload\"file.cas\"\n") The M2009 Emulator strips the ":#" prefix before sending the command to the server.

    - After a command is processed the (on '\n'') it returns  with a
        - 'T': indicates a nul-terminated string follows, the client print this string directly on the P2000T display 
               it will also contains the special color codes for the P2000T display. 
        - 'S': indicates the ftp-client should start sending a file with special header
                  2 bytes (little-endian) indicating the file size
                  2 bytes (little-endian) indicating the load address in P2000T memory
                  'S' indicates the start of the file transfer data follows
                  xx bytes of file data follow (xx=file size specified earlier)
        - 'U': indicates a nul-terminated string follows, giving an existing filename, this message can be prinnter directly 
               on the P2000T display. The client should ask the to confirm "overwrite" or similar action from the user.
                  On positive confirmation, the client should proceed with the file transfer as specified by the 'S' command
                  On negative confirmation, the client should close the connection.
        - 'R': indicates the ftp-client should prepare to receive a file. The server will send a header first
                  2 bytes (little-endian) indicating the file size
                  2 bytes (little-endian) indicating the load address in P2000T memory
                  'R' indicates the start of the file transfer data follows
                  xx bytes of file data follow (xx=file size specified earlier)
        - All responses below T (A-Q) indicate an error, which kan be send as cassette error (See P2000T user manual page 149) 

    responses:
        All: 
            Onbekend FTP commando
            Geen server geselecteerd  -> Send byte M2009 emulator

        FCD command can report:
            Disk niet gevonden
            Geen geldige disk naam gegeven
            Geen disk geselecteerd

        LOAD command can report:
            Disk niet gevonden
            Geen geldige disk naam gegeven
            Ongeldige extensie
            Geen disk geselecteerd

        FSAVE command can report":
            Geen cassette dir geselecteerd
            Ongeldig media type (geen cass-dir)
            Geen wildcards toegestaan
            Ongeldige extensie
            "G" -->   Cassete fout G (Geen stopje)

        Commands handled by M2009 emulator
            FSERVERS
            FSERVER
                Server niet gevonden
                Geen server geselecteerd
                Ongeldige server URL
                Ongeldig FTP argument
                Ongeldig FTP commando

        Commands handled by both M2009 emulator and server (depending on connection)
            FHELP
            FBYE

        Error messages from ftp-client:
            "A" "Geen cassette" [could not connect]
            "B" "Cassette fout B" (geen begin gevonden)  [wrong start marker]
            "C" "Leesfout" (carrier lost)
            "I" "Cassette fout I" (Terugspoeltijd voorbij) [timeout]
'''

import socket
import threading
import time
import os
import errno
import msvcrt
import signal
import sys
import glob
import logging
import textwrap
import argparse
import struct
from zeroconf import ServiceInfo, Zeroconf
from dataclasses import dataclass, field

# P2000T FTP Server for M2009 Emulator
# TODO: Extra programma om hele disks up en donwloaden (per track/side)
# TODO: Grafisch user interface for the server (GUI Unix and Windows)

_VERSION_ = "1.0"
_COPYRIGHT_ = "(c) Bart Eversdijk '2026. All rights reserved."

# Setup logger and add verbose logging level (-V option)
logger = logging.getLogger('p2000tftp')
VERBOSE_LEVELV_NUM = 15
logging.addLevelName(VERBOSE_LEVELV_NUM, "VERBOSE")
def verbose(self, message, *args, **kws):
    if self.isEnabledFor(VERBOSE_LEVELV_NUM):
        self._log(VERBOSE_LEVELV_NUM, message, args, **kws) 
logging.Logger.verbose = verbose

zeroconf = Zeroconf()

# Tape header
BASEADRS = 0x6547

# Disk track and block configuration (16 sectors per track each sector 256 bytes)
SECTORSIZE = 256
BLOCKS_PER_TRACK = 16
TRACKSIZE = BLOCKS_PER_TRACK * SECTORSIZE

LINE_SIZE = 40
LINES_PER_PAGE = 24
SCREEN_SIZE = LINES_PER_PAGE * LINE_SIZE

P2000T_RED     = "\x81"
P2000T_GREEN   = "\x82"
P2000T_YELLOW  = "\x83"
P2000T_BLUE    = "\x84"
P2000T_MAGENTA = "\x85"
P2000T_CYAN    = "\x86"
P2000T_WHITE   = "\x87"

P2000T_GR_RED     = "\x91"
P2000T_GR_GREEN   = "\x92"
P2000T_GR_YELLOW  = "\x93"
P2000T_GR_BLUE    = "\x94"
P2000T_GR_MAGENTA = "\x95"
P2000T_GR_CYAN    = "\x96"
P2000T_GR_WHITE   = "\x97"

@dataclass
class JWSIndexEntry:
    name: str
    ext: str
    type: str
    size: int
    startadr: int
    side: int
    startblock: int
    lastblock: int
    blocks: int

@dataclass
class TapeIndexEntry:
    name: str
    ext: str
    type: str
    size: int
    startadr: int
    lastfile: int
    startblock: int
    lastblock: int
    blocks: int
    mediafilename: str = ""


@dataclass
class MediaEntry:
    fullname: str
    name: str  = None
    metadata: dict = None
    writeprotected: bool = False
    index: list = None
    type: str = "disk"

@dataclass
class FileTransfer:
    receiving: bool = False
    received_bytes: int = 0
    filename: str = ""
    extension: str = ""
    content: bytearray = field(default_factory=bytearray)
    filesize: int = 0
    start_address: int = 0
    overwrite: bool = False

@dataclass
class ClientSession:
    socket: socket.socket
    selected_index: int = -1
    transfer: FileTransfer = field(default_factory=FileTransfer)
    connected: bool = True

running = True


# --- Helper functions for formatting and file operations ---------------------------
def format_hex_text(data, bytes_per_line=32, column_size=8):
    lines = []
    for offset in range(0, len(data), bytes_per_line):
        chunk = data[offset:offset + bytes_per_line]
        columns = []
        for col_start in range(0, bytes_per_line, column_size):
            col = chunk[col_start:col_start + column_size]
            col_hex = " ".join(f"{b:02X}" for b in col).ljust(column_size * 3 - 1)
            columns.append(col_hex)
        decoded = "".join(chr(b) if 32 <= b <= 126 else "." for b in chunk)

        lines.append(f"{offset:04X}  {'  '.join(columns)}  |{decoded}|")

    return "\n".join(lines)
  
def read_content(infilename):
    logger.debug(f"- opening file: {infilename}" )
    with open(infilename, 'rb') as infile:
        logger.debug("- Reading data")
        data = infile.read()
    
    logger.debug(f"- Content size {len(data)} bytes")

    return data

# --- Media image handling --------------------------------------------------------------------------------------------------
def list_media_content(disk, session):
    '''List the contents of a Cassette directory.'''
    if disk.type == "C":
        output = [f"T{P2000T_CYAN}Cassette map:{P2000T_YELLOW}{disk.name}\r\n"]
        for entry in disk.index:
            logger.debug(f"Cassette entry: {entry}")
            
            if entry.startblock == 0:
                # New cassette and the last one tape, so multiple cassettes on the same disk, show cassette filename
                if not entry.lastfile:
                    output.append(f"{P2000T_YELLOW}Cass:{P2000T_CYAN}{entry.mediafilename[:32]}\r\n")
                    multiple_files_on_tape = True
                else:
                    multiple_files_on_tape = False

            if multiple_files_on_tape:
                output.append(
                    f"{P2000T_GREEN}  {entry.name:<16} {entry.ext:<3}"
                    f"{P2000T_CYAN}{entry.type:<1}{entry.size:7d}\r\n"
                )
            else:
                output.append(
                    f"{P2000T_GREEN}{entry.name:<16} {entry.ext:<3}"
                    f"{P2000T_CYAN}{entry.type:<1}{entry.size:7d}\r\n"
                )
        output.append("\x00")
        send_data_to_P2000("".join(output), session)
        return

    '''List the contents of a JWS disk image.'''
    output = [f"T{P2000T_CYAN}Disk naam:{P2000T_YELLOW}{disk.name}\r\n"]
       
    def print_free_space(blocncnt):
        if 608 - blocncnt > 0:
            return f"{' ':<17s}{P2000T_GREEN}vrij{P2000T_CYAN}   {608 - blocncnt:3d}\r\n"
        return f"{' ':<17s}{P2000T_GREEN}vol{P2000T_CYAN}\r\n"

    if disk.metadata != None and len(disk.metadata) > 0:
        logger.verbose(f"Disk metadata: {disk.metadata}")
        output.append(f"{P2000T_CYAN}{disk.metadata['copyright']}\r\n")
        output.append(f"{P2000T_YELLOW}{disk.metadata['version']}{disk.metadata['versionsub']}\r\n")
        output.append(f"{P2000T_GREEN}{disk.metadata['disk_type']}\r\n")

    output.append(f"{P2000T_CYAN}kant 1\r\n")

    last_side = 0
    blocncnt = 0

    for idx in disk.index:
        if idx.side == 1 and last_side == 0:
            output.append(print_free_space(blocncnt))
            output.append(f"{P2000T_CYAN}kant 2\r\n")
            last_side = 1
            blocncnt = 0

        blocks = idx.lastblock - idx.startblock + 1
        output.append(f"{P2000T_GREEN}{idx.name:<16} {idx.ext:<3}{P2000T_CYAN}{idx.type:<1}   {blocks:3d}{idx.size:6d}\r\n")
        blocncnt += blocks

    if last_side == 0:
        output.append(print_free_space(blocncnt))
        output.append(f"{P2000T_CYAN}kant 2\r\n")
        blocncnt = 0

    output.append(print_free_space(blocncnt))
    output.append("\x00")
    send_data_to_P2000("".join(output), session)

def list_available_media(disks, session):
    '''List the available media.'''
    send_data_to_P2000(f"T{P2000T_CYAN}Disk overzicht:\r\n", session)
    for i, disk in enumerate(disks):
        if session.selected_index == i:
            send_data_to_P2000(f"*{P2000T_YELLOW}{disk.name:<29}{P2000T_CYAN}{disk.type}{len(disk.index):-4}\r\n", session)
        else:
            send_data_to_P2000(f"{P2000T_GREEN}{disk.name:<30}{P2000T_CYAN}{disk.type}{len(disk.index):-4}\r\n", session)
    send_data_to_P2000("\x00", session)
        
def select_disk(disk_name, disks, session):
    '''Select a disk by name.'''
    if disk_name != "":
        if disk_name[0] == '"' and (disk_name[-1] == '"' or disk_name[-1] == '*') :
            disk_name = disk_name[1:]

            if  disk_name[-1] == '"':
                disk_name = disk_name[:-1]

            session.selected_index = -1

            if disk_name.endswith("*"):
                disk_name = disk_name[:-1]
                wildcard = True
            else:
                wildcard = False
            for i, disk in enumerate(disks):  
                if disk.name == disk_name or (wildcard and disk.name.startswith(disk_name)):
                    session.selected_index = i
                    break
            if session.selected_index < 0:
                logger.info("[FCD] Disk %s not found", disk_name)
                send_data_to_P2000(f"T{P2000T_MAGENTA}Disk niet gevonden:\r\n\x00", session)
                return
            send_data_to_P2000(f"T{P2000T_GREEN}Disk:{P2000T_YELLOW}{disks[session.selected_index].name}\r\n\x00", session)
        else:
            logger.info("[FCD] No quotes around disk name")
            send_data_to_P2000(f"T{P2000T_MAGENTA}Geen geldige disk naam gegeven\r\n\x00", session)
            return
    else:
        if session.selected_index < 0:
            send_data_to_P2000(f"T{P2000T_YELLOW}Geen disk geselecteerd\r\n\x00", session)
        else:
            send_data_to_P2000(f"T{P2000T_GREEN}Disk:{P2000T_YELLOW}{disks[session.selected_index].name}\r\n\x00", session)


def load_file(file_name, disks, session):
    '''Load a file from the selected disk.'''
    if file_name.endswith("*"):
        file_name = file_name[:-1]
        wildcard = True
    else:
        wildcard = False

    marker = '"0"'                 # chr$(0) gets translated byte P2000 to "0"
    if marker in file_name:
        file_name, extension_part = file_name.split(marker, 1)
        extension = extension_part[:3].upper()
        check_extension = True
        logger.debug(f"[FLOAD] File name: {file_name}, Extension: {extension}")
        if len(extension) != 3:
            logger.info("[FLOAD]Invalid extension: %s", extension)
            send_data_to_P2000(f"T{P2000T_MAGENTA}Ongeldige extensie\r\n\x00", session)
            return
    else:
        check_extension = False

    if session.selected_index == -1:
        logger.info("[FLOAD] No disk selected; cannot load file: %s", file_name)
        send_data_to_P2000(f"T{P2000T_MAGENTA}Geen disk geselecteerd\r\n\x00", session)
        return
       
    selected_file = None
    for file in disks[session.selected_index].index:
        name_matches = file.name.rstrip() == file_name or (wildcard and file.name.startswith(file_name))
        if name_matches and (not check_extension or file.ext.upper() == extension):
            selected_file = file
            break

    if selected_file == None:
        logger.info("[FLOAD] File %s not found", file_name)
        send_data_to_P2000("M", session)  # Cassete fout M (file not found)
        return

    logger.info(f"[FLOAD] Selected file: {selected_file.name}")
    if disks[session.selected_index].type == "D":
        content = get_file_content_from_disk(disks[session.selected_index], selected_file)
    else:
        content = get_file_content_from_tape(disks[session.selected_index], selected_file)
    
    if selected_file.size != len(content):
        logger.warning("[FLOAD] File size mismatch: expected %d, got %d", selected_file.size, len(content))

    file_size = len(content)
    header = bytes((
        ord("R"),
        selected_file.startadr & 0xFF,
        (selected_file.startadr >> 8) & 0xFF,
        file_size & 0xFF,
        (file_size >> 8) & 0xFF,
        ord("R"),
    ))
    logger.debug("[LOAD] Sending load header: %s", header.hex(" ").upper())
    send_data_to_P2000(header + content, session)


def save_file(file_name, disks, session):
    '''Save a file to the selected tape save to disk in not supported.'''
    if session.selected_index == -1:
        logger.info("[FSAVE] No disk selected; cannot save file: %s", file_name)
        send_data_to_P2000(f"T{P2000T_MAGENTA}Geen cassette dir geselecteerd\r\n\x00", session)
        return

    if disks[session.selected_index].type != "C":
        logger.info("[FSAVE] Invalid media type; cannot save file: %s", file_name)
        send_data_to_P2000(f"T{P2000T_MAGENTA}Ongeldig media type (geen cass-dir)\r\n\x00", session)
        return

    if "*" in file_name:
        logger.info("[FSAVE] Wildcards not allowed: %s", file_name)
        send_data_to_P2000(f"T{P2000T_MAGENTA}Geen wildcards toegestaan\r\n\x00", session)
        return

    marker = '"0"'
    if marker in file_name:
        file_name, extension_part = file_name.split(marker, 1)
        extension = extension_part[:3].upper()
        check_extension = True
        logger.debug(f"[FSAVE] File name: {file_name}, Extension: {extension}")
        if len(extension) != 3:
            logger.info("[FSAVE] Invalid extension: %s", extension)
            send_data_to_P2000(f"T{P2000T_MAGENTA}Ongeldige extensie\r\n\x00", session)
    else:
        extension = "BAS"
        check_extension = False
  
    selected_file = None
    for file in disks[session.selected_index].index:
        if file.name.rstrip() == file_name and (not check_extension or file.ext == extension):
            selected_file = file
            break

    if disks[session.selected_index].writeprotected:
        logger.info("[FSAVE] File is write-protected; cannot save file: %s", file_name)
        send_data_to_P2000(f"G", session)  # Cassete fout G (Geen stopje)
        return
   
    logger.info("[FSAVE] Saving file %s", file_name)
    transfer = session.transfer
    transfer.received_bytes = 0
    transfer.filename = file_name
    transfer.extension = extension
    transfer.content.clear()
    transfer.filesize = 0
    transfer.start_address = 0
    transfer.receiving = True
    transfer.overwrite = False

    if selected_file != None:
        logger.info("[FSAVE] File %s found, overwrite?", selected_file.name)
        transfer.overwrite = True
        send_data_to_P2000(f"U{P2000T_GREEN}{selected_file.name:<16s} {selected_file.ext}{P2000T_CYAN}{selected_file.type}    {selected_file.blocks:-3d}{selected_file.size:-5d}\r\n\x00", session) 
        return
        
    send_data_to_P2000(f"S", session) # Instruction to start saving the file

def saving_file(rawbyte, transfer, disks, session):
    if not transfer.receiving:
        logger.warning("[FSAVE] Not currently receiving a file")
        return False

    if len(rawbyte) != 1:
        logger.error("[FSAVE] Expected one byte, got %r", rawbyte)
        transfer.receiving = False
        return False

    byte_value = rawbyte[0]
    transfer.received_bytes += 1
    logger.debug(f"Receiving byte: {byte_value:02X} (received_bytes={len(transfer.content)} OF {transfer.filesize}) [byte #{transfer.received_bytes}]")
    match transfer.received_bytes:
        case 1:
            transfer.content.clear()
            transfer.start_address = byte_value
            return True
        case 2:    
            transfer.start_address |= byte_value << 8
            return True
        case 3:
            transfer.filesize = byte_value
            return True
        case 4:
            transfer.filesize |= byte_value << 8
            return True
        case 5:
            if byte_value != ord('S'):
                logger.error("[FSAVE] Expected start of file marker 'S', got %r", rawbyte)
                transfer.receiving = False
                return False
            logger.info("[FSAVE] Start of file marker, expecting %d bytes (@ 0x%04X)", transfer.filesize, transfer.start_address)

    if transfer.received_bytes > 5:
        transfer.content.append(byte_value)
        if len(transfer.content) >= transfer.filesize:
            logger.info("[FSAVE] File received: %s (%d bytes)", transfer.filename, len(transfer.content))
            # store received content as cas image
            store_cassette_image(transfer, disks, session)
            return False

    return True

def extract_disk_block(rawdata, side, track, block, tracks=40, sides=2):
    '''
    Image is |sd0 tr0|sd1 tr0|sd0 tr2|sd1 tr2|sd0 tr3...sd0 tr39|sd1 tr39
    '''     

    total_tracks = int(len(rawdata) / TRACKSIZE)
    logger.debug("=== %d = int(len(%d) / %d) ", total_tracks, len(rawdata), TRACKSIZE)
    if total_tracks > 40:
        tracks = int(total_tracks / 2)
        sides = 2 
    else:    
        tracks = tracks
        sides = 1
    logger.debug("* Found %s disk-image with %d tracks", "DualSided" if sides == 2 else "SingleSided", tracks )

    if block > 15 or block < 0:
        logger.error("Block number %d is out of range (0-15)", block)
        return None
    if track > tracks or track < 0:
        logger.error("Track number %d is out of range (0-%d)", track, tracks)
        return None
    if side > sides or side < 0:
        logger.error("Side number %d is out of range (0-%d)", side, sides)
        return None

    offset = (track * sides * TRACKSIZE) + (side * TRACKSIZE) + (block * SECTORSIZE)
    logger.debug("=== offset = (%d * %d * %d) + (%d * %d) + (%d * %d) == %04x", track, sides,  TRACKSIZE, side, TRACKSIZE,  block, SECTORSIZE, offset)
    return rawdata[offset : offset + SECTORSIZE]

def extract_disk_indexItem(entry):
    '''
    Extract the index of the disk image
    this is listed on track 1 side 0 and track 1 side 1
     0  1  2  3  4  5  6  7  8  9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24 25 26 27 28 29 30 31
    46 72 61 78 78 6F 6E 20 2B 20 73 63 6F 72 65 73 42 41 53 42 B9 5F 47 65 00 21 00 80 00 01 02 00
    F   r  a  x  x  o  n     +     s  c  o  r  e  s  B  A  S  B  ¹  _  G  e  � !  �  € �   � 
    Each entry is 16 bytes long and contains the following information:
        Name (16 bytes),
        Extention (3 bytes) 
        Type (1 byte)
        size (2 bytes) [5FB9h]
        Start addres (2 bytes)  [6547h]
        Side (1 byte) 
        Start block (2 bytes) [0021h]
        Last block (2 bytes) [0080h]  
        3 unknown bytes

        All sectors are numbered starting from 1, (1 = track 0, sector 0), called block.
        The first block containing actual file data starts with number 33 (which corresponds to side 0, track 2, sector 0).
        (608 blocks of 256 bytes per side = 307200 bytes of file data + 32 blocks * 256 bytes for the index/system code)
    '''

    index = JWSIndexEntry(
        name=entry[0:16].decode('latin-1').rstrip(" \x00"),
        ext=entry[16:19].decode('latin-1').rstrip(" \x00"),
        type=entry[19:20].decode('latin-1'),
        size=entry[20] + (entry[21] << 8),
        startadr=entry[22] + (entry[23] << 8),
        side=entry[24],
        startblock=entry[25] + (entry[26] << 8),
        lastblock=entry[27] + (entry[28] << 8),
        blocks=0
    )
    index.blocks=(index.size + 225) // 256 
    return index
  
def extract_disk_index(diskImage):
    '''
    Extract the disk index and metadata from a JWS disk image.
    The JWS meta is located at the last 65 bytes of track 0 side 0.
        bytes (0-23) Copyright information
        bytes (28-42) version information
        bytes (43-61) disk type info

    Returns a tuple (index, metadata) where:
        index: list of index entries
        metadata: dictionary containing disk metadata
    '''
    metadata = {}
    meta_block = extract_disk_block(diskImage, side=0, track=0, block=15)

    STARTCOPYRIGHT = 0xBF
    STARTVERSION = 0xDB
    STARTVERSIONSUB = 0xE6
    STARTDISKTYPE = 0xEF
    STARTDRIVE = 0xF7
    magicdata     = meta_block[STARTDRIVE:STARTDRIVE+5].decode('ascii', errors='replace')
    if magicdata == 'drive':
        metadata['copyright']  = meta_block[STARTCOPYRIGHT:STARTCOPYRIGHT+24].decode('latin-1').rstrip(" \x00")
        metadata['version']    = meta_block[STARTVERSION:STARTVERSION+10].decode('latin-1').rstrip(" \x00")
        metadata['versionsub'] = meta_block[STARTVERSIONSUB:STARTVERSIONSUB+2].decode('latin-1').rstrip(" \x00")
        metadata['disk_type']  = meta_block[STARTDISKTYPE:STARTDISKTYPE+7].decode('latin-1').rstrip(" \x00")
    
    logger.debug ("Disk Metadata: %s", metadata)

    index = []
    # Directory entries are stored on track 1 on block 8 - 16 on side 0
    for bl in range(8, 16):
        data = extract_disk_block(diskImage, side=0, track=1, block=bl)
        if data[0] != 0x00:
            for i in range(0, len(data), 32):
                entryraw = data[i:i+32]
                if entryraw[0] != 0x00:
                    idx = extract_disk_indexItem(entryraw)
                    index.append(idx)

    # Directory entries are stored on track 1 on block 0 - 7 on side 1
    for bl in range(0, 7):
        data = extract_disk_block(diskImage, side=1, track=1, block=bl)
        if data[0] != 0x00:
            for i in range(0, len(data), 32):
                entryraw = data[i:i+32]
                if entryraw[0] != 0x00:
                    idx = extract_disk_indexItem(entryraw)
                    index.append(idx)

    for idx in index:
        logger.debug("%s %s %s %3d %7d StartAdr: %04X StartBlock: %3d LastBlock: %3d Side: %d", 
                                    idx.name, idx.ext, idx.type, idx.blocks, idx.size, idx.startadr, idx.startblock, idx.lastblock, idx.side)
    return (index, metadata)


def get_file_content_from_disk(disk, file):
    if disk.type != "D":
        logger.error("Not a disk file")
        return ""

    # print(file)
    diskimage = read_content(infilename=disk.fullname)
    data = b""
    for blocknr in range(file.startblock, file.lastblock + 1):
        # Block number starts from 1, so we need to subtract 1 when calculating track and sector.
        track = ((blocknr - 1)// BLOCKS_PER_TRACK)
        sector = (blocknr - 1) % BLOCKS_PER_TRACK

        offset = (track * 2 * TRACKSIZE) + (file.side * TRACKSIZE) + (sector * SECTORSIZE)
        logger.debug("Extracting bloknr: %d sector %d from side %d, track %d, offset == %04x" % (blocknr, sector, file.side, track, offset))
                    
        newblock = extract_disk_block(diskimage, side=file.side, track=track, block=sector)
        if len(data) + len(newblock) < file.size:
            data += newblock
        else:
            data += newblock[:file.size - len(data)]
            break

    return data
    
def collect_disk_info(infile):
    '''
       Extract a JWS disk image.
        A JWS disk layout:
        - Side 0 Track 0:   JWS system code
        - Side 0 Track 1:   Sector 0-7: JWS system code | Sector 8-15: Directory entries side 0
        - Side 0 Track 2-x: (depending on disk layout 35/40/80), Data blocks follow the directory entries

        - Side 1 Track 0:   - empty -
        - Side 1 Track 1:   Sector 0-6: Directory entries side 1
        - Side 1 Track 2-x: (depending on disk layout 35/40/80), Data blocks follow the directory entries

    '''
    logger.verbose(f"Checking disk: {infile}")
    image = read_content(infilename=infile)
    
    return extract_disk_index(image)


def get_file_content_from_tape(disk, file):
    if disk.type != "C":
        logger.error("Not a tape file")
        return ""

    logger.debug(f"Reading tape : {file}")
    tapeimage = read_content(infilename=os.path.join(disk.fullname, file.mediafilename)) 
    data = b""
    for blocknr in range(file.startblock, file.lastblock + 1):
        offset = blocknr * (0x100 + 1024)
        logger.debug("Extracting bloknr: %d  offset == %04x" % (blocknr, offset))
        block_data = tapeimage[offset + 0x100:offset + 0x100 + 1024]
        if len(data) + len(block_data) < file.size:
            data += block_data
        else:   
            data += block_data[:file.size - len(data)]
            break

    return data

def extract_casssette_index_item(infile):
    '''
    Extract the index of the cassette image each block has a header of 256 byte and a data section of 1024 bytes long.
    The header has 0x30 bytes (48 bytes) with zero's
    Followed by 0x20 (32 bytes) of real header info
    and filled with zero's to complete the 256-byte header.
        0-0x2F      : Zero's
        0x30-0x31   : Basic load address  [0x6547] (2 bytes) 
        0x32-0x33   : Total size (2 bytes)
        0x34-0x35   : Usefull program size (2 bytes)
        0x36-0x3D   : Filename 1ste part (8 bytes)
        0x3E-0x40   : Extension (3 bytes)
        0x41        : Type [B] (1 byte)
        0x42        : Land code  [N=dutch] (1 byte)
        0x43-0x45   : Start address standalone (2 bytes)
        0x44-0x46   : Load addres standalone   (2 byte)
        0x47-0x4E   : Filename 2nd part (8 bytes)
        0x4F        : Blocks count (blocks left) (1 bytes)
        0x50-0xFF:  : 176 bytes of Zero's to complete the 256-byte header
    '''
    
    bocksize = 0x100 + 1024 
    cassette = read_content(infile)
    indexlist = []
    blockcount = 0

    logger.verbose(f"Checking tape: {infile}")
    i = 0
    j = 0
    while i < len(cassette) and j < 30: # Limit to first 30 blocks for safety
        j += 1
        if i + 0x100 > len(cassette):
            break
        block_header = cassette[i:i+0x100]

        if len(block_header) < 0x100:
            break

        start_address, _, usefull_size = struct.unpack_from("<HHH", block_header, 0x30)
        
        raw_name = block_header[0x36:0x3E] + block_header[0x47:0x4F]
        name = "".join(chr(value) for value in raw_name if 0x20 <= value <= 0x7E).strip()
        extension = "".join(chr(value) for value in block_header[0x3E:0x41] if 0x20 <= value <= 0x7E).strip().upper()
        file_type = block_header[0x41]
        blocks = block_header[0x4F]
        if blocks == 0:
            logger.debug(f"Warning: {infile}: {usefull_size // 1024} blocks (+1 if > UB {(usefull_size % 1024)}).")
            blocks = ((usefull_size + 1023) // 1024) 
            if (blocks * bocksize) > len(cassette) - i:
                # Calculated blocks exceed remaining cassette size so skip this block
                logger.verbose(f"Warning: Calculated exceed remaining cassette size, skipping block. (blockcount={blockcount}, blocks={blocks})")
                continue

        # Put i on start of next block                
        i += (bocksize * blocks)

        logger.debug(f"Processing tape block {blockcount} with {blocks} blocks name {name})")
        indexlist.append(
            TapeIndexEntry(
                name=name or os.path.splitext(os.path.basename(infile))[0],
                ext=extension or "BAS",
                type=chr(file_type) if 0x20 <= file_type <= 0x7E else "B",
                size=usefull_size,
                startadr=start_address,
                lastfile=True if i+1 > len(cassette) else False,             # true if this was the file on tape
                startblock=blockcount,
                lastblock=blockcount + blocks - 1,
                blocks=blocks,
                mediafilename=os.path.basename(infile),
            )
        )
        blockcount += blocks
        
    return indexlist

def store_cassette_image(transfer, disks, session):
    '''Store the received file content  as cassette image.'''
    '''
    Store of the cassette image each block has a header of 256 byte and a data section of 1024 bytes long.
    The header has 0x30 bytes (48 bytes) with zero's
    Followed by 0x20 (32 bytes) of real header info
    and filled with zero's to complete the 256-byte header.
        0-0x2F      : Zero's
        0x30-0x31   : Basic load address  [0x6547] (2 bytes) 
        0x32-0x33   : Total size (2 bytes)
        0x34-0x35   : Usefull program size (2 bytes)
        0x36-0x3D   : Filename 1ste part (8 bytes)
        0x3E-0x40   : Extension (3 bytes)
        0x41        : Type [B] (1 byte)
        0x42        : Land code  [N=dutch] (1 byte)
        0x43-0x45   : Start address standalone (2 bytes)
        0x44-0x46   : Load addres standalone   (2 byte)
        0x47-0x4E   : Filename 2nd part (8 bytes)
        0x4F        : Blocks count (blocks left) (1 bytes)
        0x50-0xFF:  : 176 bytes of Zero's to complete the 256-byte header
    '''

    name = transfer.filename.encode("latin-1")[:16].ljust(16, b" ")
    extension = transfer.extension.encode("latin-1")[:3].ljust(3, b" ")
    
    blocks = (transfer.filesize + 1023) // 1024          # Calculate the number of 1024-byte blocks needed
    if blocks > 1:
        blocksize = 1024
    else:
        blocksize = transfer.filesize
        transfer.content = transfer.content.ljust(1024, b"\x00")

    header = bytearray(0x100)
    # Real header info (32 bytes) filled with zero's to complete the 256-byte header
    struct.pack_into( "<HHH8s3sccHH8sB", header, 
            0x30,                      # Offset for the start of the real header info
            transfer.start_address,    # Start address (2 bytes)
            transfer.filesize,         # Total size (2 bytes)
            transfer.filesize,         # Total size (2 bytes)
            name[:8],                  # Filename 1ste part (8 bytes)
            extension,                 # Extension (3 bytes)
            b"B",                      # Type [B] (1 byte)
            b"N",                      # Land code [N=dutch] (1 byte)
            transfer.filesize,         # Total size (2 bytes)
            0x0000,                    # Load addres standalone  (2 byte)
            name[8:16],                # Filename 2nd part (8 bytes)
            blocks,                    # Blocks count (blocks left) (1 byte)
        )

    indexentry = TapeIndexEntry(
            name=transfer.filename[:16].ljust(16, " "),
            ext=transfer.extension[:3].ljust(3, " "),
            type="B",
            size=transfer.filesize,
            startadr=transfer.start_address,
            lastfile=True,
            startblock=0,
            lastblock=blocks-1,
            blocks=blocks,
            mediafilename=transfer.filename+".cas",
    )

    content = bytearray()
    while blocks > 0:
        content += header + transfer.content[:1024]
        transfer.content = transfer.content[1024:]

        logger.debug("Writing block: %d remaining size = %d", blocks, len(transfer.content))

        blocks -= 1
        # Update header for next block
        header[0x4F] = blocks & 0xFF

    filenameondisk = os.path.join(disks[session.selected_index].fullname, transfer.filename+".cas")
    try:
        with open(filenameondisk, "wb") as f:
            f.write(content)
        logger.info("[FSAVE] File stored successfully: %s", filenameondisk)
    except Exception as e:
        logger.error("[FSAVE] Failed to store file %s: %s", filenameondisk, e)

    # Update the disk index with the new or modified entry
    if not transfer.overwrite:
        logger.debug("[FSAVE] Adding new index entry for file: %s", indexentry.name)
        disks[session.selected_index].index.append(indexentry)
    else:
        for i, item in enumerate(disks[session.selected_index].index):
            if item.name == indexentry.name and item.ext == indexentry.ext:
                logger.debug("[FSAVE] Updating index entry for file: %s", indexentry.name)
                disks[session.selected_index].index[i] = indexentry
                break

# ----- server handling  --------------------------------------------------------------------------------------------------------
def send_data_to_P2000(payload, session):
    """Send data to the P2000 client."""
    if not session.connected:
        return False
    if isinstance(payload, str):
        payload = payload.encode('latin-1')
    try:
        session.socket.sendall(payload)
    except OSError as error:
        logger.info("[MODEM] Send failed (%d bytes, payload=%r): %s", len(payload), payload, error)
        session.connected = False
        return False
    return True


def handle_client_receive(client_socket, disks, session, client_ip, sessions_by_ip, sessions_lock):
    """Communicaties with P2000T client."""
    command = b''
    try:
        client_socket.settimeout(1.0)
        while running and session.connected:
            try:
                raw_data = client_socket.recv(1)
                # Als recv b'' teruggeeft, heeft de emulator de verbinding verbroken!
                if raw_data == b'':
                    logger.warning("[FTP] Disconnected")
                    break
                logger.debug("[FTP] Byte received: %02x (%c)" % (raw_data[0], raw_data[0] if 32 <= raw_data[0] < 127 else ord('?')))

                if session.transfer.receiving:
                    if not saving_file(raw_data, session.transfer, disks, session):
                        if not session.transfer.receiving:
                            logger.error("[FTP] File transfer failed.")
                        else:
                            logger.info("[FTP] File transfer completed.")
                            session.transfer.receiving = False
                        break
                    if not session.transfer.receiving:
                        logger.info("[FTP] File saving completed.")
                        break
                else:
                    if raw_data in b"0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ.-_\"' *":
                        command += raw_data
                
                    if raw_data == b'\r':
                        logger.info("[FTP] Command: %s" % command.decode('latin-1'))
                        cmd = command.decode('latin-1').strip()
                        if (cmd == "FZOEK"):
                            if session.selected_index == -1:
                                send_data_to_P2000(f"T{P2000T_MAGENTA}Geen disk geselecteerd\r\n\x00", session)
                            else:
                                list_media_content(disks[session.selected_index], session)
                        elif (cmd.startswith("FCD")):
                            select_disk(cmd[3:].strip(), disks, session)

                        elif (cmd == "FDIR"):
                            list_available_media(disks, session)

                        elif (cmd.startswith("FLOAD")):
                            load_file(cmd[6:].strip().strip('"'), disks, session)

                        elif (cmd.startswith("FSAVE")):
                            save_file(cmd[6:].strip().strip('"'), disks, session)

                        elif (cmd.startswith("FBYE")):
                            send_data_to_P2000(f"T\x00", session)
                            with sessions_lock:
                                if sessions_by_ip.get(client_ip) is session:
                                    del sessions_by_ip[client_ip]
                            session.connected = False
                            
                        elif (cmd == "FHELP"):
                            send_data_to_P2000(f"T{P2000T_GREEN}Commandos:\n\r"
                                    f"{P2000T_GREEN}:#FDIR  {P2000T_YELLOW}Toon beschikare disks/cass.\n\r"
                                    f"{P2000T_GREEN}:#FZOEK {P2000T_YELLOW}Geef disk/cass. inhoud\n\r"
                                    f"{P2000T_GREEN}:#FCD\"<naam>\"   {P2000T_YELLOW}Selecteer disk/cass.\n\r"
                                    f"{P2000T_GREEN}:#FLOAD\"<naam>\" {P2000T_YELLOW}Laad bestand van srv.\n\r"
                                    f"{P2000T_GREEN}:#FSAVE\"<naam>\" {P2000T_YELLOW}Sla bestand op server\n\r"
                                    f"{P2000T_GREEN}:#FSERVERS      {P2000T_YELLOW}Toon servers\n\r"
                                    f"{P2000T_GREEN}:#FSERVER\"<url>\"{P2000T_YELLOW}Selecteer server\n\r"
                                    f"{P2000T_GREEN}:#FSERVER <nr>  {P2000T_YELLOW}Selecteer server\n\r"
                                    f"{P2000T_GREEN}:#FBYE          {P2000T_YELLOW}Stop verbinding\n\r"
                                    f"{P2000T_GREEN}:#FHELP         {P2000T_YELLOW}Toon deze help\n\r\x00", session)

                        else:
                            send_data_to_P2000(f"T{P2000T_MAGENTA}Onbekend FTP commando\r\n\x00", session)
                        command = b""
                        if not session.transfer.receiving or not session.connected:
                            break

            except socket.timeout:
                continue
            except OSError as error:
                logger.info("[FTP] Socket closed or replaced: %s", error)
                break
    except Exception:
        logger.exception("[FTP] Unexpected error")
    finally:
        session.transfer.receiving = False
        session.connected = False
        client_socket.close()


def signal_handler(sig, frame):
    """Stop server on Ctrl+C"""
    global running, server_socket
    running = False
    logger.info("[SIGNAL] Server stopped via Ctrl+C.")
    try:
        server_socket.close()
    except Exception:
        pass
    sys.exit(0) 

def server(args, disks):
    global running, server_socket
    sessions_by_ip = {}
    sessions_lock = threading.Lock()
    signal.signal(signal.SIGINT, signal_handler)
    # Start de TCP Server
    server_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)

    if args.host == "":
        try:
            args.host = socket.gethostbyname(socket.gethostname())
        except socket.gaierror:
            logger.error(f"[SERVER] Cannot discover IP address, Choose one manually with -H.")
            exit(1)

    try:
        server_socket.bind((args.host, args.port))
    except OSError as e:
        if e.errno == errno.EADDRINUSE:
            logger.error(f"[Error] Port {args.port} in use. Choose another port with -p.")
        else:
            logger.error(f"New error on {args.host} : {args.port}: {e}")
        sys.exit(1)

    # Start mDNS service for local network discovery
    service_info = ServiceInfo(
        "_ftp._tcp.local.",
        "p2000ftp._ftp._tcp.local.",
        addresses=[socket.inet_aton(args.host)],
        port=args.port,
        properties={"info": "FTP Test Server", "model": "P2000T Ftp Sever"},
        server="p2000ftp.local."
    )

    logger.info("mDNS-service registreren...")
    zeroconf.register_service(service_info)
    
    server_socket.listen()
    server_socket.settimeout(0.1)

    logger.info(f"[SERVER] Server running. Listening on port {args.port}...")

    try:
        while running:
            try:
                client_socket, client_address = server_socket.accept()
                logger.info(f"\n[SERVER] Visitor: {client_address}")
                client_ip = client_address[0]
                with sessions_lock:
                    previous_session = sessions_by_ip.get(client_ip)
                    if previous_session is None:
                        session = ClientSession(socket=client_socket, selected_index = 0 if len(disks) == 1 else -1)
                    else:
                        previous_session.connected = False
                        try:
                            previous_session.socket.shutdown(socket.SHUT_RDWR)
                        except OSError:
                            pass
                        previous_session.socket.close()
                        session = ClientSession(
                            socket=client_socket,
                            selected_index=previous_session.selected_index,
                        )
                    sessions_by_ip[client_ip] = session

                while msvcrt.kbhit():
                    msvcrt.getch()
                    
                time.sleep(1.0)
                
                client_thread = threading.Thread(
                    target=handle_client_receive,
                    args=(client_socket, disks, session, client_ip, sessions_by_ip, sessions_lock),
                    daemon=True,
                )
                client_thread.start()
                
            except socket.timeout:
                pass

    except KeyboardInterrupt:
        logger.info("[SERVER] Server stopted via Ctrl+C.")
    finally:
        running = False
        server_socket.close()

# ----- main entry point --------------------------------------------------------------------------------------------------------
def main(args):
    disks = []
    cassette_directories = {}

    logger.info("*** P2000T FTP Server ****")
    # Scan for disk image files matching the provided pattern (single, multiple files and/or wildcard patterns)
    logger.verbose(f"[START] Starting search for disk images with patterns: {args.file}")
    for pattern in args.file:
        # Seach for disk image (.dsk) files matching the current pattern AND directories containing .cas files
        logger.info(f"[START] Searching for media images in: {pattern}")

        mediaimages = glob.glob(pattern, recursive=True)
        if os.path.isdir(pattern):
            cassette_pattern = os.path.join(pattern, "**", "*.cas")
            mediaimages = glob.glob(cassette_pattern, recursive=True)
        elif not mediaimages:
            if os.path.isfile(pattern):
                mediaimages = [pattern]
            else:
                logger.debug(f"[START] Skipping missing file/pattern: {pattern}")
                continue

        cassette_root = os.path.abspath(pattern if os.path.isdir(pattern) else os.path.dirname(pattern) or ".")
        for match in mediaimages:
            if os.path.isfile(match):

                if match.lower().endswith(".cas"):
                    # Handle cassette files by creating or retrieving a MediaEntry for the containing directory
                    directory = os.path.dirname(os.path.abspath(match))
                    cassette_disk = cassette_directories.get(directory)
                    if cassette_disk is None:
                        relative_directory = os.path.relpath(directory, cassette_root)
                        cassette_name = (
                            os.path.basename(cassette_root)
                            if relative_directory == "."
                            else relative_directory.replace(os.sep, "/")
                        )
                        cassette_disk = MediaEntry(
                            fullname=directory,
                            name=cassette_name,
                            writeprotected=args.readonly,
                            index=[],
                            type="C",
                        )
                        cassette_directories[directory] = cassette_disk
                        disks.append(cassette_disk)

                    for i in extract_casssette_index_item(match):
                        cassette_disk.index.append(i)
                    continue

                
                disk = MediaEntry(fullname=match)
                disk.name = os.path.basename(disk.fullname)
                if args.readonly:
                    disk.writeprotected = True
                else:
                    disk.writeprotected = not os.access(disk.fullname, os.W_OK)
                disk.type = "D"
                (disk.index, disk.metadata) = collect_disk_info(infile=disk.fullname)
                disks.append(disk)

    if not disks:
        logger.error("[START] No valid disk images found.")
        sys.exit(1)

    logger.info(f"Starting server with {len(disks)} 'disks'")
    server(args, disks)

# ----- command line handling --------------------------------------------------------------------------------------------------
class RawFormatter(argparse.HelpFormatter):
    def _fill_text(self, text, width, indent):
        lines = "\n".join([textwrap.fill(line, width) for line in textwrap.indent(textwrap.dedent(text), indent).splitlines()]) 
        return lines
    def _split_lines(self, text, width):
        lines = super()._split_lines(text, width)
        if text.endswith('\n'):
            lines += ['']
        return lines

program_description = f'''
    FTP file server for hosting P2000T disk and cassette images. Use in combination with the P2000T M2009 modem emulator.
    '''

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=program_description, formatter_class=RawFormatter)

    parser.add_argument('--version', '-v', dest='version',      action='version',      version='%(prog)s {version} {copyright}'.format(version=_VERSION_, copyright=_COPYRIGHT_))
    parser.add_argument('--file',    '-f', dest='file',         action="extend", default=[], help='input file(s) to serve, wildcard supported', nargs="+")

    parser.add_argument('--readonly', '-r', dest='readonly',    action='store_true',   help="Do not allow modifications to the served files")
    
    parser.add_argument("--host",     '-H', dest='host',        default="",            help="Host address for the server (default: 127.0.0.1)")
    parser.add_argument("--port",     '-p', dest='port',        type=int, default=21,  help="Port for the server (default: 21)\n")

    parser.add_argument('--debug',   '-D', dest='debug',        action='store_true',   help="Show detailed and debug information")
    parser.add_argument('--verbose', '-V', dest='verbose',      action='store_true',   help="Show detailed information")
    parser.add_argument('--silent',  '-S', dest='silent',       action='store_true',   help="Show only errors and critical information")
            
    args = parser.parse_args()

    # Setup logger with correct log level
    logger = logging.getLogger('p2000tftp')
    logger.setLevel( logging.DEBUG if args.debug else \
                    (VERBOSE_LEVELV_NUM if args.verbose else \
                    (logging.ERROR if args.silent else logging.INFO)
                    ))
    if logger.level < 20:
        logging.basicConfig(format='[%(levelname)-8s] %(message)s')
    else:
        logging.basicConfig(format='%(message)s')

    if not args.file:
        parser.error("No files or directory specified.")
        parser.print_help()
        sys.exit(2)

    main(args)
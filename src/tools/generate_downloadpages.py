#
# Generates M2009 download pages from a CAS file for the P2000T computer.
#
import re
import struct
import argparse
from pathlib import Path

def _cpp_string_literal(data: bytes) -> str:
  chunks = [data[index : index + 16] for index in range(0, len(data), 16)]
  return '\n'.join(
      '    "' + ''.join(f'\\{value:03o}' for value in chunk) + '"'
      for chunk in chunks
  )

def encode_m2009_stream(data: bytes) -> bytearray:
  out = bytearray()
  for b in data:
    if b <= 0x1F:
      out.extend([0x7C, b + 0x40])
    elif b == 0x20:
      out.append(0x7B)
    elif 0x21 <= b <= 0x7A:
      out.append(b)
    elif 0x7B <= b <= 0x7F:
      out.extend([0x7D, b])
    elif 0x80 <= b <= 0xBF:
      out.extend([0x7E, b - 0x40])
    else:
      out.extend([0x7F, b - 0x80])
  return out

def calc_decoded_xor(raw_bytes: bytes) -> bytearray:
  xor_val = 0
  for b in raw_bytes:
    xor_val ^= b
  return encode_m2009_stream(bytes([xor_val]))

def build_m2009_frames(cas_path: Path | str, base_page: int = 5, output_dir: Path | str = Path('pages'), cppheaderfile: bool = False):
  Path(output_dir).mkdir(parents=True, exist_ok=True)
  pages = []

  def add_page(page_name: str, content: bytes):
    if cppheaderfile:
      pages.append((page_name, content))
    else:
      #with open(os.path.join(output_dir, f'{page_name}.bin'), 'wb') as out:
      with open(Path(output_dir) / f'{page_name}.bin', 'wb') as out:
        out.write(content)

  with open(cas_path, 'rb') as f:
    raw_cas = f.read()

  if len(raw_cas) < 0x100:
    raise ValueError('CAS-has no full 256-byte header')
  _, data_len = struct.unpack_from('<HH', raw_cas, 0x30)
  if not data_len or len(raw_cas) < 0x100 + data_len:
    raise ValueError('Invalid or incomplete program length in CAS file')
  raw_name = raw_cas[0x36:0x3e]+raw_cas[0x47:0x4E]
  prog_name = "".join(chr(b) for b in raw_name if 0x20 <= b <= 0x7E).strip()

  file_ext = "".join(chr(b) for b in raw_cas[0x3e:0x41] if 0x20 <= b <= 0x7E).strip() 
  file_type = raw_cas[0x41]
  payload = raw_cas[0x100 : 0x100 + data_len]

  chunk_size = 512
  blocks = [
      payload[i : i + chunk_size] for i in range(0, len(payload), chunk_size)
  ]
  num_blocks = len(blocks)
  if num_blocks > 25:
    raise ValueError('File too large for available page letters')

  r_raw = bytearray()
  for r in (b'*#', b'*00', b'#'):
    r_raw.append(0x20 + len(r))
    r_raw.extend(r)
  frame_r = (
      bytearray(b'R') + encode_m2009_stream(r_raw) + calc_decoded_xor(b'R' + r_raw)
  )

  end_letter = chr(ord('a') + num_blocks)

  # 1. Startpage
  page_intro = (
      f'\x1b\x43    === M2009 MODEM EMULATOR === \r\n'
      f'\x1B\x52' + f'p' * 37 + f'\r\n\r\n'
      f'\x1b\x44Bestand:\x1b\x1b\x43{prog_name:<16}\x1b\x46{file_ext:<3} {chr(file_type)}\r\n'
      f'\x1b\x44Grootte:\x1b\x46{len(payload)} bytes ({num_blocks} pagina\'s)\r\n\r\n'
      f'\x1b\x42   Druk nu op\x1B\x48SHIFT + START\x1B\x49om het\r\n'
      f'\x1b\x42      downloaden te starten.\r\n\r\n'
      f' Gebruik de FTP-client om bestanden \r\n'
      f' via wifi van en naar de P2000T te\r\n'
      f' sturen. Zorg ervoor dat de P2000T-FTP\r\n'
      f' server draait in het netwerk. Zie:\r\n\r\n'
      f' https://github.com/Bekkie/p2000t-m2009-cartridge/src/server/ftp-server\r\n\r\n'
      f' Stel het adres in via het adresboek\r\n'
      f' (type FTP) en gebruik commando\'s als:\r\n'
      f' :\x5ffzoek :\x5ffcd, :\x5ffdir, :\x5ffload en\r\n'
      f' :\x5ffsave (zie :\x5ffhelp) \r\n'
      f'\x1B\x52' + f'p' * 37 + f'\r\n'
      f'\x1b\x43Druk op 0 voor het hoofdmenu.'
  ).encode('ascii')
  add_page(str(base_page), page_intro)

  # File Header (F) preparation
  name_bytes = prog_name.encode('ascii')[:16]
  f_raw = bytearray(b'\x26P2000T')
  f_raw.append(file_type)
  f_raw.append(0x20 + len(name_bytes))
  f_raw.extend(name_bytes)
  f_raw.extend([0x01, 0x01, 0x42, num_blocks])
  f_raw.append(0x42)  # Relative load address (2 bytes)
  f_raw.extend(struct.pack('<H', 0x0000))
  f_raw.append(0x42)  # Relative start address (2 bytes)
  f_raw.extend(struct.pack('<H', 0x0000))
  f_raw.append(0x02)  # Total length (2 bytes)
  f_raw.extend(struct.pack('<H', len(payload)))

  frame_f = (
      bytearray(b'F')
      + encode_m2009_stream(f_raw)
      + calc_decoded_xor(b'F' + f_raw)
  )
  
  # Create data pages
  for idx, blk in enumerate(blocks):
    remaining = num_blocks - idx
    b_hdr_raw = bytearray([0x42, remaining])
    b_hdr_raw.extend(struct.pack('<H', len(blk)))
    b_hdr_raw.append(0x00)

    frame_b = (
        bytearray(b'B')
        + encode_m2009_stream(b_hdr_raw)
        + calc_decoded_xor( b'B'+ b_hdr_raw)
    )
    frame_b.extend(encode_m2009_stream(blk))
    frame_b.extend(calc_decoded_xor(blk))

    page_letter = chr(ord('a') + idx)
    page_content = bytearray()
    page_content.extend(b'\x1b\x43    === M2009 MODEM EMULATOR === \r\n'
          b'\x1B\x52' + b'p' * 37 + b'\r\n')

    page_content.extend(b'\x1b>')
    if idx == 0:
      page_content.extend(frame_f)
    page_content.extend(frame_b)
    page_content.extend(frame_r)
    page_content.extend(b'\x1b?')
    
    add_page(f'{base_page}{page_letter}', page_content)

  # Success page
  page_outro = (
      f'\x1b\x43    === M2009 MODEM EMULATOR === \r\n'
      f'\x1B\x52' + f'p' * 37 + f'\r\n\r\n'
      f'\r\n\x1b\x42\x1b\x4d      DOWNLOAD VOLTOOID\r\n\r\n\r\n'
      f'\x1b\x47Het programma ({len(payload)} bytes) is\r\n'
      f' in het geheugen opgeslagen.\r\n\r\n'
      f' Stop het terminal programma tijdelijk\r\n'
      f' met <STOP> en bewaar het ftp-programma\r\n'
      f' op disk of cassette.\r\n\r\n'
      f' Keer terug in de terminal met <START>.\r\n\r\n\r\n' 
      f'\x1b\x43Druk op 0 voor het hoofdmenu.\r\n\r\n\r\n\r\n\r'
      f'\x1B\x52' + f'p' * 37
  ).encode('ascii')
  add_page(f'{base_page}{end_letter}', page_outro)

  if cppheaderfile:
    header_path = Path(output_dir) / 'download_pages.h' # os.path.join(output_dir, 'download_pages.h')
    with open(header_path, 'w', encoding='ascii', newline='\n') as out:
      out.write('#pragma once\n#include <string>\n\n')
      out.write('// DO NOT EDIT THIS FILE IT IS AUTO-GENERATED BY: generate_downloadpages.py\n\n')

      for page_name, content in pages:
        symbol = re.sub(r'[^A-Za-z0-9_]', '_', f'PAGE_{page_name.upper()}')
        out.write(f'static const std::string {symbol} = "\\014" \n')  # 0xC = Form Feed is octal \014
        out.write(_cpp_string_literal(content))
        out.write(';\n\n')

      out.write(f'#define NUM_OF_DOWNLOAD_PAGES {len(pages)}\n')
      out.write(f'static const std::string* const DOWNLOAD_PAGES[NUM_OF_DOWNLOAD_PAGES] = {{\n')
      for page_name, content in pages:
        symbol = re.sub(r'[^A-Za-z0-9_]', '_', f'PAGE_{page_name.upper()}')
        out.write(f'  &{symbol},\n')
      out.write('};\n\n')

  output_type = 'download_pages.h' if cppheaderfile else '.bin files'
  print(f"Done! Pages {base_page} till {base_page}{end_letter} created as {output_type}.")
  

program_description = f'''
    Generate Viedata download pages from a given .cas file (used for the P2000T M2009 emulator)
    '''

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=program_description)

    parser.add_argument('--file',       '-f', dest='file',       type=Path,  required=True,    help='Input .cas file to convert')
    parser.add_argument('--base-page',  '-b', dest='base_page',  type=int,   default=5,        help='Base page number for the download pages (default: %(default)s)')
    parser.add_argument('--cppheader',  '-c', dest='cppheader',  action='store_true',          help='Generate C++ header file with download pages, instead of binary files')
    parser.add_argument('--output-dir', '-o', dest='output_dir', type=Path,  required=True,   help='Output directory for the generated download pages')
            
    args = parser.parse_args()


    #build_m2009_frames('ftp.cas', base_page=5, output_dir='pages', cppheaderfile=False)
    build_m2009_frames(
        args.file,
        base_page=args.base_page,
        output_dir=args.output_dir,
        cppheaderfile=args.cppheader
    )



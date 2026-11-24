'''
MyViewdataServer - Python socket server for M2009 Videotex Emulator / Viditel Modem Emulator
Author: Bart Eversdijk
Description: Handles client connections and serves Videotex pages.

Server some internal pages for demo purposes.
Serves all *.bin files in the ./pages/ directory, these files should contain the binary representation of Videotex pages.
Files should be named according to the page number they represent, e.g., 100.bin for page 100. and 100b.bin for page 100b. etc.
'''

import socket
import threading
import time
import msvcrt
import signal
import sys
import os
import argparse
import errno
from zeroconf import ServiceInfo, Zeroconf

# Globale variabele om de actieve client bij te houden
active_client = None
running = True
zeroconf = Zeroconf()

# --- VIDEOTEX PAGINA DEFINITIES ---
PAGINA_HOOFDMENU = (
    b'\x0C'
    b'\x1B\x43\x1B\x4E   === DEMO VIDEOTEX HOST ===\r\n'
    b'\x1B\x52' + b'p' * 37 + b'\x1B\x47\r\n'
    b' \r\n\r\n'
    b'    \x1B\x4D Welkom op de M2009 server!\r\n\r\n'
    b' \r\n\r\n'
    b'\x1B\x43Maak een keuze uit het menu:\r\n\r\n'
    b'\x1B\x42 1 \x1B\x47- Toon SAA5050 Kleurentest\r\n'
    b'\x1B\x42 2 \x1B\x47- Help & Systeeminformatie\r\n'
    b'\x1B\x42 3 \x1B\x47- Wis het scherm\r\n\r\n'
    b'\x1B\x42 9 \x1B\x47- Verbinding verbreken\r\n\r\n'
    b'\x1B\x52' + b'p' * 37 + b'\x1B\x47\r\n'
    b'\x1B\x46Geef uw keuze aub... \x1B\x47'
)

PAGINA_KLEUREN = (
    b'\x0C'
    b'\x1B\x43\x1B\x4E      SAA5050 KLEURENTEST      \r\n\x1B\x4F\r\n'
    b'\x1B\x41Rood (\x1B\x470x01\x1B\x41)       \x1B\x51Grafisch Rood\r\n'
    b'\x1B\x42Groen (\x1B\x470x02\x1B\x42)      \x1B\x52Grafisch Groen\r\n'
    b'\x1B\x43Geel (\x1B\x470x03\x1B\x43)       \x1B\x53Grafisch Geel\r\n'
    b'\x1B\x44Blauw (\x1B\x470x04\x1B\x44)      \x1B\x54Grafisch Blauw\r\n'
    b'\x1B\x45Magenta (\x1B\x470x05\x1B\x45)    \x1B\x55Grafisch Magenta\r\n'
    b'\x1B\x46Cyaan (\x1B\x470x06\x1B\x46)      \x1B\x56Grafisch Cyaan\r\n'
    b'\x1B\x47Wit (\x1B\x470x07\x1B\x47)        \x1B\x57Grafisch Wit\r\n\r\n'
    b'\x1B\x43Druk op 0 voor het Hoofdmenu.'
)

PAGINA_HELP = (
    b'\x0C'
    b'\x1B\x46\x1B\x4E     VIDEOTEX HELP & INFO     \r\n\x1B\x4F\r\n'
    b'Je bent succesvol verbonden!!\r\n'
    b'De data wordt live gegenereerd door de\r\n'
    b'Python socket server via poort 8080.\r\n\r\n'
    b'\x1B\x43Druk op 0 voor het Hoofdmenu.'
)

def add_even_parity(byte_value):
    """Bereken Even Parity over de volledige 7 databits (bit 0 t/m 6)."""
    clean_value = byte_value & 0x7F
    count = 0
    for i in range(7):
        if (clean_value >> i) & 1:
            count += 1
    if count % 2 != 0:
        return clean_value | 0x80
    return clean_value

def send_slowly(client_socket, payload):
    """Stuurt de data byte voor byte met stabiele Even Parity en ademruimte."""
    parity_payload = bytearray()
    for b in payload:
        parity_payload.append(add_even_parity(b))
        
    for i in range(len(parity_payload)):
        byte_to_send = parity_payload[i:i+1]
        try:
            client_socket.sendall(byte_to_send)
            time.sleep(0.004)
        except Exception:
            break

def handle_client_receive(client_socket):
  """Vangt data op van de P2000T client en handelt Viewdata-paginering (#, *# en *00) af."""
  global active_client
  loopconunter = 0
  command_buffer = bytearray()
  last_command = ""
  command_start = False

  try:
      client_socket.settimeout(1.0)
      while running:
          try:
              raw_data = client_socket.recv(1)
              if not raw_data:
                  print("[MODEM] Emulator heeft opgehangen (verbinding verbroken).")
                  break

              print(f"[MODEM] Byte ontvangen: {ord(raw_data):02x}")
              clean_byte = bytes([ord(raw_data) & 0x7F])

              if command_start:
                if clean_byte == b"*":
                  command_buffer.clear()

                elif clean_byte in (b"\r", b"_", b"#") or \
                  ( clean_byte == b"0" and command_buffer == b"0"):
                  if clean_byte == b"0":
                    command_buffer.append(ord(clean_byte))

                  command_start = False
                  command_str = command_buffer.decode("ascii", errors="ignore")
                  command_buffer.clear()
                  print("[MODEM] Volledige command ontvangen:", command_str)

                  if command_str == "00":
                      if last_command and last_command.isdigit():
                          last_command = last_command + "a"
                          loopconunter = 0
                          print(f"[MODEM] Start download vanaf {last_command[:-1]} ->"f" {last_command}")
                      else:
                          print("[MODEM] *00 ontvangen (herhaal huidige pagina)")
                          loopconunter += 1
                          if loopconunter > 5:
                              print("[MODEM] Te veel herhalingen, stop.")
                              break
                      filename = os.path.join("pages", last_command + ".bin") if last_command else ""
                  elif command_str == "" and last_command:
                      # '*#' ontvangen: ga door naar 501a, 501b, ...
                      loopconunter = 0
                      if last_command.isdigit():
                          last_command = last_command + "a"
                      else:
                          last_command = last_command[:-1] + chr(ord(last_command[-1]) + 1)
                      print(f"[MODEM] *# ontvangen -> volgende pagina: {last_command}")
                      filename = os.path.join("pages", last_command + ".bin")
                  else:
                      loopconunter = 0
                      last_command = command_str
                      filename = os.path.join("pages", last_command + ".bin")

                  if filename and os.path.exists(filename):
                      print(f"[MODEM] Bestand {filename} bestaat.")
                      with open(filename, "rb") as f:
                          content = f.read()
                      send_slowly(client_socket, b"\x0c")
                      send_slowly(client_socket, content)
                  else:
                      print(f"[MODEM] Bestand {filename} bestaat niet.")
                else:
                  command_buffer.append(ord(clean_byte))
                  print(f"[MODEM] command {command_buffer}")

              else:
                if clean_byte == b"*":
                  print("[MODEM] Start of command")
                  command_buffer.clear()
                  command_start = True
                elif clean_byte in (b"#", b"_") and last_command:
                  loopconunter = 0
                  if last_command.isdigit():
                      last_command = last_command + "a"
                  else:
                      last_command = last_command[:-1] + chr(ord(last_command[-1]) + 1)
                  filename = os.path.join("pages", last_command + ".bin")
                  if os.path.exists(filename):
                      print(f"[MODEM] Volgende pagina via #: {filename}")
                      with open(filename, "rb") as f:
                        content = f.read()
                      send_slowly(client_socket, b"\x0c")
                      send_slowly(client_socket, content)
                  else:
                      print(f"[MODEM] Einde reeks bereikt ({filename}).")

                elif clean_byte == b"1":
                    send_slowly(client_socket, PAGINA_KLEUREN)

                elif clean_byte == b"2":
                    send_slowly(client_socket, PAGINA_HELP)

                elif clean_byte == b"3":
                    send_slowly(client_socket, b"\x0C\x1B\x47Scherm gewist. Druk 0 voor menu.")

                elif clean_byte == b"4":
                  send_slowly(client_socket, b"\x0CPing.\n\r")

                elif clean_byte == b"0":
                  send_slowly(client_socket, PAGINA_HOOFDMENU)

                elif clean_byte == b"9":
                  print("\n[MODEM] Verbinding door gebruiker via menu verbroken.")
                  afscheid = (
                      b"\x0C\x1B\x47Verbinding verbreken...\r\n\r\nNO CARRIER\r\n"
                  )
                  send_slowly(client_socket, afscheid)
                  time.sleep(1)
                  break

          except socket.timeout:
            continue
          except (ConnectionResetError, ConnectionAbortedError):
            print("\n[MODEM] Emulator verbinding hard gereset.")
            break
  except Exception as e:
    print(f"[SERVER] Onverwachte fout: {e}")
  finally:
    if active_client == client_socket:
      active_client = None
      print("[SERVER] Status gereset: Wachten op volgende inbelpoging...")
      while msvcrt.kbhit():
        msvcrt.getch()
    client_socket.close()



def signal_handler(sig, frame):
    """Sluit de server direct en geruisloos af bij Ctrl+C zonder tracebacks te dumpen."""
    global running, server_socket, active_client
    running = False
    print("\nServer handmatig gestopt via Ctrl+C.")
    if active_client:
        try:
            active_client.close()
        except Exception:
            pass
    try:
        server_socket.close()
    except Exception:
        pass
    sys.exit(0) # Sluit het Python-proces onmiddellijk succesvol af


if __name__ == "__main__":  
    program_description = "Demo Viewdata host server. Zorg dat de firewall staat open voor de gekozen poort en dat verbindingen accepteerd worden."

    parser = argparse.ArgumentParser(description=program_description)
    parser.add_argument("--ip",       '-i', dest='IP',          default="",              help="Gebruik dit al ip addressa (default: eigen ip adres)")
    parser.add_argument("--port",     '-p', dest='port',        type=int, default=8080,    help="Poort nummer voor modem (default: %(default)s)\n")
    args = parser.parse_args()

    signal.signal(signal.SIGINT, signal_handler)

    # Start de TCP Server
    server_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    if args.IP == "":
        ipaddress = socket.gethostbyname(socket.gethostname())
    else:
        ipaddress = args.IP

    try:
        server_socket.bind((ipaddress, args.port))
    except OSError as e:
        if e.errno == errno.EADDRINUSE:
            print(f"Fout: Poort {args.port} is al in gebruik. Kies een andere poort met -p.")
        else:
            print(f"Netwerkfout bij starten van server op poort {args.port}: {e}")
        sys.exit(1)

    # Start mDNS service for local network discovery
    service_info = ServiceInfo(
        "_http._tcp.local.",
        "Viewdata-Server._http._tcp.local.",
        addresses=[socket.inet_aton(ipaddress)],
        port=args.port,
        properties={"info": "Viewdata Test Server", "model": "Pyton Sever"},
        server="viewdata-server.local."
    )

    print("mDNS-service registreren...")
    zeroconf.register_service(service_info)

    server_socket.listen()
    server_socket.settimeout(0.1)

    print(f"Server draait. Wachten op verbinding op {ipaddress}:{args.port}...")

    try:
        # DE LUS STAAT NU STRAC IN HET INTRY-BLOK OM TRACEBACKS TE VOORKOMEN
        while running:
            try:
                client_socket, client_address = server_socket.accept()
                print(f"\n[VERBONDEN] Client aangesloten: {client_address}")
                
                while msvcrt.kbhit():
                    msvcrt.getch()
                    
                active_client = client_socket
                time.sleep(1.0)
                #time.sleep(0.1) 
                send_slowly(client_socket, PAGINA_HOOFDMENU)
                #send_slowly(client_socket, b"\x0cHi!")
                
                client_thread = threading.Thread(target=handle_client_receive, args=(client_socket,), daemon=True)
                client_thread.start()
                
            except socket.timeout:
                pass

            # Handmatige live invoer vanaf de server-terminal
            if active_client:
                if msvcrt.kbhit():
                    char = msvcrt.getch()
                    payload = b''
                    
                    if char in (b'\x00', b'\xe0'):
                        speciaal = msvcrt.getch()
                        
                        # CORRECTIE F1 TOETS: Zowel onder prefix 0x00 als 0xE0 stabiel afvangen
                        if speciaal == b';':      # F1 scancode
                            payload = b'\x0C'     # Clear Screen
                        elif speciaal == b'H':    # Pijl omhoog
                            payload = b'\x0B'
                        elif speciaal == b'P':    # Pijl omlaag
                            payload = b'\x0A'
                        elif speciaal == b'K':    # Pijl links
                            payload = b'\x08'
                        elif speciaal == b'M':    # Pijl rechts
                            payload = b'\x09'
                    else:
                        if char == b'\r':
                            payload = b'\r\n'
                        elif char == b'\x08':
                            payload = b'\x08'
                        else:
                            payload = char

                    if payload:
                        try:
                            parity_payload = bytearray([add_even_parity(b) for b in payload])
                            active_client.sendall(parity_payload)
                        except Exception:
                            pass

    except KeyboardInterrupt:
        print("\nServer handmatig gestopt via Ctrl+C.")
    finally:
        running = False
        if active_client:
            active_client.close()
        server_socket.close()


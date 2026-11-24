'''  
Simuleer P2000T Modem Client, produceerd een incommende oproep naar de M2009 / Viditel modem
'''

from select import select
import socket
import time
import msvcrt
import argparse

program_description = "Simuleer P2000T Modem Client, produceerd een incommende oproep naar de M2009 / Viditel modem"

parser = argparse.ArgumentParser(description=program_description)
parser.add_argument('--server',   '-s', dest='server',      default='m2009.local',   help="Server adres voor de M2009/Viditel emulator (default: m2009.local of viditel.local)")
parser.add_argument("--ip",       '-i', dest='IP',          default="",              help="Gebruik IP ipv hostnaam (default: wordt server adres gebruikt)")
parser.add_argument("--port",     '-p', dest='port',        type=int, default=23,    help="Poort nummer voor modem (default: 23)\n")
        
args = parser.parse_args()

if args.IP:
    EMU_IP = args.IP
else:
    try:
        EMU_IP = socket.gethostbyname(args.server)
    except socket.gaierror:
        print(f"\n[FOUT] Kan de hostnaam {args.server} niet vinden.")
        exit(1)
EMU_PORT = args.port

WELKOM_MSG = b'\x0C\x1B\x43\x1B\x4E  INKOMENDE OPROEP SUCCES!  \r\n\x1B\x47\x1B\x4FTyp hier live in de pc-terminal...\r\n'

def add_even_parity(byte_value):
    clean_value = byte_value & 0x7F
    count = sum((clean_value >> i) & 1 for i in range(7))
    return clean_value | 0x80 if count % 2 != 0 else clean_value & 0x7F

print(f"Modem beller: Verbinding zoeken met P2000T op {EMU_IP}:{EMU_PORT}...")

try:
    # Maak verbinding met de socket-server in MAME
    client_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    client_socket.connect((EMU_IP, EMU_PORT))
    print("\n[BEL] De P2000T rinkelt nu! Neem de lijn op via de P2000T software...")
    
    # Wacht tot de P2000T off-hook is gegaan en stuur het menu
    time.sleep(2.0)
    #client_socket.sendall(bytearray([add_even_parity(b) for b in WELKOM_MSG]))
    print("\n[ONLINE] Verbinding actief! Begin te typen. Druk 'Esc' om te stoppen.")

    while True:
        if msvcrt.kbhit():
            char = msvcrt.getch()
            if char == b'\x1b':
                break
            payload = b'\r\n' if char == b'\r' else char
            client_socket.sendall(bytearray([add_even_parity(b) for b in payload]))

        readable, _, _ = select([client_socket], [], [], 0.05)
        if readable:
            data = client_socket.recv(4096)
            if not data:  # peer heeft de verbinding gesloten
                break
            clean = bytes(b & 0x7F for b in data)  # parity-bit verwijderen
            text = ''.join(chr(b) for b in clean if b in (0x0A, 0x0D) or 0x20 <= b < 0x7F)
            print(text, end='', flush=True)
            

except ConnectionRefusedError:
    print("\n[FOUT] Verbinding geweigerd. Start de P2000T emulator eerst op!")
finally:
    client_socket.close()

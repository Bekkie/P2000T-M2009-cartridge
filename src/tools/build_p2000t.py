# pyright: reportUndefinedVariable=false
import os
import subprocess

import generate_downloadpages as pagegenerator
Import("env")

print("--- START P2000T Z80 BUILD ---")

project_dir = env.subst("$PROJECT_SRC_DIR")

# Define source and output locations (including the 'ftpclient' submap)
src_file = os.path.join(project_dir, "ftpclient", "ftp-client.asm")
out_file = os.path.join(project_dir, "ftpclient", "ftp-client.cas")
page_dir = os.path.join(project_dir, "m2009-emulator")
z80asm_exe = os.path.join(project_dir, "tools", "z80asm.exe")

if not os.path.exists(z80asm_exe):
    print(f"ERROR: z80asm.exe not found at {z80asm_exe}")
    print("Please download it from https://www.ticalc.org/archives/files/fileinfo/168/16866.html")
    print("Install it in the <project_dir>/tools directory.")
    env.Exit(1)

def build_cas():
    ftp_dir = os.path.join(project_dir, "ftpclient")
    bin_file = os.path.join(ftp_dir, "ftp-client.bin")
    
    # Remove old files first
    for f in [bin_file, out_file]:
        if os.path.exists(f):
            os.remove(f)

    # Run z80asm INSIDE the ftpclient directory, using only the short filename 'ftp-client.asm'
    cmd = [z80asm_exe, "-b", "-ns", "-nm", "ftp-client.asm"]
    print(f"Building P2000T cassfile in {ftp_dir}...")
    
    result = subprocess.run(cmd, cwd=ftp_dir, capture_output=True, text=True)
    if result.returncode != 0:
        print("Z80ASM ERROR:")
        print(result.stdout)
        print(result.stderr)
        env.Exit(1)

    # Rename the created ftp-client.bin to ftp-client.cas
    if os.path.exists(bin_file):
        os.rename(bin_file, out_file)
        print(f"Done! File saved as: {out_file} ({os.path.getsize(out_file)} bytes)")
    else:
        print("ERROR: ftp.bin not created by z80asm.")
        print("STDOUT:", result.stdout)
        print("STDERR:", result.stderr)
        env.Exit(1)

# Call the build function DIRECTLY (not via buildprog)
build_cas()

print("--- GENERATE DOWNLOAD PAGES ---")
pagegenerator.build_m2009_frames(out_file, base_page=5, output_dir=page_dir, cppheaderfile=True)

# Check if the user has clicked on 'Upload'
if env["PIOENV"] == "ftpclient" and "upload" in COMMAND_LINE_TARGETS:
    m2000_exe = r"C:\Program Files\M2000\M2000.exe"
    print(f"Start M2000 emulator with {out_file}...")
    subprocess.run([m2000_exe, out_file])

print("--- P2000T Z80 BUILD DONE ---")
if env["PIOENV"] == "ftpclient":
    env.Exit(0)

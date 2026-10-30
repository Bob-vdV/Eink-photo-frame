#!/usr/bin/env python3
"""
upload_images.py

Push packed-4bpp Spectra 6 images into the onboard QSPI flash of a
XIAO nRF52840 Plus ePaper board running epaper_gallery.ino.

    pip install pyserial

Examples
--------
List what is already stored:
    python upload_images.py --list

Probe the board:
    python upload_images.py --probe

Upload one image into slot 0 and show it:
    python upload_images.py photo.bin --slot 0 --show

Upload several images into consecutive slots:
    python upload_images.py a.bin b.bin c.bin

Display slot 2:
    python upload_images.py --show-slot 2

Delete slot 1:
    python upload_images.py --delete-slot 1

Wipe the filesystem:
    python upload_images.py --format
"""

import argparse
import glob
import struct
import sys
import time
from pathlib import Path

try:
    import serial
except ImportError:
    sys.exit("pyserial is required:  pip install pyserial")


IMG_BYTES = 800 * 480 // 2          # 192000
MAX_IMAGES = 8
BAUD = 115200
CHUNK = 2048                        # Reduced from 4096 for reliability
CHUNK_TIMEOUT = 0.1                 # seconds between chunks


def find_port():
    """Try common Linux/Unix/macOS serial port patterns."""
    for pattern in ("/dev/ttyACM*", "/dev/ttyUSB*", "/dev/cu.usbmodem*"):
        hits = sorted(glob.glob(pattern))
        if hits:
            return hits[0]
    return None


def open_board(port=None, settle=2.0):
    port = port or find_port()
    if not port:
        sys.exit("No serial port found. Is the board plugged in?")

    try:
        ser = serial.Serial(port, BAUD, timeout=0.5, write_timeout=None)
    except serial.SerialException as exc:
        sys.exit(f"Cannot open {port}: {exc}\n"
                 f"If this is a permissions problem:\n"
                 f"    sudo usermod -aG dialout $USER   # then log out and back in")

    print(f"connected to {port}")

    # Drain any output from reset
    deadline = time.time() + settle
    while time.time() < deadline:
        ser.readline()

    ser.reset_input_buffer()
    ser.write(b"\n")
    ser.flush()
    time.sleep(0.3)
    ser.reset_input_buffer()

    return ser


def read_line(ser, timeout=5.0):
    """Read one newline-terminated line, or return '' on timeout."""
    deadline = time.time() + timeout
    buf = bytearray()
    while time.time() < deadline:
        b = ser.read(1)
        if not b:
            continue
        if b == b"\n":
            return buf.decode(errors="replace").strip()
        if b != b"\r":
            buf.extend(b)
    return buf.decode(errors="replace").strip()


def drain_lines(ser, timeout=0.5):
    """Read all pending lines until quiet."""
    lines = []
    deadline = time.time() + timeout
    while time.time() < deadline and ser.in_waiting:
        line = read_line(ser, timeout=0.1)
        if line:
            lines.append(line)
    return lines


def send_command(ser, cmd, timeout=5.0):
    """Send a line command and collect all response lines."""
    ser.reset_input_buffer()
    ser.write(cmd.encode() + b"\n")
    ser.flush()

    lines = []
    deadline = time.time() + timeout
    while time.time() < deadline:
        line = read_line(ser, timeout=max(0.1, deadline - time.time()))
        if line:
            lines.append(line)
        elif not ser.in_waiting:
            break
    return lines


def cmd_list(ser):
    for line in send_command(ser, "L"):
        print(line)


def cmd_probe(ser):
    for line in send_command(ser, "P"):
        print(line)


def cmd_format(ser):
    print("WARNING: this erases every stored image.")
    reply = input("Type 'yes' to continue: ").strip().lower()
    if reply != "yes":
        print("aborted")
        return
    print("Formatting filesystem...")
    for line in send_command(ser, "F", timeout=60.0):
        print(line)


def cmd_delete(ser, slot):
    for line in send_command(ser, f"X{slot}"):
        print(line)


def cmd_show(ser, slot):
    for line in send_command(ser, f"D{slot}", timeout=120.0):
        print(line)


def cmd_upload(ser, slot, path, show=False):
    """Upload an image file to a specific slot."""
    data = Path(path).read_bytes()

    if len(data) != IMG_BYTES:
        print(f"  !! {path}: expected {IMG_BYTES} bytes, got {len(data)}")
        print(f"     Did you convert with spectra6_convert.py --raw ?")
        return False

    checksum = sum(data) & 0xFFFFFFFF

    # Start the upload session
    ser.reset_input_buffer()
    ser.write(f"U {slot}\n".encode())
    ser.flush()

    # Wait for READY
    ready = read_line(ser, timeout=10.0)
    if not ready.startswith("READY"):
        print(f"  !! board refused upload: {ready!r}")
        # Try to resync
        print("  Attempting resync...")
        lines = drain_lines(ser, timeout=1.0)
        for l in lines:
            print(f"  stray: {l}")
        return False

    print(f"  Sending length header...")
    ser.write(struct.pack("<I", len(data)))
    ser.flush()

    # Stream payload in chunks
    sent = 0
    t0 = time.time()
    while sent < len(data):
        chunk = data[sent:sent + CHUNK]
        n = ser.write(chunk)
        ser.flush()
        sent += n

        pct = sent * 100 // len(data)
        elapsed = time.time() - t0
        rate = sent / max(elapsed, 0.001) / 1024
        print(f"\r  {pct:3d}%  {sent}/{len(data)} bytes  ({rate:.0f} KB/s)", end="", flush=True)

        # Small pause between chunks to let UART drain
        time.sleep(CHUNK_TIMEOUT)

    print()

    print(f"  Sending checksum...")
    ser.write(struct.pack("<I", checksum))
    ser.flush()

    # Wait for result
    result = read_line(ser, timeout=30.0)

    print(f"  {result}")

    if not result.startswith("OK"):
        # Try to drain any additional output
        lines = drain_lines(ser, timeout=1.0)
        for l in lines:
            print(f"  extra: {l}")
        return False

    if show:
        cmd_show(ser, slot)

    return True


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main():
    ap = argparse.ArgumentParser(
        description="Upload 4bpp images to the ePaper board's QSPI flash.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    ap.add_argument("images", nargs="*", help=".bin files to upload")
    ap.add_argument("-p", "--port", help="serial port (autodetected)")
    ap.add_argument("-s", "--slot", type=int, default=0,
                    help="starting slot for uploads (default 0)")
    ap.add_argument("--show", action="store_true",
                    help="display each image after uploading it")
    ap.add_argument("--show-slot", type=int, metavar="N",
                    help="display slot N and exit")
    ap.add_argument("--delete-slot", type=int, metavar="N",
                    help="delete slot N and exit")
    ap.add_argument("--list", action="store_true", help="list stored images")
    ap.add_argument("--probe", action="store_true", help="run board diagnostics")
    ap.add_argument("--format", action="store_true",
                    help="erase the whole filesystem (asks for confirmation)")
    args = ap.parse_args()

    if not (args.images or args.list or args.probe or args.format
            or args.show_slot is not None or args.delete_slot is not None):
        ap.print_help()
        return

    ser = open_board(args.port)

    try:
        if args.probe:
            cmd_probe(ser)

        if args.list:
            cmd_list(ser)

        if args.format:
            cmd_format(ser)

        if args.delete_slot is not None:
            cmd_delete(ser, args.delete_slot)

        if args.show_slot is not None:
            cmd_show(ser, args.show_slot)

        if args.images:
            slot = args.slot
            failures = 0

            for path in args.images:
                if slot >= MAX_IMAGES:
                    print(f"slot {slot} exceeds MAX_IMAGES ({MAX_IMAGES}); stopping")
                    break

                print(f"[slot {slot}] {path}")
                if not cmd_upload(ser, slot, path, show=args.show):
                    failures += 1
                slot += 1

            print()
            cmd_list(ser)

            if failures:
                sys.exit(f"{failures} upload(s) failed")

    finally:
        ser.close()


if __name__ == "__main__":
    main()
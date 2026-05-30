"""Read N seconds of serial output from COM3, with optional DTR/RTS reset.
Used to capture ESP32-S3 boot logs from a non-interactive shell.
"""
import serial
import sys
import time

PORT = "COM3"
BAUD = 115200
SECONDS = 14

s = serial.Serial(PORT, BAUD, timeout=0.3)

# Try a DTR/RTS reset. On ESP32-S3 USB-CDC this may be a no-op (the JTAG/CDC
# bridge does not route DTR/RTS to EN/IO0), but it doesn't hurt.
try:
    s.setDTR(False)
    s.setRTS(True)
    time.sleep(0.15)
    s.setRTS(False)
    time.sleep(0.05)
    s.setDTR(True)
except Exception:
    pass

end = time.time() + SECONDS
while time.time() < end:
    chunk = s.read(4096)
    if chunk:
        sys.stdout.buffer.write(chunk)
        sys.stdout.flush()

s.close()

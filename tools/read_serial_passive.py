"""Read N seconds of serial output from COM3 WITHOUT resetting the board.
Used to capture touch events while the user interacts with the screen.
"""
import serial
import sys
import time

PORT = "COM3"
BAUD = 115200
SECONDS = 30

s = serial.Serial(PORT, BAUD, timeout=0.3)
# IMPORTANT: do NOT toggle DTR/RTS here. We want to observe the live system,
# not reboot it.

end = time.time() + SECONDS
while time.time() < end:
    chunk = s.read(4096)
    if chunk:
        sys.stdout.buffer.write(chunk)
        sys.stdout.flush()

s.close()

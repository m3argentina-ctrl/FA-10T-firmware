"""Captura extra-larga (90s) sin reset."""
import serial, sys, time

s = serial.Serial("COM3", 115200, timeout=0.3)
end = time.time() + 90
while time.time() < end:
    chunk = s.read(4096)
    if chunk:
        sys.stdout.buffer.write(chunk)
        sys.stdout.flush()
s.close()

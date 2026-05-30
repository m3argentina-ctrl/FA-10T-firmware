"""Monitor serial continuo de COM3. Cortar con Ctrl+C."""
import serial
import sys
import time

s = serial.Serial("COM3", 115200, timeout=0.3)
print("=== Monitor COM3 activo. Ctrl+C para salir. ===", flush=True)
try:
    while True:
        chunk = s.read(4096)
        if chunk:
            sys.stdout.buffer.write(chunk)
            sys.stdout.flush()
        else:
            time.sleep(0.05)
except KeyboardInterrupt:
    print("\n=== Cerrado por usuario. ===", flush=True)
finally:
    s.close()

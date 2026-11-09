import serial
import time

s = serial.Serial('COM5', 115200, timeout=0.1)
# Toggle RTS to reset ESP32
s.dtr = False
s.rts = True
time.sleep(0.1)
s.rts = False
time.sleep(0.1)

t0 = time.time()
while time.time() - t0 < 6:
    line = s.readline()
    if line:
        try:
            txt = line.decode('utf-8', errors='replace').rstrip()
        except Exception:
            txt = repr(line)
        print(f"[{time.time()-t0:6.3f}s] {txt}")
s.close()

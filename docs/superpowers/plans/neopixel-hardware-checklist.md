# NeoPixel bring-up checklist (PS2 build, per-port)

Wiring: WS2812 DIN(port 1) -> GPIO12, DIN(port 2) -> GPIO15; 5V + GND; ~300-500R
series on each DIN, decoupling cap recommended. These are the former analog-mode
LED pins (ESP32 strapping pins — a first-frame boot glitch is possible, cosmetic).

- [ ] Power on, no controllers: both pixels breathe **amber**.
- [ ] Enter pairing / put a controller in discoverable mode: both pixels pulse **blue**.
- [ ] Connect a controller to port 1: pixel 1 turns **green**; pixel 2 stays amber.
- [ ] Toggle that pad to analog mode: pixel 1 goes **steady/bright green**; digital = gentle breathe.
- [ ] Connect a second controller to port 2: pixel 2 turns green independently.
- [ ] Force an init fault (e.g. FS/NVS): both pixels blink **red** rapidly and latch.
- [ ] On-board error LED still pulses/behaves exactly as before (unchanged).
- [ ] PS2 controller input + analog-mode toggling still work over the wire
      (the discrete analog LED is gone, but the protocol behavior is unchanged); no I/O regression.

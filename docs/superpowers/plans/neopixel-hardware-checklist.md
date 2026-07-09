# NeoPixel bring-up checklist (PS2 build)

Wiring: WS2812 DIN -> GPIO4 (or configured pin); 5V + GND; ~300-500R in series
on DIN and a decoupling cap recommended.

- [ ] Power on, no controller: pixel breathes **amber** (never fully off).
- [ ] Enter pairing / put a controller in discoverable mode: pixel pulses **blue**.
- [ ] Connect 1 pad: pixel is **green** with a single heartbeat beat per ~2.5 s.
- [ ] Connect 2 pads (or multitap): green with a double-beat.
- [ ] Force an init fault (e.g. FS/NVS): pixel blinks **red** rapidly and latches.
- [ ] On-board error LED still pulses/behaves exactly as before (unchanged).
- [ ] PS2 controller input works normally throughout (no I/O regression).

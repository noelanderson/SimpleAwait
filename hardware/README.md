# Hardware validation

This directory holds on-target validation sketches used to confirm behavior that
host tests cannot cover (native clock monotonicity, IRQ → `ThreadSafeFlag` wake,
long-running allocator recovery, and representative peripheral wakeups).

Per-target subdirectories are added as the corresponding milestones land:

- `rp2040/`  — Raspberry Pi RP2040 (Arduino-Pico)
- `rp2350/`  — Raspberry Pi RP2350, Arm and RISC-V (Arduino-Pico)
- `esp32/`   — ESP32 family, ESP32-S3 required (Arduino-ESP32)

At M0 there is no runtime behavior to validate on hardware; the build skeleton is
verified by the host tests and the `examples/Empty` compile checks.

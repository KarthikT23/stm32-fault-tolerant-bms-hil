# SafeCell

Fault-tolerant battery-monitoring firmware for the STM32F407 Discovery board (FreeRTOS),
validated with a PC-driven hardware-in-the-loop (HIL) test bench.

BMS-style monitoring firmware: the battery pack is simulated by a Python emulator that
streams cell voltages, current and temperatures over UART. This is not a certified BMS.

## Status
Work in progress.

## Layout
- `firmware/` - STM32CubeIDE project (FreeRTOS, HAL)
- `hil/` - Python pack emulator and automated test scenarios
- `docs/` - requirements, diagrams, test evidence

See `docs/requirements.md` for the interface spec and requirements.

See [docs/cubemx-setup.md](docs/cubemx-setup.md) for the CubeMX configuration and how to rebuild the project from the .ioc.

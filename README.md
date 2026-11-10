# IRSense — IR object counter & conveyor monitor (NUCLEO-F401RE)

[![CI](https://github.com/MohansaiPotnuru/IRSense-NUCLEO-F401RE/actions/workflows/ci.yml/badge.svg)](https://github.com/MohansaiPotnuru/IRSense-NUCLEO-F401RE/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE) [![Release](https://img.shields.io/github/v/release/MohansaiPotnuru/IRSense-NUCLEO-F401RE)](https://github.com/MohansaiPotnuru/IRSense-NUCLEO-F401RE/releases/latest)

Bare-metal STM32 firmware that turns an IR obstacle sensor into a
measurement instrument. It counts objects, measures how long each one
blocks the beam and the gap between them to the microsecond, tracks
throughput, raises jam, rate and intrusion alarms, and has a serial
command shell with settings saved in flash.

Board: **NUCLEO-F401RE** (STM32F401RET6, Cortex-M4F, 84 MHz). Generated with
STM32CubeMX and buildable in **STM32CubeIDE** or from the command line with
`make`.

```
irsense> status
mode      : counter
beam      : clear
objects   : 128  (rejected 3)
batch     : 8/10  (12 done)
rate      : 42 /min  (peak 61)
width us  : min 3120  avg 4875  max 9010  (n=128)
gap us    : min 18200  avg 1402311  max 9930120  (n=127)
alarms    : none  (latched none)
uptime    : 812.554 s
```

---

## Features

**Measurement**
- µs-accurate timestamps: TIM2 is captured inside the EXTI0 interrupt on
  every IR edge
- A timestamp-based glitch filter (`debounce_us`) that reports the *original*
  edge time, so filtering adds latency but no error
- Per-object width, gap to the previous object, min / avg / max, and
  throughput over a sliding 60 s window plus its peak

**Application**
- 3 modes, cycled with a short press of B1:
  - **Counter**: counts objects and signals every *N* objects (batch)
  - **Monitor**: measures only
  - **Guard**: an intrusion detector with a 5 s arming delay
- Alarms: `JAM` (beam blocked too long), `RATE` (throughput above the
  threshold, with hysteresis), `INTRUSION` and `OVERFLOW`. Alarms latch
  until acknowledged.
- B1 gestures: **short** = next mode, **double** = reset counters,
  **long** = acknowledge alarms or pause/resume (arm/disarm in guard mode)
- LD2 status patterns (heartbeat, blinks, flashes) plus a 40 ms blip for
  every counted object

**System**
- A serial shell on the ST-Link virtual COM port with 21 commands, line
  editing and history
- 9 runtime settings described by one table; `set` applies them live
- **Wear-levelled, power-fail-safe config storage** in flash: CRC-32,
  sequence numbers, 2520 saves per erase
- An independent watchdog, reset-cause reporting, and overflow detection on
  every queue
- A cooperative scheduler with per-task execution-time profiling
- A machine-readable event stream, with a Python live dashboard and CSV logger
- `sim` command: test everything without a sensor

**Engineering**
- Layered design: register-level drivers in `bsp/`, and all logic in
  hardware-independent `app/`
- **45 host unit tests** (494 checks) run on a PC with
  AddressSanitizer + UBSan, including a simulated NOR flash with power-loss
  injection
- GitHub Actions CI: unit tests plus an ARM firmware build, with the
  `.elf`/`.hex`/`.bin` uploaded as artifacts
- Survives CubeMX code regeneration: the only changes to generated files are
  two calls inside `USER CODE` blocks

See [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) for the design and
[docs/CLI.md](docs/CLI.md) for every command, setting and record format.

---

## Hardware

| Pin | Use | Notes |
|---|---|---|
| **PB0** | IR sensor output | EXTI0, both edges, internal pull-up |
| PC13 | B1 blue button | polled every 5 ms, debounced |
| PA5 | LD2 green LED | status patterns |
| PA2 / PA3 | USART2 TX / RX | 115200 8N1, ST-Link virtual COM port (same USB cable) |
| PB3 | SWO | log mirror for the CubeIDE SWV console |
| PA13 / PA14 | SWDIO / SWCLK | debugger |

### Wiring a typical IR obstacle module (FC-51, TCRT5000, E18-D80NK...)

```
 IR module            NUCLEO-F401RE
 ─────────            ─────────────
   VCC  ───────────── 3V3   (CN6 pin 4)
   GND  ───────────── GND   (CN6 pin 6)
   OUT  ───────────── PB0   (CN8 pin 4 / Arduino A3)
```

Most modules pull OUT **low** when they see an object, which is the default
(`active_low = 1`). For a sensor that goes high, use `set active_low 0`.

> **PB0 is not 5 V tolerant** (it is an analog-capable pin). Power the module
> from **3V3**. A 5 V-only sensor needs a level shifter or a resistor divider
> on OUT.

Clock: 16 MHz HSI → PLL → **84 MHz** SYSCLK (APB1 42 MHz, APB2 84 MHz).

---

## Quickstart

### 1. Requirements

| What | Version used |
|---|---|
| NUCLEO-F401RE board + USB Mini-B cable | — |
| [STM32CubeIDE](https://www.st.com/en/development-tools/stm32cubeide.html) | 2.2.0 (GNU Tools for STM32 14.3) |
| STM32Cube FW_F4 package | V1.28.3 |
| Optional: `make` + `arm-none-eabi-gcc` | for command-line builds |
| Optional: Python 3.8+ with `pyserial` | for `tools/irsense_monitor.py` |

### 2a. Build and flash with STM32CubeIDE

1. **File → Import… → General → Existing Projects into Workspace**. Select the
   cloned folder, tick **Mohan_Sai**, leave *Copy projects into workspace*
   unticked, and click **Finish**.
2. **Project → Build Project**.
3. Plug in the board, then **Run → Debug As → STM32 C/C++ Application** and
   press **Resume (F8)**.

### 2b. Or build from the command line

```sh
make                 # -> build/Mohan_Sai.elf / .hex / .bin
make test            # host unit tests (needs a native C compiler)
```

To flash, copy `build/Mohan_Sai.bin` onto the **NOD_F401RE** USB drive the
board shows up as, or use STM32CubeProgrammer:
`STM32_Programmer_CLI -c port=SWD -w build/Mohan_Sai.elf -rst`.

On Windows you can use the toolchain bundled with CubeIDE:

```sh
P=/c/ST/STM32CubeIDE_2.2.0/STM32CubeIDE/plugins
export PATH="$(echo $P/*gnu-tools-for-stm32*/tools/bin):$(echo $P/*externaltools.make*/tools/bin):$PATH"
make
```

### 3. Talk to it

Open the board's COM port (Device Manager → *STMicroelectronics STLink
Virtual COM Port*) at **115200 8N1** and press Enter:

```
[     0.002] I sys: IRSense 1.0.0 (a1b2c3d) on NUCLEO-F401RE
[     0.002] I sys: reset cause: reset pin
[     0.003] I sys: using default config (empty)
[     0.003] I sys: mode counter, beam clear. Type 'help' for commands.
irsense> sim 12
simulated 12 objects (width 5000 us, gap 20000 us)
irsense> status
```

No sensor yet? `sim` injects objects through exactly the same code path.

### 4. Live dashboard (optional)

```sh
pip install -r tools/requirements.txt
python tools/irsense_monitor.py --list
python tools/irsense_monitor.py --port COM5 --csv run.csv
```

---

## Project layout

```
Core/
  Inc/app/, Src/app/     portable application code (no HAL), unit tested
    app.c                modes, alarms, batches, LED pattern selection
    app_cli.c            serial commands
    cli.c                line editor, tokenizer, dispatcher
    config.c             settings table: defaults, ranges, set/get
    config_store.c       wear-levelled, CRC-protected flash records
    edge_filter.c        timestamp glitch filter
    button.c             debounce + short/double/long gestures
    stats.c              totals, min/avg/max, 60 s rate window
    led_pattern.c        LED sequencer + flash overlay
    scheduler.c          cooperative tasks with timing stats
    event_queue.c        lock-free ISR -> main queue
    ring_buffer.c        lock-free byte FIFO (UART)
    log.c, crc32.c, util.c
  Inc/bsp/, Src/bsp/     board support (registers / HAL)
    firmware.c           wires bsp <-> app, main loop
    bsp_ir.c             EXTI0 ISR on PB0 + TIM2 timestamps
    bsp_uart.c           IRQ-driven USART2
    bsp_time.c           TIM2 1 MHz timebase
    bsp_board.c          button, LED, IWDG, reset cause, UID, SWO
    bsp_flash.c          flash sector 7 driver
  Src/main.c             CubeMX-generated; calls fw_init() / fw_poll()
Drivers/                 ST HAL + CMSIS (don't edit)
tests/                   host unit tests (make -C tests [SAN=1])
tools/                   irsense_monitor.py + its tests
docs/                    ARCHITECTURE.md, CLI.md
Makefile                 command-line firmware build
STM32F401RETX_FLASH.ld   linker script (last 128 KiB reserved for config)
Mohan_Sai.ioc            CubeMX configuration
```

## Memory use

| | Used | Available |
|---|---|---|
| Flash (program) | ~23 KB | 384 KB (+128 KB config sector) |
| RAM | ~5.7 KB | 96 KB |

## Working with CubeMX

- The firmware configures TIM2, USART2, EXTI0 and IWDG itself (in `bsp/`),
  so **don't enable them in the `.ioc`**: CubeMX would generate a
  conflicting init code and IRQ handlers.
- Only write code between `/* USER CODE BEGIN … */` and `/* USER CODE END … */`
  in generated files. After **Generate Code**, check `git diff` and make sure
  `fw_init();` and `fw_poll();` are still in `main.c`.
- CubeMX doesn't overwrite an existing linker script, so the 384 KiB limit
  survives regeneration.

## Testing

```sh
make -C tests            # 45 tests, 494 checks
make -C tests SAN=1      # with AddressSanitizer + UBSan (Linux/macOS)
python -m unittest discover -s tools -p "test_*.py"
```

The tests cover the ring buffer and queue wrap-around, CRC vectors, glitch
filtering across timer wrap, button gestures, the rate window, and the
scheduler's overrun handling. They also cover config storage under
corruption and power loss, CLI parsing and line editing, and the full
application state machine (batches, jam, rate hysteresis, guard arming,
pause/reset, simulation).

## Git notes

This folder is also a CubeIDE workspace that holds unrelated files, so
`.gitignore` **ignores everything by default** and lists only the
project's files. If you add a new top-level file or folder, add a matching
`!/name` line to `.gitignore`, or git will silently skip it. `Debug/`,
`build/`, `.metadata/` and `EWARM/` are intentionally not tracked.

## Ideas for next steps

- [ ] Capture HardFault registers in backup SRAM and report them after reboot
- [ ] DMA for UART TX
- [ ] A second sensor channel for direction / speed measurement
- [ ] Low-power STOP mode between objects, woken by EXTI

## License

MIT for the IRSense application code. See [LICENSE](LICENSE). ST/Arm vendor files under `Drivers/` keep their own licenses.

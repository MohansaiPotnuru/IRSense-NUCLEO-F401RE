# IRSense serial interface

Connect to the ST-Link virtual COM port at **115200 8N1** (PuTTY, Tera
Term, `screen`, the CubeIDE terminal, or `tools/irsense_monitor.py`).
Press Enter to get the `irsense>` prompt.

Line editing: Backspace, **Ctrl-C** (discard the line), **↑** (repeat the
last command). Commands and setting names are case-insensitive. Numbers
can be decimal, `0x` hex, or contain `_` (`1_000_000`).

## Commands

| Command | Description |
|---|---|
| `status` | mode, beam state, counts, batch progress, rate, width and gap stats, alarms, uptime |
| `version` | firmware name, version, git hash |
| `reset` | reset counters and statistics (same as a double press) |
| `mode [counter\|monitor\|guard]` | show or change the mode (a short press cycles through them) |
| `pause` / `resume` | stop or restart counting |
| `arm` / `disarm` | guard mode only. Arming has a 5 s exit delay |
| `ack` | acknowledge latched alarms (same as a long press while an alarm shows) |
| `config` | list every setting with value, unit, range and description |
| `get <name>` | show one setting |
| `set <name> <value>` | change a setting. Takes effect immediately |
| `save` / `load` | write settings to flash / reload them |
| `defaults` | restore factory settings (run `save` to keep them) |
| `stream [on\|off]` | machine-readable `$` records (below) |
| `log [error\|warn\|info\|debug]` | show or set the log level |
| `sim <n> [width_us] [gap_us]` | inject `n` simulated objects (defaults 5000 / 20000 µs) |
| `tasks` | scheduler table: period, runs, overruns, last / max execution time |
| `sys` | clock, unique ID, reset cause, IR edge / glitch counts, queue and UART stats, flash slots |
| `reboot` | software reset |
| `help [command]` | list the commands, or show one command's usage |

## Settings

| Name | Default | Range | Meaning |
|---|---|---|---|
| `debounce_us` | 2000 | 50 – 100000 | the IR level must be stable this long to count |
| `min_width_us` | 0 | 0 – 10000000 | objects shorter than this are rejected (`$REJ`) |
| `jam_ms` | 3000 | 0 – 600000 | beam blocked longer than this raises `JAM` (0 = off) |
| `batch` | 10 | 0 – 1000000 | objects per batch in counter mode (0 = off) |
| `rate_alarm` | 0 | 0 – 60000 | objects/min that raises `RATE` (0 = off) |
| `active_low` | 1 | 0 – 1 | 1 = the sensor output goes LOW when an object is present |
| `stream` | 0 | 0 – 1 | print `$` records |
| `log_level` | 2 | 0 – 3 | 0 error, 1 warn, 2 info, 3 debug |
| `boot_mode` | 0 | 0 – 2 | mode after reset: 0 counter, 1 monitor, 2 guard |

## Stream records (`stream on`)

One record per line, comma separated. `t_ms` is the device uptime in ms.

| Record | Fields | When |
|---|---|---|
| `$OBJ` | `t_ms, seq, width_us, gap_us, total` | an object passed and was accepted |
| `$REJ` | `t_ms, width_us` | an object was shorter than `min_width_us` |
| `$BEAM` | `t_ms, 0\|1` | the filtered beam state changed (1 = blocked) |
| `$BATCH` | `t_ms, batches, target` | a batch completed |
| `$ALARM` | `t_ms, SET\|CLEAR, FLAGS` | an alarm was raised or acknowledged (`JAM\|RATE\|INTRUSION\|OVERFLOW`) |
| `$MODE` | `t_ms, mode` | the mode changed |
| `$PAUSE` | `t_ms, 0\|1` | counting was paused or resumed |
| `$GUARD` | `t_ms, disarmed\|arming\|armed` | the guard state changed |
| `$RESET` | `t_ms` | the counters were reset |

## Example session

```
irsense> sim 3 4000 16000
simulated 3 objects (width 4000 us, gap 16000 us)
irsense> status
mode      : counter
beam      : clear
objects   : 3  (rejected 0)
batch     : 3/10  (0 done)
rate      : 3 /min  (peak 3)
width us  : min 4000  avg 4000  max 4000  (n=3)
gap us    : min 20000  avg 20000  max 20000  (n=2)
alarms    : none  (latched none)
uptime    : 42.118 s
irsense> set batch 3
batch = 3
irsense> save
saved (record #1, slot 1/2520)
```

## Encoded data

Each 256-byte flash page stores a stream of 4-bit nibbles, packed high nibble first. A code may start in either half of a byte.

| Nibble(s) | Meaning |
|---|---|
| `0000`-`0110` | Temperature delta of 0 to +6 quantization steps. |
| `0111` | Metadata prefix; the following nibble selects the record type below. |
| `1000` | Anchor escape; the following 3 nibbles contain an absolute temperature value `q`. |
| `1001`-`1110` | Temperature delta of -1 to -6 steps; magnitude is `nibble - 0x8`. |
| `1111` | Erased/unused nibble; ends the used data in a page. |

Metadata records, each beginning with `0x7`, are:

| Nibbles | Record | Payload |
|---|---|---|
| `7 1` | TIMEBASE | 8 nibbles of Unix time, 3 nibbles of sampling interval in whole minutes, then an anchor (`8` + 3-nibble `q`). |
| `7 2` | SENSOR_ERR | No payload; records a failed sensor-read slot. |
| `7 3` | BOOT | No payload; marks the start of a logging segment. |
| `7 4` | CHECK | 4 nibbles of CRC-16 over all nibbles since the previous CHECK on this page (or the start of the page). |

The anchor value is `q = round((temperature_C + 40) / 0.25)`, clamped to `0..500`; decode it as `q * 0.25 - 40` °C. Deltas update the previous `q`. Each page's remaining erased flash is `0xFF`; the first unused `0xF` nibble terminates decoding.

## Sampling
Configured in minutes, 1 - 4095

## RAM buffer flush & flash storage
GP21 flushes buffer
Hourly auto-flushing during sample wakes.
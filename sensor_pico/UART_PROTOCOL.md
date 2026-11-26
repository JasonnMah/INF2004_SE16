# Sensor Pico UART protocol

This document describes the sensor Pico (Pico 1) side of the UART link to the
Wi-Fi Pico (Pico 2). The shared protocol definitions are in
`shared/uart_protocol/src`.

## Physical link and wake-up

- UART1: GP4 TX, GP5 RX, 115200 baud, 8 data bits, no parity, 1 stop bit.
- The sensor Pico can be asleep with its UART clocks gated. Before **each**
  message, the Wi-Fi Pico must send 32 bytes of `0x55` immediately followed by
  the protocol frame, with no gap. The preamble's falling edge wakes the sensor;
  the byte arriving during wake-up may be lost or garbled. It is sacrificial,
  not part of the frame.
- After UART activity or a UART wake edge, the sensor stays awake for 500 ms.
  An active data transfer or flash clear also keeps its protocol processing
  active until completion.

## Frame format

Each frame is delimited by `0x7E`:

| Field | Size | Description |
|---|---:|---|
| SOF | 1 byte | `0x7E` |
| Version | 1 byte | `0x01` |
| Message type | 1 byte | See message types below |
| Flags | 1 byte | Reserved |
| Session ID | 4 bytes | Little-endian; sensor currently expects `0x11223344` |
| Sequence | 2 bytes | Little-endian sequence number |
| Payload length | 2 bytes | Little-endian |
| Payload | 0–256 bytes | Message-specific |
| CRC | 2 bytes | CRC-16/X-25, little-endian |
| EOF | 1 byte | `0x7E` |

Within the frame, `0x7E` is escaped as `0x7D 0x5E`, and `0x7D` as
`0x7D 0x5D`. Multi-byte integers in payloads are little-endian too. Frames
with another session ID are ignored.

## Messages handled by the sensor

| Type | Value | Payload | Sensor behavior |
|---|---:|---|---|
| `HELLO` | `0x01` | Empty | ACKs the request. |
| `CONFIG` | `0x02` | Unix timestamp (u32), sampling interval in seconds (u32) | Accepts intervals that are at least 60 seconds, are a whole number of minutes, and are no more than 4095 minutes. ACKs accepted settings; otherwise NACKs. |
| `REQUEST_DATA` | `0x03` | Starting data block (u16) | Flushes/seals the current log snapshot, then ACKs and starts sending blocks from the requested index. |
| `ACK` | `0x05` | Acknowledged sequence (u16) | Advances the active DATA/TRANSFER_END exchange when it acknowledges the pending frame. |
| `NACK` | `0x06` | Rejected sequence (u16), reason (u8) | Retries the matching pending transfer frame. |
| `CLEAR_LOG` | `0x07` | Empty | Starts an asynchronous flash erase. ACKs only after erase completes; NACKs on failure. |

The NACK reason values are `0x01` bad payload, `0x02` unexpected message,
and `0x03` bad data block.

Accepted CONFIG values update the sampling schedule immediately. The timestamp
and interval are recorded in the log as a new TIMEBASE with the next successful
sensor reading, not at CONFIG receipt.

## Data transfer

After acknowledging a `REQUEST_DATA`, the sensor sends `DATA` frames (`0x04`)
one block at a time:

- Payload starts with a u16 little-endian block index, followed by up to 8 bytes
  of the flash snapshot.
- Each DATA frame awaits its ACK before the next block is sent.
- After all blocks, the sensor sends `TRANSFER_END` (`0x08`) with the total
  block count as a u16 little-endian payload. The transfer is complete when
  this frame is ACKed.
- The flash snapshot is held stable through the transfer; sensor flash writes
  are paused while it is in use.

Transfer frames are retried after a 500 ms timeout, up to three retries. A
matching NACK also triggers a retry. If retries are exhausted, the transfer is
aborted and the snapshot is released. The 16-bit block index limits a snapshot
to `65535 * 8` bytes.

An accepted CLEAR_LOG is processed in the main loop, one flash erase step at a
time. While it is pending, the sensor does not ACK it or accept other commands;
it sends the ACK after the erase and verification finish, or a NACK if the
operation fails.

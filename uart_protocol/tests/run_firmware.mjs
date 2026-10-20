// Executes the actual SDK-produced .bin using rp2040js, not a host C mock.
// This is a LOCAL emulator check, not the Wokwi VS Code/cloud simulator.
import fs from 'node:fs';
import assert from 'node:assert/strict';
import { Simulator, ConsoleLogger, LogLevel } from 'rp2040js';
import { bootromB1 } from './vendor/bootrom.mjs';

const [binary, modeText, logPath, goldenPath, expectedBytesText = '100'] = process.argv.slice(2);
const mode = Number(modeText);
const expectedBytes = Number(expectedBytesText);
if (!binary || !logPath || !/^[0-4]$/.test(modeText ?? '')) {
  console.error('Usage: node tests/run_firmware.mjs firmware.bin 0..4 output.log [golden-wire.json] [expected-bytes=100]');
  process.exit(2);
}
assert(Number.isSafeInteger(expectedBytes) && expectedBytes > 0, 'Invalid expected byte count');
const simulator = new Simulator();
const { rp2040: mcu, clock } = simulator;
mcu.logger = new ConsoleLogger(LogLevel.Warn);
mcu.loadBootrom(bootromB1);
const firmware = fs.readFileSync(binary);
assert(firmware.length <= mcu.flash.length, 'Firmware exceeds emulator flash');
mcu.flash.set(firmware);
// Use the same flash entry point as the upstream rp2040js demo.
mcu.core.PC = 0x10000000;

let monitor = '';
const transmitted = [[], []];
let finished = false;
const required = [
  '[P2] HELLO acknowledged',
  '[P2] CONFIG acknowledged',
  '[P2] REQUEST_DATA acknowledged',
  '[P2] TRANSFER_END validated',
  '[P2] Retrieved compressed log matches Pico 1 data',
  '[P2] CLEAR_LOG acknowledged',
  '=== UART PROTOCOL FAILURE TEST COMPLETE ===',
];
const modeMarkers = [
  ['[TEST] Mode: NONE (normal happy path)'],
  ['[TEST] Injected CRC corruption into first DATA frame',
   '[P2] Invalid frame dropped',
   '[P1] Transfer timeout: retransmitted pending frame'],
  ['[TEST] Intentionally dropped ACK for DATA block 0',
   '[P1] Transfer timeout: retransmitted pending frame',
   '[P2] Duplicate DATA block detected; ACKing without storing twice'],
  ['[TEST] Injected wrong block index in first DATA frame',
   '[P1] NACK received; retransmitted correct transfer frame'],
  ['[TEST] Intentionally dropped first CONFIG ACK',
   '[P2] Command timeout: retransmitted'],
];

function finish(error) {
  if (finished) return;
  finished = true;
  simulator.stop();
  clearTimeout(wallTimeout);
  fs.writeFileSync(logPath, monitor, 'latin1');
  const wire = transmitted.map(bytes => Buffer.from(bytes).toString('hex'));
  fs.writeFileSync(`${logPath}.wire.json`, JSON.stringify(wire, null, 2) + '\n');
  try {
    if (error) throw error;
    for (const marker of [...required, ...modeMarkers[mode]]) {
      assert(monitor.includes(marker), `Missing expected message: ${marker}`);
    }
    for (const marker of ['TEST FAILED', 'Retry limit reached',
      'retry limit reached', 'Transfer validation failed']) {
      assert(!monitor.includes(marker), `Unexpected failure: ${marker}`);
    }
    // The unchanged demo validates every source byte before acknowledging END.
    // Mode 2 stores block 0 but intentionally omits its first ACK/log message.
    const newBlocks = monitor.split('[P2] DATA block received and ACKed').length - 1;
    assert.equal(newBlocks, Math.ceil(expectedBytes / 8) - (mode === 2 ? 1 : 0),
      'Unexpected DATA acceptance count');
    const duplicates = monitor.split('[P2] Duplicate DATA block detected').length - 1;
    assert.equal(duplicates, mode === 2 ? 1 : 0, 'Unexpected duplicate handling');
    if (goldenPath) {
      assert.deepEqual(wire, JSON.parse(fs.readFileSync(goldenPath, 'utf8')),
        'UART bytes differ from the pre-refactor implementation');
    }
    console.log(`PASS local RP2040 emulator: mode ${mode}; ${expectedBytes} bytes validated; CLEAR_LOG ACKed`);
    process.exitCode = 0;
  } catch (failure) {
    console.error(`FAIL local RP2040 emulator: mode ${mode}: ${failure.message}`);
    process.exitCode = 1;
  }
}

// Each output callback represents one byte written to the emulated UART.
// Deliver bytes in order, spaced by ten bits at 115200 baud (8N1).
// UART0 -> UART1 is GP0 -> GP5; UART1 -> UART0 is GP4 -> GP1.
// This models byte transport, NOT GPIO waveforms or TX FIFO backpressure.
for (let tx = 0; tx < 2; tx++) {
  let nextByteAt = 0;
  mcu.uart[tx].onByte = value => {
    transmitted[tx].push(value);
    nextByteAt = Math.max(nextByteAt, clock.nanos) + 1e9 * 10 / 115200;
    clock.createAlarm(() => {
      const receiver = mcu.uart[1 - tx];
      if (receiver.flags & (1 << 6)) {
        finish(new Error('Emulated UART RX FIFO overflow'));
        return;
      }
      receiver.feedByte(value);
      // Passive serial monitor on UART0 only: no extra transmitter on GP1.
      if (tx === 0) {
        monitor += String.fromCharCode(value);
        if (monitor.endsWith('=== UART PROTOCOL FAILURE TEST FAILED ===\r\n') ||
            monitor.endsWith('=== UART PROTOCOL FAILURE TEST FAILED ===\n')) {
          finish(new Error('Firmware reported failure'));
        } else if (monitor.includes('=== UART PROTOCOL FAILURE TEST COMPLETE ===\r\n') ||
                   monitor.includes('=== UART PROTOCOL FAILURE TEST COMPLETE ===\n')) {
          finish();
        }
      }
    }).schedule(nextByteAt - clock.nanos);
  };
}
clock.createAlarm(() => finish(new Error('10-second simulation timeout'))).schedule(10e9);
const wallTimeout = setTimeout(() => finish(new Error('120-second wall-clock timeout')), 120000);
simulator.execute();

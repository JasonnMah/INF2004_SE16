// Real Pico SDK binary + simulated USB CDC, GPIO buttons, UART and I2C.
// Peripheral models do not establish physical AHT10/electrical correctness.
import fs from 'node:fs';
import assert from 'node:assert/strict';
import { Simulator, USBCDC, ConsoleLogger, LogLevel, GPIOPinState } from 'rp2040js';
import { bootromB1 } from './vendor/bootrom.mjs';

const [binary, scenario, output] = process.argv.slice(2);
if (!binary || !output || !['absent', 'stuck', 'ready', 'rx_timeout'].includes(scenario)) {
  console.error('Usage: node tests/run_codec_firmware.mjs firmware.bin absent|stuck|ready|rx_timeout output.log');
  process.exit(2);
}
const sim = new Simulator();
const { rp2040: mcu, clock } = sim;
mcu.logger = new ConsoleLogger(LogLevel.Error);
mcu.loadBootrom(bootromB1);
mcu.flash.set(fs.readFileSync(binary));
mcu.core.PC = 0x10000000;
const usb = new USBCDC(mcu.usbCtrl);
let text = '', finished = false, opened = false, i2cWrites = 0;
let pressedConfig = false, pressedRetrieve = false;
let requestedRecovery = false, completeScheduled = false;
let requestAckAt, receiveFailureAt, ledRises = 0;
mcu.gpio[6].addListener(state => {
  if (state === GPIOPinState.High && mcu.gpio[6].outputEnable) { ledRises++; }
});
const i2c = mcu.i2c[0];
let readIndex = 0;
// AHT10-formatted six-byte response: not busy, temperature raw 0x60000 = 25 C.
const measurement = [0x08, 0, 0, 0x06, 0, 0];
i2c.onConnect = address => {
  assert(opened, 'Diagnostic image touched I2C before the monitor was opened');
  assert.equal(address, 0x38);
  readIndex = 0;
  if (scenario !== 'stuck') { i2c.completeConnect(scenario === 'ready' || scenario === 'rx_timeout'); }
};
i2c.onWriteByte = () => { i2cWrites++; i2c.completeWrite(true); };
i2c.onReadByte = () => i2c.completeRead(measurement[readIndex++ % measurement.length]);
for (const pin of [20, 21, 22]) { mcu.gpio[pin].setInputValue(true); }

function later(ms, fn) { clock.createAlarm(fn).schedule(ms * 1e6); }
function press(pin) {
  mcu.gpio[pin].setInputValue(false);
  later(80, () => mcu.gpio[pin].setInputValue(true));
}
// Model a terminal opening late, instead of assuming it is connected at reset.
usb.cdcSetControlLineState = function(value = 0, interfaceNumber = 0) {
  USBCDC.prototype.cdcSetControlLineState.call(this, value, interfaceNumber);
};
usb.onDeviceConnected = () => {
  later(700, () => { opened = true; usb.cdcSetControlLineState(3); });
};
for (let tx = 0; tx < 2; tx++) {
  let next = 0;
  mcu.uart[tx].onByte = byte => {
    next = Math.max(next, clock.nanos) + 1e9 * 10 / 115200;
    clock.createAlarm(() => mcu.uart[1 - tx].feedByte(byte)).schedule(next - clock.nanos);
  };
}
usb.onSerialData = bytes => {
  text += Buffer.from(bytes).toString('utf8');
  if (text.includes('GP22: clear/pause') && !pressedConfig) {
    pressedConfig = true;
    later(50, () => press(20));
  }
  if (text.includes('[UI] CONFIG acknowledged') && !pressedRetrieve) {
    pressedRetrieve = true;
    later(400, () => press(21));
  }
  if (text.includes('[P2] REQUEST_DATA acknowledged') && requestAckAt === undefined) {
    requestAckAt = clock.nanos;
  }
  if (text.includes('[P2] Receive timeout: no transfer progress') && receiveFailureAt === undefined) {
    receiveFailureAt = clock.nanos;
    assert(!text.includes('[P2] TRANSFER_END validated'), 'Partial transfer reached completion');
  }
  if (scenario === 'rx_timeout' && text.includes('[TEST] RX suppression released;') && !requestedRecovery) {
    requestedRecovery = true;
    later(700, () => press(21));
  }
  if (text.includes('[UI] Data retrieval complete') && !completeScheduled) {
    completeScheduled = true;
    // Let the UI's success pulse sequence finish before checking its LED count.
    later(500, () => finish());
  }
};
function finish(error) {
  if (finished) { return; }
  finished = true;
  sim.stop();
  clearTimeout(wall);
  fs.writeFileSync(output, text);
  try {
    if (error) { throw error; }
    for (const marker of ['[BOOT] codec startup diagnostics v1; USB output ready',
      '[BOOT] Entering main loop', 'Sensor codec loopback ready',
      '[UI] CONFIG acknowledged', '[P2] REQUEST_DATA acknowledged',
      '[P2] TRANSFER_END validated', '[UI] Data retrieval complete']) {
      assert(text.includes(marker), `Missing ${marker}`);
    }
    if (scenario === 'ready' || scenario === 'rx_timeout') {
      assert(i2cWrites >= 6, 'Expected AHT10 init and measurement-trigger writes');
      assert(text.includes('AHT10 init: OK'));
      assert(text.includes('[ENCODE] #1 25.00 C'));
      assert(/Samples decoded : [1-9]\d*/.test(text));
      assert(text.includes('1700000000       25.00 C'));
    } else {
      assert(text.includes('AHT10 init: FAILED'));
      assert(text.includes('[DECODE] Logger is empty'));
      assert(!text.includes('[ENCODE] #1'));
      if (scenario === 'stuck') {
        assert(text.includes('AHT10 init write returned -2'), 'Expected SDK timeout');
      }
    }
    if (scenario === 'rx_timeout') {
      assert(text.includes('[TEST] RX timeout test armed:'));
      assert(text.includes('[TEST] RX suppression released; received_log_valid=0, received_bytes=0'));
      assert(text.includes('[UI] Data retrieval failed'));
      assert(text.includes('[P1] Transfer timeout: retry limit reached'));
      assert.equal(text.split('[UI] Data retrieval failed').length - 1, 1);
      assert.equal(text.split('[UI] Data retrieval complete').length - 1, 1);
      const elapsedMs = (receiveFailureAt - requestAckAt) / 1e6;
      assert(elapsedMs >= 2490 && elapsedMs <= 2530, `Receiver failed at ${elapsedMs} ms`);
      assert.equal(ledRises, 6, 'Expected CONFIG pulse + three failure pulses + two success pulses');
    } else {
      assert(!text.includes('[TEST] RX timeout test armed:'), 'Use the normal diagnostic build for this case');
      assert(!text.includes('retry limit reached'));
    }
    console.log(`PASS codec ARM firmware: ${scenario}; late USB open, CONFIG, UART retrieval`);
  } catch (failure) {
    console.error(`FAIL ${scenario}: ${failure.message}; PC=0x${mcu.core.PC.toString(16)}; see ${output}`);
    process.exitCode = 1;
  }
}
later(7000, () => finish(new Error('7-second simulation timeout')));
const wall = setTimeout(() => finish(new Error('90-second execution limit')), 90000);
sim.execute();

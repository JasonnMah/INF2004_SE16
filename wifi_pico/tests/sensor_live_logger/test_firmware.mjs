// Actual RP2040 binary with modeled I2C/USB/UART. NOT a physical sensor test.
// Build the Sensor firmware first, then run with Node.js. Optional arguments
// select a scenario and target, for example:
//   node test_firmware.mjs init-short-write sensor_live_logger_i2c_diag
// The emulator supplies pretend AHT10 replies and acts as the Wi-Fi UART peer.
import fs from 'node:fs';
import path from 'node:path';
import {fileURLToPath, pathToFileURL} from 'node:url';
import assert from 'node:assert/strict';
const here = path.dirname(fileURLToPath(import.meta.url));
const root = path.resolve(here, '../../..');
// ready: successful sensor; absent: no sensor; no-usb: protocol without a monitor;
// init-short-write: startup warning but later measurements still work.
const scenario = process.argv[2] || 'ready';
assert(['ready', 'absent', 'no-usb', 'init-short-write'].includes(scenario));
const target = process.argv[3] || 'sensor_live_logger';
assert(['sensor_live_logger', 'sensor_live_logger_i2c_diag'].includes(target));
const diagnostic = target.endsWith('_i2c_diag');
const outputName = (diagnostic ? 'i2c-diag-' : '') + scenario;
const {Simulator, USBCDC, ConsoleLogger, LogLevel} = await import(pathToFileURL(path.join(root, 'wifi_pico/tests/legacy_uart/tests/node_modules/rp2040js/dist/esm/index.js')));
const {bootromB1} = await import(pathToFileURL(path.join(root, 'wifi_pico/tests/legacy_uart/tests/vendor/bootrom.mjs')));
const sim = new Simulator();
const {rp2040: mcu, clock} = sim;
mcu.logger = new ConsoleLogger(LogLevel.Error);
mcu.loadBootrom(bootromB1);
// Run the compiled Pico program, not a JavaScript rewrite of its main loop.
mcu.flash.set(fs.readFileSync(path.join(here, 'build/' + target + '.bin')));
mcu.core.PC = 0x10000000;
let text = '', done = false, configAck = false, requestAck = false, ended = false;
let nextRx = 0, peerSequence = 10, dataBlocks = 0, triggerTime, requestTime;
const received = [], serialFrame = [];
function later(ms, fn) { clock.createAlarm(fn).schedule(ms * 1e6); }
// Test-peer helpers build/check frames independently of the running firmware.
// This duplication is deliberate in the test so incorrect firmware is detected.
function crc(bytes) {
  let value = 0xffff;
  for (const b of bytes) {
    value ^= b;
    for (let n = 0; n < 8; ++n) value = (value >>> 1) ^ ((value & 1) ? 0x8408 : 0);
  }
  return value ^ 0xffff;
}
function frame(type, sequence, payload = Buffer.alloc(0)) {
  const body = Buffer.alloc(11 + payload.length + 2);
  body[0] = 1; body[1] = type;
  body.writeUInt32LE(0x11223344, 3); body.writeUInt16LE(sequence, 7);
  body.writeUInt16LE(payload.length, 9); payload.copy(body, 11);
  body.writeUInt16LE(crc(body.subarray(0, -2)), body.length - 2);
  const encoded = [0x7e];
  for (const b of body) {
    if (b === 0x7e || b === 0x7d) encoded.push(0x7d, b ^ 0x20);
    else encoded.push(b);
  }
  return [...encoded, 0x7e];
}
// Deliver bytes at the configured 115200-baud 8N1 rate instead of all at once.
function send(type, sequence, payload) {
  for (const b of frame(type, sequence, payload)) {
    nextRx = Math.max(nextRx, clock.nanos) + 1e9 * 10 / 115200;
    clock.createAlarm(() => mcu.uart[1].feedByte(b)).schedule(nextRx - clock.nanos);
  }
}
function ack(sequence) {
  const p = Buffer.alloc(2); p.writeUInt16LE(sequence);
  send(5, peerSequence++, p);
}
// Validate Sensor replies, collect DATA without its block index, and ACK frames
// as the real Wi-Fi endpoint would. Count blocks and reject a second completion.
function processFrame(encoded) {
  const raw = []; let escape = false;
  for (const b of encoded) {
    if (escape) { raw.push(b ^ 0x20); escape = false; }
    else if (b === 0x7d) escape = true;
    else raw.push(b);
  }
  assert(!escape);
  const body = Buffer.from(raw), payloadLength = body.readUInt16LE(9);
  assert.equal(body.length, 13 + payloadLength);
  assert.equal(crc(body.subarray(0, -2)), body.readUInt16LE(body.length - 2));
  const sequence = body.readUInt16LE(7), payload = body.subarray(11, -2);
  assert.equal(body[0], 1); assert.equal(body.readUInt32LE(3), 0x11223344);
  if (body[1] === 5) {
    const acknowledged = payload.readUInt16LE(0);
    if (acknowledged === 1) {
      configAck = true;
      // Bytes arrive during the actual driver's 80 ms measurement wait. The
      // firmware RX ISR must retain this request until main resumes.
      later(50, () => { requestTime = clock.nanos; send(3, 2, Buffer.from([0, 0])); });
    } else if (acknowledged === 2) requestAck = true;
  } else if (body[1] === 4) {
    assert.equal(payload.readUInt16LE(0), dataBlocks++);
    received.push(...payload.subarray(2)); ack(sequence);
  } else if (body[1] === 8) {
    assert.equal(payload.readUInt16LE(0), dataBlocks);
    assert(!ended, 'Unexpected second completion'); ended = true; ack(sequence);
  } else assert.fail('Unexpected protocol reply ' + body[1]);
}
mcu.gpio[5].setInputValue(true);
mcu.uart[1].onByte = b => {
  if (b === 0x7e) {
    if (serialFrame.length) processFrame(serialFrame.splice(0));
  } else serialFrame.push(b);
};
// Model I2C address/byte acknowledgements and a known temperature response.
// This checks software behavior; it cannot confirm real wiring or sensor health.
const i2c = mcu.i2c[0];
let readIndex = 0, commandByte;
const response = [0x08, 0, 0, 0x06, 0, 0]; // Calibrated, not busy, exactly 25 C.
i2c.onConnect = address => {
  assert.equal(address, 0x38); readIndex = 0; commandByte = undefined;
  i2c.completeConnect(scenario !== 'absent');
};
i2c.onWriteByte = b => {
  if (commandByte === undefined) commandByte = b;
  if (b === 0xac) triggerTime = clock.nanos;
  // Model the second initialization byte not being ACKed. Measurement remains
  // available, matching the symptom we need to distinguish on actual hardware.
  i2c.completeWrite(!(scenario === 'init-short-write' && commandByte === 0xe1 && b === 0x08));
};
i2c.onReadByte = () => i2c.completeRead(response[readIndex++ % response.length]);
// Attach a pretend USB monitor, then exercise the Sensor's 's' and 'd' commands.
// The no-usb case must still complete CONFIG and retrieval without USB output.
if (scenario !== 'no-usb') {
  const usb = new USBCDC(mcu.usbCtrl);
  usb.cdcSetControlLineState = function(value = 0, interfaceNumber = 0) {
    USBCDC.prototype.cdcSetControlLineState.call(this, value, interfaceNumber);
  };
  usb.onDeviceConnected = () => later(700, () => usb.cdcSetControlLineState(3));
  usb.onSerialData = bytes => { text += Buffer.from(bytes).toString('utf8'); };
  later(1200, () => usb.sendSerialByte('s'.charCodeAt(0)));
  if (scenario === 'ready' || scenario === 'init-short-write') later(1300, () => usb.sendSerialByte('d'.charCodeAt(0)));
}
// Send CONFIG automatically; physical GP20 is replaced by the test peer here.
later(500, () => {
  const config = Buffer.alloc(8); config.writeUInt32LE(1700000000, 0); config.writeUInt32LE(3600, 4);
  send(2, 1, config);
});
// Save generated evidence and check exact bytes and expected status messages.
// Emulated time drives the test; the wall timer only catches a stuck simulation.
function finish(error) {
  if (done) return; done = true; sim.stop(); clearTimeout(wall);
  fs.writeFileSync(path.join(here, 'build', 'emulator-' + outputName + '.log'), text);
  fs.writeFileSync(path.join(here, 'build', 'snapshot-' + outputName + '.bin'), Buffer.from(received));
  try {
    if (error) throw error;
    assert(configAck && requestAck && ended, 'CONFIG/request/transfer must complete');
    if (scenario === 'absent') {
      assert.equal(received.length, 0); assert.equal(dataBlocks, 0);
      assert(text.includes('AHT10 init: I2C error')); assert(text.includes('samples=0 errors=1'));
    } else {
      assert.equal(received.length, 256); assert.equal(dataBlocks, 32);
      // BOOT/TIMEBASE prefix and JZ's 25 C q=260 (0x104) anchor.
      assert.equal(Buffer.from(received).subarray(0, 10).toString('hex').slice(0, 19), '73716553f10003c8104');
      assert(triggerTime < requestTime && requestTime < triggerTime + 80e6,
             'Request must arrive while sensor conversion blocks main');
      if (scenario === 'no-usb') assert.equal(text, '');
      else {
        assert(text.includes('Sensor live AHT10 UART test'));
        assert(text.includes('AHT10 init: ' + (scenario === 'init-short-write' ? 'I2C error' : 'ok')));
        assert(text.includes('samples=1 errors=0'));
        assert(text.includes('dropped=0'));
        assert(text.split('[START] Sensor live AHT10 UART test').length >= 3, 'USB s must reprint status');
        assert(text.includes('[BINARY END] completion=1'), 'USB d must finish snapshot dump');
        const hex = [...text.matchAll(/\[BINARY\] offset=[0-9a-f]+ hex=([0-9a-f]+)/g)].map(m => m[1]).join('');
        assert.equal(hex, Buffer.from(received).toString('hex'), 'USB dump must match UART bytes');
      }
    }
    if (diagnostic && scenario !== 'no-usb') {
      assert(text.includes('[I2C INIT] Saved startup trace;'));
      if (scenario === 'init-short-write') {
        assert(/WRITE addr=0x38 bytes=e10800 expected=3 returned=1 elapsed_us=\d+ short write/.test(text));
        assert(text.includes('result=I2C error calls=2;'));
      } else if (scenario === 'ready') {
        assert(/READ addr=0x38 bytes=08 expected=1 returned=1 elapsed_us=\d+ complete/.test(text));
        assert(text.includes('result=ok calls=3;'));
      } else {
        // This RP2040JS address-NACK model does not restore TX_EMPTY after
        // its FIFO flush; the real SDK therefore reaches the 20 ms timeout.
        // Require that exact outcome, not a guessed physical NACK return code.
        const timeout = text.match(/WRITE addr=0x38 bytes=ba expected=1 returned=-2 elapsed_us=(\d+) timeout/);
        assert(timeout && Number(timeout[1]) >= 19900 && Number(timeout[1]) <= 20100);
        assert(text.includes('result=I2C error calls=1;'));
      }
    }
    console.log('PASS RP2040 emulator ' + target + ' ' + scenario + ': CONFIG, modeled I2C, UART RX IRQ and retrieval; NOT physical');
  } catch (e) { console.error(text); console.error(e); process.exitCode = 1; }
}
later(1800, () => finish());
const wall = setTimeout(() => finish(new Error('90-second wall timeout')), 90000);
sim.execute();

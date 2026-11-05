/* Run serially, after building the optional module:
 *   node backend/capture/tests/performance.js
 * CAPTURE_PERF_FRAMES changes the default 100,000 frames (~103 MiB).
 * CAPTURE_PERF_MMS_FILE selects a real, locally generated MMS fixture.
 * Files remain under .tmp/capture-performance for repeatable inspection.
 * This test creates and reads captures; it never opens a network interface.
 */
'use strict';

const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const { spawn, execFileSync } = require('node:child_process');
const { performance } = require('node:perf_hooks');
const readline = require('node:readline');

const root = path.resolve(__dirname, '../../..');
const output = path.join(root, '.tmp/capture-performance');
const helperPath = path.join(root, 'backend/capture/bin/capture-helper.exe');
const frameCount = Number(process.env.CAPTURE_PERF_FRAMES || 100_000);
assert(Number.isSafeInteger(frameCount) && frameCount >= 1000 && frameCount <= 2_000_000);
fs.mkdirSync(output, { recursive: true });

function createCapture(file, count) {
  const frame = Buffer.alloc(1066);
  Buffer.from('0200000000020200000000010800', 'hex').copy(frame);
  frame[14] = 0x45;
  frame.writeUInt16BE(frame.length - 14, 16);
  frame[22] = 64;
  frame[23] = 17;
  Buffer.from([198, 18, 0, 1, 198, 18, 0, 2]).copy(frame, 26);
  let sum = 0;
  for (let n = 14; n < 34; n += 2) sum += frame.readUInt16BE(n);
  while (sum >> 16) sum = (sum & 0xffff) + (sum >> 16);
  frame.writeUInt16BE((~sum) & 0xffff, 24);
  frame.writeUInt16BE(49152, 34);
  frame.writeUInt16BE(49153, 36);
  frame.writeUInt16BE(frame.length - 34, 38);
  const header = Buffer.alloc(24);
  header.writeUInt32LE(0xa1b2c3d4);
  header.writeUInt16LE(2, 4);
  header.writeUInt16LE(4, 6);
  header.writeUInt32LE(65535, 16);
  header.writeUInt32LE(1, 20);
  const fd = fs.openSync(file, 'w');
  try {
    fs.writeSync(fd, header);
    const batch = Buffer.alloc((frame.length + 16) * 512);
    let used = 0;
    for (let n = 0; n < count; n++) {
      const at = used * (frame.length + 16);
      batch.writeUInt32LE(1780000000 + Math.floor(n / 1_000_000), at);
      batch.writeUInt32LE(n % 1_000_000, at + 4);
      batch.writeUInt32LE(frame.length, at + 8);
      batch.writeUInt32LE(frame.length, at + 12);
      frame.copy(batch, at + 16);
      if (++used === 512 || n + 1 === count) {
        fs.writeSync(fd, batch.subarray(0, used * (frame.length + 16)));
        used = 0;
      }
    }
  } finally { fs.closeSync(fd); }
}

class Helper {
  constructor() {
    this.id = 0;
    this.pending = new Map();
    this.child = spawn(helperPath, [], { windowsHide: true, stdio: ['pipe', 'pipe', 'pipe'] });
    this.stderr = '';
    this.child.stderr.on('data', data => { this.stderr = (this.stderr + data.toString()).slice(-32768); });
    readline.createInterface({ input: this.child.stdout }).on('line', line => {
      let message;
      try { message = JSON.parse(line); } catch { return; }
      const call = this.pending.get(message.id);
      if (!call) return;
      clearTimeout(call.timer);
      this.pending.delete(message.id);
      if (message.error) {
        const error = new Error(message.error.message);
        error.code = message.error.code;
        call.reject(error);
      } else call.resolve(message.result);
    });
    this.child.on('error', error => this.rejectAll(error));
    this.child.on('exit', code => this.rejectAll(new Error(`Capture helper exited (${code}): ${this.stderr}`)));
    this.child.stdin.on('error', error => this.rejectAll(error));
  }

  rejectAll(error) {
    for (const call of this.pending.values()) { clearTimeout(call.timer); call.reject(error); }
    this.pending.clear();
  }

  call(command, args = {}, timeout = 180_000) {
    const id = ++this.id;
    return new Promise((resolve, reject) => {
      const timer = setTimeout(() => {
        this.pending.delete(id);
        reject(new Error(`Timed out waiting for ${command}`));
      }, timeout);
      this.pending.set(id, { resolve, reject, timer });
      this.child.stdin.write(`${JSON.stringify({ id, command, args })}\n`);
    });
  }

  async close() {
    try { await this.call('close', {}, 5000); } catch {}
    this.child.stdin.end();
    await new Promise(resolve => {
      if (this.child.exitCode !== null) return resolve();
      const timer = setTimeout(() => { this.child.kill(); resolve(); }, 5000);
      this.child.once('exit', () => { clearTimeout(timer); resolve(); });
    });
  }
}

function p95(values) { return [...values].sort((a, b) => a - b)[Math.max(0, Math.ceil(values.length * .95) - 1)]; }
function peakWorkingSet(pid) {
  if (process.platform !== 'win32') return null;
  try {
    // Read only. Decoder child-process peak memory is deliberately not claimed.
    return Number(execFileSync('powershell.exe', ['-NoProfile', '-Command', `(Get-Process -Id ${Number(pid)}).PeakWorkingSet64`], { windowsHide: true, encoding: 'utf8' }).trim());
  } catch { return null; }
}

async function measured(fn) {
  const started = performance.now();
  const result = await fn();
  return { result, elapsed_ms: performance.now() - started };
}

function realFixture() {
  if (process.env.CAPTURE_PERF_MMS_FILE) return path.resolve(process.env.CAPTURE_PERF_MMS_FILE);
  const directory = path.join(root, 'backend/capture/test-output');
  if (!fs.existsSync(directory)) return null;
  const files = fs.readdirSync(directory).filter(name => /\.pcapng$/i.test(name))
    .map(name => ({ file: path.join(directory, name), stat: fs.statSync(path.join(directory, name)) }))
    .filter(entry => entry.stat.size > 10000).sort((a, b) => b.stat.mtimeMs - a.stat.mtimeMs);
  return files[0]?.file || null;
}

async function main() {
  const captureFile = path.join(output, `offline-${frameCount}-frames.pcap`);
  createCapture(captureFile, frameCount);
  const report = {
    generated_at: new Date().toISOString(), node: process.version, platform: process.platform,
    frames: frameCount, capture_bytes: fs.statSync(captureFile).size,
    note: 'Offline generated Ethernet/IPv4/UDP load; MMS semantics are measured separately on a real capture. Working-set metric covers helper only, not TShark.',
  };
  const helper = new Helper();
  try {
    report.capabilities = await helper.call('capabilities');
    assert.equal(report.capabilities.analysis, true, 'TShark must be present; this performance run cannot be marked passed without the decoder');
    const opened = await measured(() => helper.call('open', { file: captureFile }));
    assert.equal(opened.result.total, frameCount);
    report.open_ms = opened.elapsed_ms;
    console.log(`Indexed ${frameCount} frames in ${report.open_ms.toFixed(1)} ms`);
    const times = [];
    for (let n = 0; n < 60; n++) {
      const offset = Math.floor((frameCount - 200) * ((n * 37 % 60) / 59));
      const page = await measured(() => helper.call('packets', { offset, limit: 200 }));
      assert.equal(page.result.packets.length, 200);
      assert.equal(page.result.packets[0].frame, offset + 1);
      times.push(page.elapsed_ms);
    }
    report.page_p95_ms = p95(times);
    report.page_max_ms = Math.max(...times);
    assert(report.page_p95_ms < 100, `Page queries exceed 100 ms p95: ${report.page_p95_ms}`);
    console.log(`Page p95 ${report.page_p95_ms.toFixed(2)} ms; checking detail cache`);
    const coldDetail = await measured(() => helper.call('detail', { frame: Math.floor(frameCount / 2) }));
    report.detail_cold_ms = coldDetail.elapsed_ms;
    const warm = [];
    for (let n = 0; n < 20; n++) {
      const detail = await measured(() => helper.call('detail', { frame: Math.floor(frameCount / 2) }));
      assert.equal(detail.result.frame, coldDetail.result.frame);
      warm.push(detail.elapsed_ms);
      assert(detail.elapsed_ms < 1000, `Repeated detail took ${detail.elapsed_ms.toFixed(1)} ms; likely rescanning the capture`);
    }
    report.detail_cached_p95_ms = p95(warm);
    assert(report.detail_cached_p95_ms < 100, `Cached detail exceeds 100 ms p95: ${report.detail_cached_p95_ms}`);
    console.log(`Cached detail p95 ${report.detail_cached_p95_ms.toFixed(2)} ms`);
    const noMms = await measured(() => helper.call('replay', { offset: 0, limit: 200 }));
    assert.equal(noMms.result.total, 0, 'UDP load must not produce guessed MMS events');
    report.cached_non_mms_replay_page_ms = noMms.elapsed_ms;
    const repeatReplay = await measured(() => helper.call('replay', { offset: 0, limit: 200 }));
    report.replay_cached_ms = repeatReplay.elapsed_ms;
    assert(report.replay_cached_ms < 100, 'A repeated replay page must use its cached index');
    report.helper_peak_working_set_bytes = peakWorkingSet(helper.child.pid);
    if (report.helper_peak_working_set_bytes !== null) assert(report.helper_peak_working_set_bytes < 384 * 1024 * 1024, 'Summary index accumulated too much helper memory');

    // Cancellation is out of band; immediately attach a rejection handler.
    const reopening = helper.call('open', { file: captureFile }).then(result => ({ result }), error => ({ error }));
    await new Promise(resolve => setTimeout(resolve, 100));
    const cancelStart = performance.now();
    await helper.call('cancel', {}, 5000);
    const cancellation = await reopening;
    report.cancel_ms = performance.now() - cancelStart;
    report.cancel_result = cancellation.error?.code || 'completed-before-cancel';
    assert(report.cancel_ms < 2500, `Cancellation exceeded 2.5 s: ${report.cancel_ms}`);
    if (cancellation.error) assert.equal(cancellation.error.code, 'CANCELLED');

    const fixture = realFixture();
    if (fixture) {
      report.mms_fixture = fixture;
      await helper.call('open', { file: fixture, mms_ports: [18122] });
      const eventPages = [];
      let offset = 0, total = Infinity;
      const started = performance.now();
      while (offset < total) {
        const page = await helper.call('replay', { offset, limit: 1000 });
        total = page.total;
        eventPages.push(...page.events);
        if (!page.events.length) break;
        offset += page.events.length;
      }
      report.mms_event_index_ms = performance.now() - started;
      report.mms_replay_events = eventPages.length;
      assert(eventPages.length > 0, 'Real MMS fixture must yield observed, mapped replay values');
      for (const e of eventPages) for (const v of e.values) {
        assert.equal(v.replayable, true);
        assert.equal(v.mapping, 'resolved');
        assert.notEqual(v.source, 'write-intent');
      }
      const last = eventPages.at(-1).frame;
      const first = eventPages[0].frame;
      const end = await helper.call('replay', { mode: 'snapshot', through_frame: last });
      const beginning = await helper.call('replay', { mode: 'snapshot', through_frame: first });
      const endAgain = await helper.call('replay', { mode: 'snapshot', through_frame: last });
      assert.deepEqual(endAgain.values, end.values, 'Seek must reconstruct exactly, without leaking a previous cursor state');
      report.first_snapshot_values = Array.isArray(beginning.values) ? beginning.values.length : Object.keys(beginning.values).length;
      const seekTimes = [];
      for (let n = 0; n < 100; n++) {
        const t = await measured(() => helper.call('replay', { mode: 'snapshot', through_frame: n % 2 ? first : last }));
        seekTimes.push(t.elapsed_ms);
      }
      report.mms_seek_p95_ms = p95(seekTimes);
      assert(report.mms_seek_p95_ms < 100, 'Repeated snapshot queries must not re-run TShark');
    } else report.mms_fixture_status = 'NOT RUN: supply CAPTURE_PERF_MMS_FILE for real MMS replay validation';
    report.status = 'passed';
  } catch (error) {
    report.status = 'failed';
    report.error = error.stack;
    process.exitCode = 1;
  } finally {
    await helper.close();
    fs.writeFileSync(path.join(output, 'result.json'), JSON.stringify(report, null, 2));
    console.log(JSON.stringify(report, null, 2));
  }
}

main().catch(error => { console.error(error); process.exitCode = 1; });

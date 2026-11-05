'use strict';

// Exercises the exact module imported by the production capture renderer.
// No Electron or capture helper is launched, so this can run during a DLL build.
const assert = require('node:assert/strict');
const path = require('node:path');
const fs = require('node:fs');
const { pathToFileURL } = require('node:url');
const { performance } = require('node:perf_hooks');

async function main() {
  const root = path.resolve(__dirname, '../../..');
  const { PacketRepository, visibleRange } = await import(pathToFileURL(path.join(root, 'src/capture/packet-repository.mjs')));
  const total = 100_000;
  let calls = 0;
  const repo = new PacketRepository({ request: async ({ offset, limit }) => {
    calls++;
    return { packets: Array.from({ length: Math.min(limit, total - offset) }, (_, n) => ({ frame: offset + n + 1, info: 'MMS ReadResponse' })), total };
  } });
  await Promise.all(Array.from({ length: 50 }, () => repo.load(0)));
  assert.equal(calls, 1, 'Concurrent requests for the same page must be deduplicated');
  assert.equal(repo.pending.size, 0);
  assert.equal(repo.get(0).frame, 1);

  const timings = [];
  for (let page = 0; page < 500; page++) {
    const started = performance.now();
    await repo.load(page * 200);
    timings.push(performance.now() - started);
    assert(repo.pages.size <= 20, 'A 100,000-frame capture must not keep every row in renderer memory');
    assert.equal(repo.get(page * 200).frame, page * 200 + 1);
  }
  const cachedRows = [...repo.pages.values()].reduce((sum, rows) => sum + rows.length, 0);
  assert(cachedRows <= 4000);
  assert.equal(repo.get(0), undefined, 'Older pages should be evicted');
  assert.equal(repo.get(99_999).frame, 100_000);
  const beforeWarm = calls;
  await repo.load(99_999);
  assert.equal(calls, beforeWarm, 'Selecting a cached frame must not issue another transport request');

  let finishOld, finishNew;
  const requestedFilters = [];
  const race = new PacketRepository({ request: ({ filter }) => {
    requestedFilters.push(filter);
    return new Promise(resolve => { if (filter === 'mms') finishNew = resolve; else finishOld = resolve; });
  } });
  const oldLoad = race.load(0);
  await Promise.resolve();
  race.reset('mms');
  const newLoad = race.load(0);
  await Promise.resolve();
  finishOld({ packets: [{ frame: 99 }], total: 99 });
  assert.deepEqual(await oldLoad, []);
  assert.equal(race.pending.size, 1, 'An old generation must not delete the new pending page');
  finishNew({ packets: [{ frame: 7 }], total: 1 });
  await newLoad;
  assert.equal(race.get(0).frame, 7);
  assert.equal(race.total, 1);
  assert.deepEqual(requestedFilters, ['', 'mms']);

  let attempts = 0;
  const retry = new PacketRepository({ request: async () => {
    if (++attempts === 1) throw new Error('Cancelled decoder job');
    return { packets: [{ frame: 1 }], total: 1 };
  } });
  await assert.rejects(retry.load(0), /Cancelled/);
  assert.equal(retry.pending.size, 0);
  await retry.load(0);
  assert.equal(retry.get(0).frame, 1);

  const windowStart = performance.now();
  let maxRenderedRows = 0;
  for (let n = 0; n < 100_000; n++) {
    const range = visibleRange({ scrollTop: n * 27, height: 900, total });
    assert(range.begin >= 0 && range.end >= range.begin && range.end <= total);
    maxRenderedRows = Math.max(maxRenderedRows, range.end - range.begin);
    assert.equal(range.before + (range.end - range.begin) * 27 + range.after, total * 27);
  }
  for (const reduced of [0, 1, 5]) {
    const range = visibleRange({ scrollTop: 99_999 * 27, height: 900, total: reduced });
    assert(range.begin <= range.end && range.end <= reduced, 'Filtering while scrolled must not produce an inverted visible range');
  }
  assert(maxRenderedRows < 100, 'Virtualized DOM range must remain bounded, independent of capture size');

  const result = {
    status: 'passed', module: 'src/capture/packet-repository.mjs', logical_frames: total,
    pages_loaded: 500, retained_pages: repo.pages.size, retained_rows: cachedRows,
    concurrent_page_requests: 50, actual_requests_for_concurrent_page: 1,
    mock_page_p95_ms: [...timings].sort((a, b) => a - b)[Math.ceil(timings.length * .95) - 1],
    visible_range_calls: 100_000, visible_range_elapsed_ms: performance.now() - windowStart,
    max_rendered_row_range: maxRenderedRows,
    note: 'Measures repository and virtual-range logic with a deterministic mock transport; these are not claims about native decoder throughput or painted frame rate.',
  };
  const output = path.join(root, '.tmp/capture-performance');
  fs.mkdirSync(output, { recursive: true });
  fs.writeFileSync(path.join(output, 'frontend-result.json'), JSON.stringify(result, null, 2));
  console.log(JSON.stringify(result, null, 2));
}

main().catch(error => { console.error(error); process.exitCode = 1; });

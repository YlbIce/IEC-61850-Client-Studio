const test = require("node:test");
const assert = require("node:assert/strict");
const fs = require("node:fs/promises");
const os = require("node:os");
const path = require("node:path");
const { HistoryStore } = require("../src/capture/ai/history-store");
test("history preserves individual answers and evidence across restart, search, delete and damaged summaries", async (t) => {
  const directory = await fs.mkdtemp(path.join(os.tmpdir(), "studio-history-"));
  t.after(() => fs.rm(directory, { recursive: true, force: true }));
  const store = new HistoryStore(directory);
  const first = await store.create({
    question: "MMS 跳变",
    at: "2026-09-10T01:00:00Z",
    status: "pending",
    context: { evidence: [] },
  });
  await store.write({
    ...first,
    status: "completed",
    answer: "220",
    context: {
      evidence: [
        {
          tool: "packet",
          arguments: { frame: 24 },
          result: { frame: 24, value: 220 },
        },
      ],
    },
  });
  const second = await store.create({
    question: "TCP 重连",
    at: "2026-09-10T02:00:00Z",
    status: "failed",
    answer: "超时",
  });
  const restarted = new HistoryStore(directory);
  assert.equal((await restarted.list()).total, 2);
  assert.equal(
    (await restarted.read(first.id)).context.evidence[0].result.value,
    220,
  );
  assert.equal((await restarted.list({ query: "重连" })).rows[0].id, second.id);
  await fs.writeFile(
    path.join(directory, first.id + ".summary.json"),
    "{broken",
  );
  assert.equal((await restarted.list({ query: "跳变" })).total, 1);
  await assert.rejects(restarted.read("../key"), /标识无效/);
  await restarted.remove(second.id);
  assert.equal((await restarted.list()).total, 1);
  await assert.rejects(restarted.read(second.id), /ENOENT/);
});

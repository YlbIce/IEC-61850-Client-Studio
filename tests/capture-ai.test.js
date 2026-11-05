const test = require("node:test"),
  assert = require("node:assert/strict"),
  fs = require("node:fs/promises"),
  os = require("node:os"),
  path = require("node:path");
const { AiSettings } = require("../src/capture/ai/settings");
const { complete } = require("../src/capture/ai/provider");
const { analyze, bounded } = require("../src/capture/ai/engine");
const { detect, validateOptions } = require("../src/capture/ai/jumps");
const { summarize } = require("../src/capture/ai/connections");
test("jumps separate streams, FC and source; missing time and no thresholds do not assert fault", () => {
  const point = (frame, value, time, stream = "0", source = "report") => ({
    frame,
    value,
    time,
    stream,
    source,
    reference: "x",
    fc: "MX",
  });
  const points = [
    point(1, 220, 0),
    point(2, 999, 0.1, "1"),
    point(3, 221, 1),
    point(4, 1000, 2),
    point(5, 1001, 2),
    point(6, 0, 3, "0", "read-response"),
  ];
  const r = detect(points);
  assert.equal(r.total_changes, 3);
  assert.equal(r.changes[0].delta, 779);
  assert(!r.changes.some((c) => c.threshold_exceeded));
  assert.equal(r.invalid_time_intervals, 1);
  assert.equal(r.changes.find((c) => c.to_frame === 5).absolute_rate, null);
  assert(detect(points, { absolute: 50 }).changes[0].threshold_exceeded);
  assert.throws(() => validateOptions({ reference: "x", rate: -1 }), /阈值/);
});
test("connection scan distinguishes repeated SYN in same stream from new stream candidates", () => {
  const p = (frame, stream, flags) => ({
    frame,
    layers: {
      ip: { "ip.src": "a", "ip.dst": "b" },
      tcp: {
        "tcp.stream": stream,
        "tcp.dstport": "102",
        "tcp.srcport": "4000",
        "tcp.flags_tree": flags,
      },
    },
  });
  const r = summarize([
    p(1, "0", { "tcp.flags.syn": "1" }),
    p(2, "0", { "tcp.flags.syn": "1" }),
    p(3, "0", { "tcp.flags.reset": "1" }),
    p(4, "1", { "tcp.flags.syn": "1" }),
  ]);
  assert.equal(r.streams.length, 2);
  assert.equal(r.repeated_connection_candidates[0].streams.length, 2);
  assert.equal(r.streams[0].syn_frames.length, 2);
  assert.equal(r.streams[0].rst_frames[0], 3);
});
const answer = (frames = [24]) => ({
  answer: "返回220",
  facts: [{ text: "读取值为220", frames }],
  limitations: ["无品质证据"],
  next_checks: ["核对显示"],
  filter: "",
});
const result = (v = answer()) => ({
  message: { content: JSON.stringify(v) },
  usage: { prompt_tokens: 10, completion_tokens: 5, total_tokens: 15 },
  model: "test-model",
});
const client = {
  request: async (cmd, args) => {
    assert.equal(cmd, "detail");
    return { frame: args.frame, values: [{ value: 220 }], correlation: {} };
  },
};
const options = {
  client,
  credentials: { key: "test-only-key", model: "test-model" },
  question: "解释",
  mode: "learn",
  frame: 24,
};
test("local key encrypted, never returned in status; preserve, clear and failure", async () => {
  const dir = await fs.mkdtemp(path.join(os.tmpdir(), "studio-ai-test-"));
  try {
    const secure = {
      isEncryptionAvailable: () => true,
      encryptString: (s) => Buffer.from(s.split("").reverse().join("")),
      decryptString: (b) => b.toString().split("").reverse().join(""),
    };
    const s = new AiSettings(dir, secure);
    await s.save({ key: "secret-api-key" });
    assert.equal((await s.credentials()).key, "secret-api-key");
    assert(!JSON.stringify(await s.status()).includes("secret-api-key"));
    assert(!(await fs.readFile(s.file, "utf8")).includes("secret-api-key"));
    await s.save({ model: "next-model", key: "" });
    assert.equal((await s.credentials()).key, "secret-api-key");
    secure.isEncryptionAvailable = () => false;
    await assert.rejects(s.save({ key: "replacement-key" }), /未保存/);
    assert.equal((await s.credentials()).key, "secret-api-key");
    await s.save({ clear: true });
    assert.equal((await s.status()).configured, false);
  } finally {
    await fs.rm(path.join(dir, "capture-ai.json"), { force: true });
    await fs.rmdir(dir);
  }
});
test("provider pins endpoint, disallows redirect, does not expose error body", async () => {
  await assert.rejects(
    complete({
      key: "secret-api-key",
      model: "test",
      messages: [],
      fetchImpl: async (url, init) => {
        assert.equal(url, "https://api.deepseek.com/chat/completions");
        assert.equal(init.redirect, "error");
        return new Response("secret-api-key", { status: 401 });
      },
    }),
    (e) => e.message === "API Key 无效",
  );
});
test("provider retains length marker, draft and usage for recovery", async () => {
  const r = await complete({
    key: "test",
    model: "test",
    messages: [],
    fetchImpl: async () =>
      Response.json({
        choices: [{ message: { content: "{}" }, finish_reason: "stop" }],
      }),
  });
  assert.equal(r.message.content, "{}");
  const truncated = await complete({
    key: "test",
    model: "test",
    messages: [],
    fetchImpl: async (_url, init) => {
      assert.equal(JSON.parse(init.body).max_tokens, 16384);
      return Response.json({
        choices: [
          {
            message: { content: '{"answer":"partial' },
            finish_reason: "length",
          },
        ],
        usage: { completion_tokens: 16384 },
      });
    },
  });
  assert.equal(truncated.finishReason, "length");
  assert.equal(truncated.message.content, '{"answer":"partial');
  assert.equal(truncated.usage.completion_tokens, 16384);
});
test("real evidence IDs, usage and unsupported references", async () => {
  const r = await analyze({ ...options, provider: async () => result() });
  assert.deepEqual(r.frames, [24]);
  assert.equal(r.usage.total_tokens, 15);
  await assert.rejects(
    analyze({ ...options, provider: async () => result(answer([999])) }),
    /未读取/,
  );
});
test("tool allowlist rejects device writes and continues read-only", async () => {
  let count = 0;
  await analyze({
    ...options,
    provider: async ({ messages }) => {
      if (count++ === 0)
        return {
          message: {
            content: "",
            tool_calls: [
              {
                id: "x",
                type: "function",
                function: { name: "Operate", arguments: "{}" },
              },
            ],
          },
        };
      assert(
        messages.some((m) => m.role === "tool" && m.content.includes("不允许")),
      );
      return result();
    },
  });
});
test("cancelled output cannot be accepted", async () => {
  const c = new AbortController();
  await assert.rejects(
    analyze({
      ...options,
      signal: c.signal,
      provider: async () => {
        c.abort();
        return result();
      },
    }),
    /取消/,
  );
});
test("bounded extraction removes raw bytes and paths", () => {
  const v = bounded({
    file: "secret.pcap",
    hex: "aabb",
    layers: {
      frame_raw: ["ab"],
      tcp: { "tcp.payload": "ab", "tcp.port": 102 },
    },
    values: [{ raw: "ab", value: "18446744073709551615" }],
  });
  assert(!JSON.stringify(v).includes("aabb"));
  assert(!JSON.stringify(v).includes("secret.pcap"));
  assert.equal(v.layers.tcp["tcp.payload"], undefined);
  assert.equal(v.values[0].value, "18446744073709551615");
});
test("malformed JSON does not become a displayed answer", async () => {
  await assert.rejects(
    analyze({
      ...options,
      provider: async () => ({ message: { content: "not json" } }),
    }),
    /结构化/,
  );
});

test("answer cannot overwrite local history identity and evidence snapshot matches tool data", async () => {
  const evidence = [];
  const r = await analyze({
    ...options,
    onEvidence: async (item) => evidence.push(item),
    provider: async () =>
      result({
        ...answer(),
        id: "model-controlled-id",
        captureFile: "fake-path",
        context: { key: "fake" },
      }),
  });
  assert.equal(r.id, undefined);
  assert.equal(r.captureFile, undefined);
  assert.equal(r.context, undefined);
  assert.equal(evidence[0].tool, "packet");
  assert.equal(evidence[0].result.frame, 24);
});

test("length recovery raises budget once and accounts both attempts", async () => {
  const requests = [];
  const r = await analyze({
    ...options,
    provider: async (input) => {
      requests.push(input);
      if (requests.length === 1) return { ...result(), finishReason: "length" };
      return result({ ...answer(), answer: "长".repeat(20000) });
    },
  });
  assert.equal(requests.length, 2);
  assert.equal(requests[0].maxTokens, 16384);
  assert.equal(requests[1].maxTokens, 32768);
  assert.equal(requests[1].thinking, false);
  assert.equal(r.usage.total_tokens, 30);
  assert.equal(r.answer.length, 20000);
  assert(!r.incomplete);
});

test("repeated truncation preserves only marked draft, never executes partial tools", async () => {
  let requests = 0;
  const r = await analyze({
    ...options,
    provider: async () => {
      requests++;
      return {
        message: {
          content: '{"answer":"已观察到 TCP 重连\\n尚需核查',
          reasoning_content: "PRIVATE REASONING",
          tool_calls: [
            {
              id: "x",
              function: { name: "packet", arguments: '{"frame":999}' },
            },
          ],
        },
        finishReason: "length",
        model: "test",
        usage: { total_tokens: 3 },
      };
    },
  });
  assert.equal(requests, 2);
  assert.equal(r.answer, "已观察到 TCP 重连\n尚需核查");
  assert.equal(r.incomplete, true);
  assert.deepEqual(r.facts, []);
  assert.equal(r.filter, "");
  assert.equal(r.usage.total_tokens, 6);
  assert(!JSON.stringify(r).includes("PRIVATE REASONING"));
});

test("length recovery failure keeps draft; cancellation still aborts", async () => {
  let calls = 0;
  const r = await analyze({
    ...options,
    provider: async () => {
      if (calls++ === 0) return { ...result(), finishReason: "length" };
      throw Error("连接失败");
    },
  });
  assert(r.incomplete);
  assert.equal(r.answer, "返回220");
  assert(r.limitations[0].includes("连接失败"));
  const abort = new AbortController();
  calls = 0;
  await assert.rejects(
    analyze({
      ...options,
      signal: abort.signal,
      provider: async () => {
        if (calls++ === 0) return { ...result(), finishReason: "length" };
        abort.abort();
        throw Error("停止");
      },
    }),
    /已取消/,
  );
});

test("draft handles split escapes and excludes reasoning-only content", () => {
  const { draftText } = require("../src/capture/ai/truncated");
  assert.equal(
    draftText('{"answer":"测试' + String.fromCharCode(92) + "u4e"),
    "测试",
  );
  assert.equal(draftText(null), "");
  assert.equal(draftText('{"reasoning_content":"不应展示"}'), "");
});

test("recovery stays non-thinking across tools and retry budget is per task", async () => {
  const requests = [];
  const r = await analyze({
    ...options,
    mode: "diagnose",
    provider: async (input) => {
      requests.push(input);
      if (requests.length === 2)
        return {
          message: {
            content: "",
            tool_calls: [
              {
                id: "recovery-tool",
                function: { name: "packet", arguments: '{"frame":24}' },
              },
            ],
          },
          finishReason: "tool_calls",
          model: "test",
          usage: { total_tokens: 2 },
        };
      return { ...result(), finishReason: "length" };
    },
  });
  assert.equal(requests.length, 3);
  assert.equal(requests[0].thinking, true);
  assert.equal(requests[1].thinking, false);
  assert.equal(requests[2].thinking, false);
  assert(r.incomplete);
});

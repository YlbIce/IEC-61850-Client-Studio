"use strict";
const {
    app,
    dialog,
    safeStorage,
    BrowserWindow,
    clipboard,
  } = require("electron"),
  fs = require("node:fs/promises"),
  path = require("node:path"),
  assert = require("node:assert/strict");
const root = path.resolve(__dirname, ".."),
  profile = path.join(root, ".tmp/ai-ui-profile");
app.setPath("userData", profile);
app.commandLine.appendSwitch("force-device-scale-factor", "1");
for (const s of [process.stdout, process.stderr]) s.on("error", () => {});
let networkCalls = 0;
let truncateMode = false;
global.fetch = async () => {
  networkCalls++;
  return new Response(
    JSON.stringify({
      model: "mock-transport",
      usage: { prompt_tokens: 100, completion_tokens: 50, total_tokens: 150 },
      choices: [
        {
          finish_reason: truncateMode ? "length" : "stop",
          message: {
            content: truncateMode
              ? '{"answer":"可见的未完成答案'
              : JSON.stringify({
                  answer:
                    "## MMS 读取交互\n\n第 **24** 帧返回 `220`。TCP 确认不等于业务成功。\n\n| 层次 | 含义 |\n| --- | --- |\n| TCP | 传输确认 |\n| MMS | 读取响应 |\n\n### 核查步骤\n\n1. 对照请求对象引用。\n2. 核查返回值与品质。\n\n```text\n22 请求 → 24 响应\n```\n\n<script>window.injected=true</script>",
                  facts: [
                    {
                      text: "第22帧请求与第24帧响应关联，返回220。",
                      frames: [22, 24],
                    },
                  ],
                  limitations: ["未提供品质和设备时间。"],
                  next_checks: ["核对客户端映射。"],
                  filter: "",
                }),
          },
        },
      ],
    }),
    { headers: { "content-type": "application/json" } },
  );
};
dialog.showOpenDialog = async () => ({
  canceled: false,
  filePaths: [path.join(root, "docs/blog/examples/sample-mms.pcapng")],
});
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
app.whenReady().then(async () => {
  let win;
  try {
    const { createCaptureWindow } = require("../src/capture/window");
    win = await createCaptureWindow({ root });
    const js = (s) => win.webContents.executeJavaScript(s);
    const wait = async (condition) => {
      for (let i = 0; i < 400; i++) {
        if (await js(condition)) return;
        await sleep(100);
      }
      throw Error("Timeout " + condition);
    };
    await wait('document.querySelectorAll("#ai-lesson option").length===4');
    await js('document.querySelector("[data-action=ai-settings]").click()');
    await wait('document.querySelector("#ai-settings-dialog").open');
    await js(
      'document.querySelector("#ai-key").value="local-test-placeholder-key";document.querySelector("#ai-settings-form").requestSubmit()',
    );
    await wait(
      'document.querySelector("#ai-settings-status").textContent.includes("已加密保存")',
    );
    const stored = await fs.readFile(
      path.join(profile, "capture-ai.json"),
      "utf8",
    );
    assert(!stored.includes("local-test-placeholder-key"));
    assert(safeStorage.isEncryptionAvailable());
    assert.equal(networkCalls, 0);
    await js(
      'document.querySelector("#ai-settings-close").click();document.querySelector("#mms-ports").value="18122";document.querySelector("[data-action=open]").click()',
    );
    await wait('document.querySelectorAll("[data-frame]").length>0');
    win.webContents.send("capture:action", { name: "ai-explain", frame: 24 });
    await wait(
      '!document.querySelector("#ai-panel").hidden && document.querySelector("#ai-question").value.includes("24")',
    );
    await js(
      'document.querySelector("#ai-course").open=false;document.querySelector("#ai-send").click()',
    );
    await sleep(200);
    assert.equal(networkCalls, 0);
    await js(
      'document.querySelector("#ai-consent").checked=true;document.querySelector("#ai-send").click()',
    );
    await wait(
      'document.querySelector("#ai-result").textContent.includes("mock-transport")',
    );
    assert.equal(networkCalls, 1);
    let answerWindow = BrowserWindow.getAllWindows().find((w) =>
      w.webContents.getURL().endsWith("/ai/answer.html"),
    );
    assert(answerWindow, "answer opens automatically");
    let answerJs = (source) =>
      answerWindow.webContents.executeJavaScript(source);
    for (let i = 0; i < 100; i++) {
      if (await answerJs('!!document.querySelector("#answer table")')) break;
      await sleep(100);
    }
    assert(answerWindow.getSize()[0] >= 1100);
    assert(
      await answerJs(
        '!!document.querySelector("#answer table") && !!document.querySelector("#answer pre") && !window.injected && !document.querySelector("#answer script")',
      ),
    );
    await answerJs(`document.querySelector('nav a[href="#context"]').click()`);
    assert.equal(await answerJs("location.hash"), "");
    await answerJs('window.answer.command("copy")');
    assert(clipboard.readText().includes("MMS 读取交互"));
    const exportPath = path.join(profile, "answer-test.md");
    dialog.showSaveDialog = async () => ({
      canceled: false,
      filePath: exportPath,
    });
    await answerJs('window.answer.command("export")');
    assert((await fs.readFile(exportPath, "utf8")).includes("帧 22, 24"));
    await assert.rejects(
      answerJs('window.answer.command("locate",999999)'),
      /帧号不在/,
    );
    const actionReceived = new Promise((resolve) => {
      const original = win.webContents.send.bind(win.webContents);
      win.webContents.send = (channel, payload) => {
        original(channel, payload);
        if (payload?.name === "ai-answer-locate") {
          win.webContents.send = original;
          resolve(payload);
        }
      };
    });
    await answerJs('document.querySelector("#facts button").click()');
    assert.equal((await actionReceived).frame, 22);
    await fs.mkdir(path.join(root, "docs/screenshots"), { recursive: true });
    await answerJs(
      'document.querySelector("main").scrollTo({top:0,behavior:"instant"})',
    );
    await fs.writeFile(
      path.join(root, "docs/screenshots/ai-answer-window-test.png"),
      (await answerWindow.webContents.capturePage()).toPNG(),
    );
    answerWindow.close();
    await js('window.capture.command("ai-show-answer")');
    answerWindow = BrowserWindow.getAllWindows().find((w) =>
      w.webContents.getURL().endsWith("/ai/answer.html"),
    );
    assert(answerWindow);
    assert.equal(networkCalls, 1, "reopening never calls AI");

    const jump = await js(
      'window.capture.command("ai-jumps",{reference:"STUDIOLD0/GGIO1.AnIn1.mag.f"})',
    );
    assert(jump.matching_frames > 0);
    assert.equal(jump.thresholds_configured, false);
    const connections = await js('window.capture.command("ai-connections")');
    assert(connections.events.length > 0);
    assert(connections.streams.length > 0);
    await fs.mkdir(path.join(root, "docs/screenshots"), { recursive: true });
    await fs.writeFile(
      path.join(root, "docs/screenshots/ai-panel-test.png"),
      (await win.webContents.capturePage()).toPNG(),
    );
    await js(
      'document.querySelector("#ai-consent").checked=false;document.querySelector("[data-action=open]").click()',
    );
    await wait('document.querySelector("#ai-result").textContent.length===0');
    for (let i = 0; i < 100; i++) {
      if (await answerJs('!document.querySelector("#stale").hidden')) break;
      await sleep(100);
    }
    assert(
      await answerJs(
        '!document.querySelector("#stale").hidden && [...document.querySelectorAll("#facts button")].every(b=>b.disabled)',
      ),
    );
    await assert.rejects(
      answerJs('window.answer.command("locate",22)'),
      /捕获文件已切换/,
    );
    await js('window.capture.command("ai-history")');
    const historyWindow = BrowserWindow.getAllWindows().find((w) =>
      w.webContents.getURL().endsWith("/ai/history.html"),
    );
    assert(historyWindow);
    const historyJs = (source) =>
      historyWindow.webContents.executeJavaScript(source);
    const historyList = await historyJs('window.historyApi.command("list")');
    const saved = historyList.rows.find((r) => r.status === "completed");
    assert(saved);
    const { HistoryStore } = require("../src/capture/ai/history-store");
    const restartedStore = new HistoryStore(path.join(profile, "ai-history"));
    const persisted = await restartedStore.read(saved.id);
    assert(
      persisted.context.evidence.some(
        (e) => e.tool === "packet" && e.result.frame === 24,
      ),
    );
    assert(persisted.context.source.size > 0);
    assert.deepEqual(persisted.context.mmsPorts, [18122]);
    assert(!JSON.stringify(persisted).includes("local-test-placeholder-key"));
    await historyJs(
      `window.historyApi.command("open",{id:${JSON.stringify(saved.id)}})`,
    );
    assert.equal(networkCalls, 1, "history reads remain offline");
    await sleep(200);
    assert(
      await answerJs(
        'document.querySelectorAll("#context-data details").length>=3',
      ),
    );
    assert(await answerJs('!document.querySelector("#stale").hidden'));
    await fs.writeFile(
      path.join(root, "docs/screenshots/ai-history-window-test.png"),
      (await historyWindow.webContents.capturePage()).toPNG(),
    );
    await assert.rejects(
      historyJs('window.historyApi.command("open",{id:"../../capture-ai"})'),
      /标识无效/,
    );
    dialog.showMessageBox = async () => ({ response: 0 });
    assert.equal(
      (
        await historyJs(
          `window.historyApi.command("delete",{id:${JSON.stringify(saved.id)}})`,
        )
      ).deleted,
      false,
    );
    dialog.showMessageBox = async () => ({ response: 1 });
    assert.equal(
      (
        await historyJs(
          `window.historyApi.command("delete",{id:${JSON.stringify(saved.id)}})`,
        )
      ).deleted,
      true,
    );
    await assert.rejects(restartedStore.read(saved.id), /ENOENT/);
    truncateMode = true;
    const partial = await js(
      'window.capture.command("ai-run",{mode:"learn",question:"截断桌面回归",frame:24,allowCloud:true})',
    );
    assert.equal(partial.incomplete, true);
    assert.equal(networkCalls, 3);
    answerWindow = BrowserWindow.getAllWindows().find((w) =>
      w.webContents.getURL().endsWith("/ai/answer.html"),
    );
    for (let i = 0; i < 100; i++) {
      if (await answerJs('!document.querySelector("#incomplete").hidden'))
        break;
      await sleep(100);
    }
    assert(
      await answerJs(
        '!document.querySelector("#incomplete").hidden && document.querySelector("#answer").textContent.includes("可见的未完成答案") && document.querySelectorAll("#facts button").length===0',
      ),
    );
    const partialList = await historyJs(
      'window.historyApi.command("list",{query:"截断桌面回归"})',
    );
    assert.equal(partialList.rows[0].status, "incomplete");
    const savedPartial = await restartedStore.read(partialList.rows[0].id);
    assert(savedPartial.incomplete && savedPartial.context.evidence.length > 0);
    await js('window.capture.command("ai-save",{clear:true})');
    assert.equal(
      (await js('window.capture.command("ai-settings")')).configured,
      false,
    );
    await fs.writeFile(
      path.join(root, "capture-ai-ui-result.json"),
      JSON.stringify(
        {
          passed: true,
          transport: "mock only, no DeepSeek call",
          dpapi: true,
          cloudConsent: true,
          evidenceNavigation: true,
          jumpScan: jump,
          connectionScan: connections,
        },
        null,
        2,
      ),
    );
    win.destroy();
    assert(answerWindow.isDestroyed(), "owner closes answer window");
    assert(historyWindow.isDestroyed(), "owner closes history manager");
    app.quit();
  } catch (e) {
    await fs.writeFile(
      path.join(root, "capture-ai-ui-result.json"),
      JSON.stringify({ passed: false, error: e.stack }),
    );
    win?.destroy();
    app.exit(1);
  }
});

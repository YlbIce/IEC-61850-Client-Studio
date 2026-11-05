export function installAiPanel({ api, context, locate, applyFilter }) {
  const $ = (s) => document.querySelector(s);
  let courses = [],
    active = null,
    epoch = 0,
    busy = false,
    lastResult = null;
  const text = (s, v) => ($(s).textContent = v);
  const status = (m, error = false) => {
    text("#ai-status", m);
    $("#ai-status").classList.toggle("error", error);
  };
  const show = () => {
    $("#ai-panel").hidden = false;
  };
  async function settings() {
    try {
      const s = await api.command("ai-settings");
      text(
        "#ai-settings-status",
        s.configured ? "已保存 Key（不会回显）" : "尚未保存 Key",
      );
      text("#ai-settings-path", "保存位置：" + s.path);
      $("#ai-model").value = s.model;
      $("#ai-key").value = "";
      $("#ai-settings-dialog").showModal();
    } catch (e) {
      show();
      status(e.message, true);
    }
  }
  function course() {
    active = courses.find((c) => c.id === $("#ai-lesson").value);
    if (!active) return;
    text("#ai-lesson-text", active.text);
    $("#ai-quiz").replaceChildren();
    const p = document.createElement("p");
    p.textContent = active.question;
    $("#ai-quiz").append(p);
    active.choices.forEach((choice, i) => {
      const l = document.createElement("label"),
        r = document.createElement("input");
      r.type = "radio";
      r.name = "ai-quiz-choice";
      r.value = i;
      l.append(r, document.createTextNode(choice));
      $("#ai-quiz").append(l);
    });
    text("#ai-feedback", "");
  }
  function prepare(kind, field) {
    show();
    $("#ai-mode").value = "learn";
    const n = context().frame;
    $("#ai-question").value = field
      ? `解释所选第 ${n || "?"} 帧中的字段“${field.slice(0, 500)}”，区分实际字段与协议知识。`
      : kind === "walk"
        ? `带我逐步看懂所选第 ${n || "?"} 帧对应的请求响应或TCP交互。先解释证据，再给一道练习；缺少上下文请指出。`
        : `解释所选第 ${n || "?"} 帧的层次、业务含义与关键字段。`;
    $("#ai-question").focus();
  }
  function render(r) {
    lastResult = r;
    const root = $("#ai-result");
    root.replaceChildren();
    const p = document.createElement("p");
    p.textContent = `答案已在独立窗口中显示 · ${r.model} · ${r.facts.length} 条证据事实`;
    const button = document.createElement("button");
    button.textContent = "打开答案窗口";
    button.onclick = () =>
      api.command("ai-show-answer").catch((e) => status(e.message, true));
    root.append(p, button);
  }

  async function send(extra = {}) {
    if (busy) return;
    if (!$("#ai-consent").checked) {
      status("请先确认云端发送范围；离线课程无需授权或 Key。", true);
      return;
    }
    const task = ++epoch;
    busy = true;
    $("#ai-send").disabled = true;
    $("#ai-stop").disabled = false;
    $("#ai-result").replaceChildren();
    lastResult = null;
    try {
      const c = context();
      text(
        "#ai-scope",
        c.frame
          ? `本次固定帧 ${c.frame}，按需查询当前文件；新问题使用新选择。`
          : "本次没有选中帧；可询问基础知识或查询当前文件。",
      );
      const r = await api.command("ai-run", {
        mode: $("#ai-mode").value,
        question: $("#ai-question").value,
        frame: c.frame || null,
        allowCloud: true,
        ...extra,
      });
      if (task === epoch) {
        render(r);
        status(
          r.incomplete
            ? "答案未完成，草稿和上下文已保存到历史。"
            : "完成。事实引用已检查，业务判断仍请核对。",
          r.incomplete,
        );
      }
    } catch (e) {
      if (task === epoch) status(e.message, true);
    } finally {
      busy = false;
      $("#ai-send").disabled = false;
      $("#ai-stop").disabled = true;
    }
  }
  $("#ai-close").onclick = () => {
    $("#ai-panel").hidden = true;
  };
  $("#ai-config").onclick = settings;
  $("#ai-history").onclick = () =>
    api.command("ai-history").catch((e) => status(e.message, true));
  $("#ai-explain").onclick = () => prepare("explain");
  $("#ai-walk").onclick = () => prepare("walk");
  $("#ai-send").onclick = () => send();
  const jumpOptions = () => ({
    reference: $("#ai-jump-reference").value.trim(),
    absolute:
      $("#ai-jump-absolute").value === ""
        ? null
        : Number($("#ai-jump-absolute").value),
    rate:
      $("#ai-jump-rate").value === "" ? null : Number($("#ai-jump-rate").value),
  });
  $("#ai-jump-selected").onclick = () => {
    const v = context().values?.find(
      (v) =>
        v.reference &&
        typeof v.value === "number" &&
        ["ST", "MX"].includes(v.fc),
    );
    if (v) $("#ai-jump-reference").value = v.reference;
    else status("所选帧没有可用数值引用，请填写完整 DA 引用。", true);
  };
  async function localScan(command, args, target) {
    if (busy) return;
    const task = ++epoch;
    busy = true;
    $("#ai-send").disabled = true;
    $("#ai-stop").disabled = false;
    status("正在执行本地只读扫描，不调用云端…");
    try {
      const r = await api.command(command, args);
      if (task !== epoch) return;
      const root = $(target);
      root.replaceChildren();
      const summary = document.createElement("p");
      summary.textContent =
        command === "ai-jumps"
          ? `${r.interpretation}。检查 ${r.examined_events}/${r.total_events} 个事件，匹配 ${r.matching_frames} 帧，发现 ${r.total_changes} 次变化。`
          : `检查 ${r.examined_frames}/${r.total_matching_frames} 帧连接控制报文，${r.streams.length} 个 TCP 流，${r.repeated_connection_candidates.length} 组重复连接候选。`;
      root.append(summary);
      const table = document.createElement("table"),
        body = document.createElement("tbody");
      const rows =
        command === "ai-jumps"
          ? r.changes.map((c) => [
              `帧 ${c.from_frame} → ${c.to_frame}`,
              `${c.from} → ${c.to}`,
              `Δ ${c.delta} / ${c.interval_seconds}s`,
              c.threshold_exceeded ? "超阈值候选" : "观察变化",
            ])
          : r.streams.map((s) => [
              `流 ${s.stream}`,
              `SYN ${s.syn_frames.length} / SYN-ACK ${s.syn_ack_frames.length}`,
              `FIN ${s.fin_frames.length} / RST ${s.rst_frames.length}`,
              s.syn_frames.length ? "见建连尝试" : "未见初始SYN",
            ]);
      for (const values of rows) {
        const row = document.createElement("tr");
        for (const value of values) {
          const cell = document.createElement("td");
          cell.textContent = value;
          row.append(cell);
        }
        body.append(row);
      }
      table.append(body);
      root.append(table);
      const scope = document.createElement("p");
      scope.textContent = r.scope;
      root.append(scope);
      for (const limit of r.limits || []) {
        const p = document.createElement("p");
        p.textContent = limit;
        root.append(p);
      }
      const frames =
        command === "ai-jumps"
          ? [...new Set(r.changes.flatMap((c) => [c.from_frame, c.to_frame]))]
          : r.events.map((e) => e.frame);
      for (const n of frames.slice(0, 30)) {
        const b = document.createElement("button");
        b.textContent = "帧 " + n;
        b.onclick = () => locate(n).catch((e) => status(e.message, true));
        root.append(b);
      }
      status(
        r.partial
          ? "扫描完成，仅覆盖部分事件，详见范围。"
          : "本地扫描完成，未发送云端。",
      );
    } catch (e) {
      if (task === epoch) status(e.message, true);
    } finally {
      busy = false;
      $("#ai-send").disabled = false;
      $("#ai-stop").disabled = true;
    }
  }
  $("#ai-jump-scan").onclick = () =>
    localScan("ai-jumps", jumpOptions(), "#ai-jump-result");
  $("#ai-jump-analyze").onclick = () => {
    $("#ai-mode").value = "diagnose";
    $("#ai-question").value =
      "请重点分析此对象的数据跳变候选，检查变化幅度、速率、品质与来源，列出证据及需要补查的原因。";
    send({ jump: jumpOptions() });
  };
  $("#ai-connection-scan").onclick = () =>
    localScan("ai-connections", {}, "#ai-connection-result");
  $("#ai-connection-analyze").onclick = () => {
    $("#ai-mode").value = "diagnose";
    $("#ai-question").value =
      "请调查客户端与服务端断连重连异常，区分正常关闭、RST、SYN重传、并发连接与重连候选；核查前后MMS交互并明确证据缺口。";
    send({ connections: true });
  };
  $("#ai-stop").onclick = async () => {
    epoch++;
    await api.command("ai-cancel");
    status("已取消；正在结束本地查询时请稍候。");
  };
  $("#ai-export").onclick = async () => {
    try {
      if (!lastResult) throw Error("没有可导出的结果");
      await api.command("ai-export");
    } catch (e) {
      status(e.message, true);
    }
  };
  $("#ai-lesson").onchange = course;
  $("#ai-grade").onclick = () => {
    const choice = document.querySelector("input[name=ai-quiz-choice]:checked");
    text(
      "#ai-feedback",
      !choice
        ? "请先选择答案。"
        : Number(choice.value) === active.answer
          ? "回答正确。" + active.explanation
          : "再想一想。" + active.explanation,
    );
  };
  $("#ai-settings-close").onclick = () => {
    $("#ai-key").value = "";
    $("#ai-settings-dialog").close();
  };
  $("#ai-settings-dialog").addEventListener("close", () => {
    $("#ai-key").value = "";
  });
  $("#ai-settings-form").onsubmit = async (e) => {
    e.preventDefault();
    const key = $("#ai-key").value;
    $("#ai-key").value = "";
    try {
      const s = await api.command("ai-save", {
        key,
        model: $("#ai-model").value.trim(),
      });
      text(
        "#ai-settings-status",
        s.configured
          ? "已加密保存，可测试连接。"
          : "模型已保存，请填写 API Key。",
      );
    } catch (error) {
      text("#ai-settings-status", error.message);
    }
  };
  $("#ai-clear-key").onclick = async () => {
    try {
      await api.command("ai-save", { clear: true });
      $("#ai-key").value = "";
      text("#ai-settings-status", "本地 Key 已清除");
    } catch (e) {
      text("#ai-settings-status", e.message);
    }
  };
  $("#ai-test").onclick = async () => {
    const b = $("#ai-test");
    b.disabled = true;
    text("#ai-settings-status", "正在测试，仅发送固定测试问题…");
    try {
      const r = await api.command("ai-test");
      text("#ai-settings-status", "连接成功：" + r.model);
    } catch (e) {
      text("#ai-settings-status", e.message);
    } finally {
      b.disabled = false;
    }
  };
  document.querySelector("#protocol-tree").addEventListener("dblclick", (e) => {
    const node = e.target.closest(".leaf,summary");
    if (node) prepare("field", node.textContent);
  });
  api.onAiProgress(({ message }) => {
    if (busy) status(message);
  });
  api
    .command("ai-lessons")
    .then((items) => {
      courses = items;
      for (const c of items) {
        const o = document.createElement("option");
        o.value = c.id;
        o.textContent = c.title;
        $("#ai-lesson").append(o);
      }
      course();
    })
    .catch((e) => status(e.message, true));
  return {
    action(name) {
      if (name === "ai-history") return api.command("ai-history");
      if (name === "ai-settings") return settings();
      if (name === "ai-toggle") {
        $("#ai-panel").hidden = !$("#ai-panel").hidden;
        return;
      }
      prepare(name === "ai-walkthrough" ? "walk" : "explain");
    },
    invalidate() {
      epoch++;
      lastResult = null;
      $("#ai-result").replaceChildren();
      $("#ai-jump-result").replaceChildren();
      $("#ai-connection-result").replaceChildren();
      $("#ai-consent").checked = false;
      status("文件已切换，旧结果已清除；新文件需重新确认发送范围。");
    },
  };
}

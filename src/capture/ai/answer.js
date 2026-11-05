const $ = (s) => document.querySelector(s);
let displayedId;
async function command(action, value) {
  try {
    const message = await window.answer.command(action, value);
    if (typeof message === "string") $("#status").textContent = message;
  } catch (e) {
    $("#status").textContent = e.message;
  }
}
document
  .querySelectorAll("[data-command]")
  .forEach((b) => (b.onclick = () => command(b.dataset.command)));
document.querySelectorAll("nav a").forEach((link) => {
  link.onclick = (event) => {
    event.preventDefault();
    document
      .querySelector(link.getAttribute("href"))
      .scrollIntoView({ behavior: "smooth" });
  };
});
function render(r) {
  if (displayedId !== r.id) $("main").scrollTo({ top: 0, behavior: "instant" });
  displayedId = r.id;
  $("#question").textContent = r.question;
  $("#meta").textContent =
    `${r.model} · ${new Date(r.at).toLocaleString()} · 输入 ${r.usage.prompt_tokens || 0} / 输出 ${r.usage.completion_tokens || 0} tokens`;
  $("#source").textContent =
    "捕获文件：" + (r.captureFile || "基础知识学习，未选择捕获文件");
  // HTML is generated exclusively by main-process Markdown rendering with HTML, links and images disabled.
  $("#answer").innerHTML = r.html;
  $("#stale").hidden = r.current;
  $("#incomplete").hidden = !r.incomplete;
  $("#facts").replaceChildren();
  for (const fact of r.facts) {
    const box = document.createElement("div");
    box.className = "fact";
    const p = document.createElement("p");
    p.textContent = fact.text;
    box.append(p);
    for (const frame of fact.frames) {
      const b = document.createElement("button");
      b.textContent = "帧 " + frame;
      b.disabled = !r.current;
      b.onclick = () => command("locate", frame);
      box.append(b);
    }
    $("#facts").append(box);
  }
  for (const [selector, items] of [
    ["#limitations", [...r.limitations, r.coverage]],
    ["#checks", r.next_checks],
  ]) {
    $(selector).replaceChildren();
    for (const text of items) {
      const li = document.createElement("li");
      li.textContent = text;
      $(selector).append(li);
    }
  }
  $("#context-data").replaceChildren();
  const info = document.createElement("p");
  info.textContent = `状态：${r.status || "completed"} · 选中帧：${r.selectedFrame || "无"} · MMS 端口：${(r.context?.mmsPorts || []).join(", ")}`;
  $("#context-data").append(info);
  const scope = document.createElement("details"),
    label = document.createElement("summary"),
    scopeText = document.createElement("pre");
  label.textContent = "捕获来源与分析参数";
  scopeText.textContent = JSON.stringify(
    {
      source: r.context?.source,
      selectedFrame: r.selectedFrame,
      mmsPorts: r.context?.mmsPorts,
      jump: r.context?.jump,
      connections: r.context?.connections,
    },
    null,
    2,
  );
  scope.append(label, scopeText);
  $("#context-data").append(scope);
  for (const item of r.context?.evidence || []) {
    const details = document.createElement("details"),
      summary = document.createElement("summary"),
      pre = document.createElement("pre");
    summary.textContent = `${item.tool} · ${JSON.stringify(item.arguments)}`;
    pre.textContent = JSON.stringify(item.result, null, 2);
    details.append(summary, pre);
    $("#context-data").append(details);
  }
  $("#filter").hidden = !r.filter;
  $("#filter").disabled = !r.current;
  $("#filter").textContent = "应用建议筛选：" + (r.filter || "");
  $("#filter").onclick = () => command("filter");
}
window.answer.onUpdate(render);
window.answer
  .command("read")
  .then(render)
  .catch((e) => ($("#status").textContent = e.message));

const $ = (s) => document.querySelector(s);
let offset = 0,
  epoch = 0,
  timer;
async function load() {
  const task = ++epoch;
  try {
    const r = await window.historyApi.command("list", {
      query: $("#search").value,
      offset,
    });
    if (task !== epoch) return;
    $("#records").replaceChildren();
    for (const item of r.rows) {
      const tr = document.createElement("tr");
      const cell = (text, sub) => {
        const td = document.createElement("td");
        td.textContent = text;
        if (sub) {
          const s = document.createElement("small");
          s.textContent = sub;
          td.append(s);
        }
        tr.append(td);
        return td;
      };
      cell(
        new Date(item.at).toLocaleString(),
        { learn: "报文学习", diagnose: "故障诊断", search: "证据查询" }[
          item.mode
        ] || item.mode,
      );
      cell(item.question, item.captureFile || "基础知识学习");
      cell(
        {
          completed: "已完成",
          incomplete: "未完成草稿",
          pending: "未完成 / 运行中",
          failed: "失败",
          cancelled: "已取消",
        }[item.status] || item.status,
        `${item.frames.length} 个证据帧 · ${item.model || ""}`,
      );
      const controls = cell("");
      for (const [label, action] of [
        ["打开", "open"],
        ["删除", "delete"],
      ]) {
        const b = document.createElement("button");
        b.textContent = label;
        b.onclick = async () => {
          try {
            await window.historyApi.command(action, { id: item.id });
            if (action === "delete") await load();
          } catch (e) {
            $("#status").textContent = e.message;
          }
        };
        controls.append(b);
      }
      tr.ondblclick = () =>
        window.historyApi
          .command("open", { id: item.id })
          .catch((e) => ($("#status").textContent = e.message));
      $("#records").append(tr);
    }
    $("#empty").hidden = r.rows.length > 0;
    $("#count").textContent =
      `共 ${r.total} 条 · 第 ${Math.floor(offset / 50) + 1} 页`;
    $("#prev").disabled = offset === 0;
    $("#next").disabled = offset + 50 >= r.total;
    $("#status").textContent =
      `本地保存：${r.path}` +
      (r.unreadable ? ` · ${r.unreadable} 条记录无法读取` : "");
  } catch (e) {
    $("#status").textContent = e.message;
  }
}
$("#search").oninput = () => {
  clearTimeout(timer);
  timer = setTimeout(() => {
    offset = 0;
    load();
  }, 200);
};
$("#refresh").onclick = () => load();
$("#prev").onclick = () => {
  offset = Math.max(0, offset - 50);
  load();
};
$("#next").onclick = () => {
  offset += 50;
  load();
};
window.historyApi.onUpdate(load);
load();

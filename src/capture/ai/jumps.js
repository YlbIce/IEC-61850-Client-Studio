"use strict";
function validateOptions({ reference, absolute = null, rate = null } = {}) {
  if (
    typeof reference !== "string" ||
    !reference.trim() ||
    reference.length > 512
  )
    throw Error("请填写要检查的完整数据属性引用（DA）");
  for (const v of [absolute, rate])
    if (v !== null && (typeof v !== "number" || !Number.isFinite(v) || v <= 0))
      throw Error("跳变阈值必须为正数，留空表示只观察变化");
  return { reference: reference.trim(), absolute, rate };
}
function detect(points, { absolute = null, rate = null } = {}) {
  const previous = new Map(),
    changes = [];
  let invalidTime = 0,
    skipped = 0;
  for (const p of points) {
    if (
      typeof p.value !== "number" ||
      !Number.isFinite(p.value) ||
      !Number.isFinite(p.time) ||
      p.stream === undefined ||
      p.stream === null
    ) {
      skipped++;
      continue;
    }
    const key = JSON.stringify([p.stream, p.reference, p.fc, p.source]);
    const old = previous.get(key);
    previous.set(key, p);
    if (!old) continue;
    const delta = p.value - old.value,
      dt = p.time - old.time;
    if (!Number.isFinite(delta)) {
      skipped++;
      continue;
    }
    const speed = dt > 0 ? Math.abs(delta) / dt : null;
    if (dt <= 0) invalidTime++;
    if (delta === 0) continue;
    changes.push({
      from_frame: old.frame,
      to_frame: p.frame,
      stream: p.stream,
      fc: p.fc,
      source: p.source,
      from: old.value,
      to: p.value,
      delta,
      interval_seconds: dt,
      absolute_rate: speed !== null && Number.isFinite(speed) ? speed : null,
      threshold_exceeded:
        (absolute !== null && Math.abs(delta) > absolute) ||
        (rate !== null &&
          speed !== null &&
          Number.isFinite(speed) &&
          speed > rate),
      quality_before: old.quality ?? "未提供",
      quality_after: p.quality ?? "未提供",
      time_basis: "抓包相对时间，不是IED采样间隔",
    });
  }
  changes.sort(
    (a, b) =>
      Number(b.threshold_exceeded) - Number(a.threshold_exceeded) ||
      Math.abs(b.delta) - Math.abs(a.delta),
  );
  return {
    changes: changes.slice(0, 20),
    total_changes: changes.length,
    invalid_time_intervals: invalidTime,
    skipped_numeric_or_stream: skipped,
    thresholds_configured: absolute !== null || rate !== null,
    interpretation:
      absolute === null && rate === null
        ? "只列出最大变化，未设置异常判据"
        : "超阈值仅为候选，不能据此确认设备故障",
  };
}
async function scanJumps(client, input, signal) {
  const options = validateOptions(input),
    points = [];
  let offset = 0,
    total = 0,
    matched = 0,
    stopped = false;
  const check = () => {
    if (signal?.aborted) throw Error("跳变扫描已取消");
  };
  while (offset < 1000 && !stopped) {
    check();
    const page = await client.request("replay", { offset, limit: 100 });
    check();
    total = page.total || 0;
    const events = page.events || [];
    if (!events.length) break;
    for (const e of events) {
      offset++;
      const values = (e.values || []).filter(
        (v) =>
          v.reference === options.reference &&
          v.complete === true &&
          v.mapping === "resolved" &&
          ["ST", "MX"].includes(v.fc) &&
          ["read-response", "report"].includes(v.source) &&
          typeof v.value === "number",
      );
      if (!values.length) continue;
      check();
      const d = await client.request("detail", { frame: e.frame });
      check();
      const tcp = d.layers?.tcp;
      const stream = tcp?.["tcp.stream"] ?? d.correlation?.stream;
      for (const v of values)
        points.push({
          frame: e.frame,
          time: e.relative_time,
          stream,
          reference: v.reference,
          fc: v.fc,
          source: v.source,
          value: v.value,
          quality: v.quality,
        });
      if (++matched >= 120) {
        stopped = true;
        break;
      }
    }
    if (offset >= total) break;
  }
  return {
    reference: options.reference,
    ...detect(points, options),
    examined_events: offset,
    total_events: total,
    matching_frames: matched,
    partial: offset < total,
    scope:
      "最多扫描前1000个语义事件/120个匹配帧；同一TCP流、FC、来源分别比较，不跨客户端混合。",
    limits: [
      "仅完整且已解析的ST/MX数值；整数超过JS安全范围不转换。",
      "抓包时间不等于采样时间，缺少品质保持未知。",
      "本次扫描不能识别所有突刺、量程/比例错误或周期性异常。",
    ],
  };
}
module.exports = { validateOptions, detect, scanJumps };

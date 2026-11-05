"use strict";
const bit = (v) => v === true || v === 1 || v === "1" || v === "True";
function summarize(details) {
  const streams = new Map(),
    events = [];
  for (const d of details) {
    const t = d.layers?.tcp || {},
      ip = d.layers?.ip || d.layers?.ipv6 || {};
    const stream = t["tcp.stream"];
    if (stream === undefined) continue;
    const flags = t["tcp.flags_tree"] || t;
    const syn = bit(flags["tcp.flags.syn"]),
      ack = bit(flags["tcp.flags.ack"]),
      fin = bit(flags["tcp.flags.fin"]),
      rst = bit(flags["tcp.flags.reset"]);
    const src = ip["ip.src"] || ip["ipv6.src"],
      dst = ip["ip.dst"] || ip["ipv6.dst"];
    const s = streams.get(String(stream)) || {
      stream: String(stream),
      syn_frames: [],
      syn_ack_frames: [],
      fin_frames: [],
      rst_frames: [],
    };
    if (syn && !ack) {
      s.syn_frames.push(d.frame);
      s.client = src;
      s.server = dst;
      s.server_port = t["tcp.dstport"];
    }
    if (syn && ack) s.syn_ack_frames.push(d.frame);
    if (fin) s.fin_frames.push(d.frame);
    if (rst) s.rst_frames.push(d.frame);
    streams.set(String(stream), s);
    events.push({
      frame: d.frame,
      stream: String(stream),
      source: src,
      destination: dst,
      source_port: t["tcp.srcport"],
      destination_port: t["tcp.dstport"],
      syn,
      ack,
      fin,
      rst,
    });
  }
  const attempts = new Map();
  for (const s of streams.values())
    if (s.syn_frames.length) {
      const key = JSON.stringify([s.client, s.server, s.server_port]);
      const a = attempts.get(key) || {
        client: s.client,
        server: s.server,
        server_port: s.server_port,
        streams: [],
      };
      a.streams.push(s.stream);
      attempts.set(key, a);
    }
  return {
    streams: [...streams.values()],
    repeated_connection_candidates: [...attempts.values()].filter(
      (a) => a.streams.length > 1,
    ),
    events,
    limits: [
      "SYN重传按同一TCP流去重，不作为新连接。",
      "见SYN/ACK不证明MMS关联成功；FIN可能是正常关闭，RST也不直接代表某端软件缺陷。",
      "没有响应可能是抓包缺失或观察窗口结束；本结果不推断超时定责。",
      "相同端点多流可能是并发连接，也可能是重连，需核对时间与关闭顺序。",
    ],
  };
}
async function scanConnections(client, signal) {
  const list = await client.request("packets", {
    filter: "tcp.flags.syn == 1 || tcp.flags.fin == 1 || tcp.flags.reset == 1",
    offset: 0,
    limit: 120,
  });
  const details = [];
  for (const p of list.packets || []) {
    if (signal?.aborted) throw Error("连接扫描已取消");
    details.push(await client.request("detail", { frame: p.frame }));
  }
  if (signal?.aborted) throw Error("连接扫描已取消");
  return {
    ...summarize(details),
    examined_frames: details.length,
    total_matching_frames: list.total,
    partial: list.total > details.length,
    scope: "最多前120帧连接控制报文；不等于完整TCP/MMS状态机诊断",
  };
}
module.exports = { summarize, scanConnections };

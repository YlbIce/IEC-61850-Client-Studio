"use strict";
const { app, safeStorage, dialog } = require("electron");
const fs = require("node:fs/promises");
const { AiSettings } = require("./settings");
const { analyze } = require("./engine");
const { complete } = require("./provider");
const { lessons } = require("./knowledge");
const { scanJumps } = require("./jumps");
const { scanConnections } = require("./connections");
const answers = require("./answer-window");
const histories = require("./history-window");
let settings;
function store() {
  return (settings ||= new AiSettings(app.getPath("userData"), safeStorage));
}
function invalidate(entry) {
  entry.aiController?.abort();
  entry.aiGeneration = (entry.aiGeneration || 0) + 1;
  entry.aiResult = null;
  answers.invalidate(entry);
}
async function command(entry, name, args = {}) {
  if (!args || typeof args !== "object" || Array.isArray(args))
    throw Error("AI 参数无效");
  if (name === "ai-history") {
    await histories.show(entry);
    return { shown: true };
  }
  if (name === "ai-show-answer") {
    await answers.show(entry);
    return { shown: true };
  }
  if (name === "ai-settings") return store().status();
  if (name === "ai-save") {
    invalidate(entry);
    return store().save(args);
  }
  if (name === "ai-lessons") return lessons;
  if (name === "ai-jumps" || name === "ai-connections") {
    if (!entry.captureFile || entry.capturing)
      throw Error("请先打开离线捕获文件");
    if (entry.aiBusy) throw Error("请等待当前AI任务完成");
    const controller = new AbortController();
    entry.aiController = controller;
    entry.aiBusy = true;
    try {
      return name === "ai-jumps"
        ? await scanJumps(entry.client, args, controller.signal)
        : await scanConnections(entry.client, controller.signal);
    } finally {
      entry.aiBusy = false;
      if (entry.aiController === controller) entry.aiController = null;
    }
  }
  if (name === "ai-cancel") {
    invalidate(entry);
    return { cancelled: true };
  }
  if (name === "ai-test") {
    const c = await store().credentials();
    const result = await complete({
      ...c,
      messages: [
        {
          role: "user",
          content:
            'Return json: {"ok":true}. This is a connection test. No capture data.',
        },
      ],
    });
    if (result.finishReason === "length")
      throw Error("连接成功，但测试回复未完成");
    return { ok: true, model: result.model, usage: result.usage };
  }
  if (name === "ai-export") {
    if (!entry.aiResult) throw Error("没有可导出的 AI 结果");
    const result = entry.aiResult;
    const selected = await dialog.showSaveDialog(entry.window, {
      title: "导出 AI 分析与学习记录",
      defaultPath: "AI分析记录.md",
      filters: [{ name: "Markdown", extensions: ["md"] }],
    });
    if (selected.canceled) return null;
    const text = answers.report(result);
    await fs.writeFile(selected.filePath, text, "utf8");
    return { saved: true };
  }
  if (name === "ai-run") {
    if (args.allowCloud !== true)
      throw Error("请勾选允许发送当前问题及按需报文证据到 DeepSeek");
    if (entry.capturing) throw Error("请停止捕获后进行 AI 分析");
    if (entry.aiBusy) throw Error("上一项 AI 任务仍在结束，请稍后重试");
    const credentials = await store().credentials();
    if (
      typeof args.question !== "string" ||
      !args.question.trim() ||
      args.question.length > 4000
    )
      throw Error("请输入1至4000字符的问题");
    const generation = entry.aiGeneration || 0;
    const controller = new AbortController();
    entry.aiController = controller;
    entry.aiBusy = true;
    entry.aiResult = null;
    const timer = setTimeout(() => controller.abort(), 360000);
    const evidence = [];
    let record;
    try {
      const source = entry.captureFile
        ? await fs
            .stat(entry.captureFile)
            .then((s) => ({
              path: entry.captureFile,
              size: s.size,
              modifiedAt: s.mtime.toISOString(),
            }))
            .catch(() => ({ path: entry.captureFile, unavailable: true }))
        : null;
      record = await histories.history().create({
        question: args.question,
        mode: args.mode,
        status: "pending",
        model: credentials.model,
        at: new Date().toISOString(),
        captureFile: entry.captureFile || null,
        selectedFrame: args.frame || null,
        context: {
          source,
          mmsPorts: entry.ports || [102],
          jump: args.jump || null,
          connections: args.connections === true,
          evidence,
        },
        answer: "本次问答尚未完成。",
        facts: [],
        limitations: [],
        next_checks: [],
        coverage: "只保存本次实际提供给 AI 的有限解析证据，不包含完整 PCAP。",
        usage: {},
      });
      histories.active.add(record.id);
      histories.changed();
      const client = {
        request: (cmd, a) => {
          if (!entry.captureFile)
            throw Error("尚未打开捕获文件，当前只能进行基础知识学习");
          return entry.client.request(cmd, a);
        },
      };
      const result = await analyze({
        client,
        credentials,
        question: args.question,
        mode: args.mode,
        frame: args.frame,
        jump: args.jump,
        connections: args.connections === true,
        signal: controller.signal,
        onEvidence: async (item) => {
          evidence.push(item);
          record = await histories
            .history()
            .write({ ...record, context: { ...record.context, evidence } });
        },
        onProgress: (message) => {
          if (
            !entry.window.isDestroyed() &&
            generation === (entry.aiGeneration || 0)
          )
            entry.window.webContents.send("capture:ai-progress", { message });
        },
      });
      if (controller.signal.aborted || generation !== (entry.aiGeneration || 0))
        throw Error("AI 任务已取消或捕获已切换");
      record = await histories.history().write({
        ...record,
        ...result,
        status: result.incomplete ? "incomplete" : "completed",
        context: { ...record.context, evidence },
      });
      histories.changed();
      if (controller.signal.aborted || generation !== (entry.aiGeneration || 0))
        return result;
      entry.aiResult = record;
      await answers.show(entry);
      return result;
    } catch (error) {
      if (record && !["completed", "incomplete"].includes(record.status)) {
        try {
          await histories.history().write({
            ...record,
            status: controller.signal.aborted ? "cancelled" : "failed",
            answer: controller.signal.aborted
              ? "本次问答已取消，未生成完整答案。"
              : error.message,
            context: { ...record.context, evidence },
          });
          histories.changed();
        } catch (saveError) {
          throw Error(
            `${error.message}；历史记录保存失败：${saveError.message}`,
          );
        }
      }
      throw error;
    } finally {
      if (record) histories.active.delete(record.id);
      clearTimeout(timer);
      entry.aiBusy = false;
      if (entry.aiController === controller) entry.aiController = null;
    }
  }
  throw Error("不支持的 AI 命令");
}
module.exports = {
  command,
  invalidate,
  close(entry) {
    answers.close(entry);
    histories.close(entry);
  },
};

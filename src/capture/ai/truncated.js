"use strict";
// Recover only the display text, never tool calls, evidence assertions or filters.
function draftText(content) {
  if (typeof content !== "string") return "";
  const text = content.slice(0, 65536);
  const match = /"answer"\s*:\s*"/.exec(text);
  if (!match) return "";
  const start = match.index + match[0].length;
  let escaped = false,
    end = start;
  for (; end < text.length; end++) {
    const c = text[end];
    if (escaped) {
      escaped = false;
      continue;
    }
    if (c === "\\") {
      escaped = true;
      continue;
    }
    if (c === '"') break;
  }
  const raw = text.slice(start, end);
  // A cutoff may split a backslash or a Unicode escape. Do not repair other malformed JSON.
  for (let cut = 0; cut <= 6 && cut <= raw.length; cut++) {
    try {
      return JSON.parse('"' + raw.slice(0, raw.length - cut) + '"');
    } catch {}
  }
  return "";
}
function incomplete(response, usage, frames, reason = "输出仍达到长度限制") {
  return {
    answer:
      draftText(response.message?.content) ||
      "模型尚未生成可展示的答案正文。请将问题拆分为一个对象或一次交互后重试。",
    incomplete: true,
    facts: [],
    filter: "",
    limitations: [
      reason +
        "。以下为未完成草稿，未通过完整结构与事实引用校验，不能视为完整诊断结论。",
    ],
    next_checks: ["结合已保存的报文上下文核对草稿；可将问题拆分后重新提问。"],
    coverage: "保留本次已读取的上下文；截断结果不提供证据跳转或筛选操作。",
    model: response.model,
    usage,
    frames,
  };
}
module.exports = { draftText, incomplete };

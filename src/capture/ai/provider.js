"use strict";
const ERRORS = {
  400: "请求格式不兼容",
  401: "API Key 无效",
  402: "DeepSeek 余额不足",
  422: "模型参数不兼容",
  429: "DeepSeek 请求限流",
  500: "DeepSeek 服务异常",
  503: "DeepSeek 服务繁忙",
};
async function complete({
  key,
  model,
  messages,
  tools,
  signal,
  thinking = false,
  maxTokens = 16384,
  fetchImpl = globalThis.fetch,
}) {
  if (![16384, 32768].includes(maxTokens)) throw Error("AI 输出预算无效");
  let response;
  try {
    response = await fetchImpl("https://api.deepseek.com/chat/completions", {
      method: "POST",
      redirect: "error",
      signal: AbortSignal.any([
        signal || new AbortController().signal,
        AbortSignal.timeout(120000),
      ]),
      headers: {
        "Content-Type": "application/json",
        Authorization: `Bearer ${key}`,
      },
      body: JSON.stringify({
        model,
        messages,
        stream: false,
        max_tokens: maxTokens,
        thinking: { type: thinking ? "enabled" : "disabled" },
        ...(thinking ? { reasoning_effort: "low" } : {}),
        ...(tools?.length ? { tools } : {}),
        response_format: { type: "json_object" },
      }),
    });
  } catch {
    if (signal?.aborted) throw Error("AI 任务已取消");
    throw Error("DeepSeek 连接失败或超时，请检查网络后重试");
  }
  if (!response.ok)
    throw Error(
      ERRORS[response.status] || `DeepSeek 请求失败（HTTP ${response.status}）`,
    );
  // Bound even successful responses. Never include provider error bodies / credentials in diagnostics.
  const reader = response.body.getReader();
  let size = 0;
  const chunks = [];
  try {
    while (true) {
      const { done, value } = await reader.read();
      if (done) break;
      size += value.length;
      if (size > 1024 * 1024) throw Error("AI 响应超过大小限制");
      chunks.push(Buffer.from(value));
    }
  } finally {
    await reader.cancel().catch(() => {});
  }
  let data;
  try {
    data = JSON.parse(Buffer.concat(chunks).toString("utf8"));
  } catch {
    throw Error("DeepSeek 返回无效 JSON");
  }
  const choice = data.choices?.[0];
  if (!choice?.message) throw Error("DeepSeek 未返回有效回答");

  return {
    message: choice.message,
    finishReason: choice.finish_reason,
    usage: data.usage || {},
    model: data.model || model,
  };
}
module.exports = { complete };

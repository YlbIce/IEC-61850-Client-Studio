const escape = (value) =>
  String(value ?? "").replace(
    /[&<>"']/g,
    (c) =>
      ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;", "'": "&#39;" })[
        c
      ],
  );
function tree(value, label = "协议", depth = 0) {
  if ((value?.name || label).endsWith("_raw")) return "";
  if (depth > 24)
    return '<div class="leaf">结构超过显示深度，请使用过滤器缩小范围。</div>';
  if (value === null || typeof value !== "object")
    return `<div class="leaf">${escape(label)}: ${escape(value)}</div>`;
  if (Array.isArray(value))
    return value
      .map((item, index) =>
        tree(
          item,
          item?.label || item?.name || `${label} [${index}]`,
          depth + 1,
        ),
      )
      .join("");
  if (value.children) {
    const text = value.label || value.name || label;
    if (!value.children.length)
      return `<div class="leaf">${escape(text)}${value.value !== undefined && !String(text).includes(String(value.value)) ? `: ${escape(value.value)}` : ""}</div>`;
    return `<details ${depth === 0 || ["mms", "goose", "sv"].includes(value.name) ? "open" : ""}><summary>${escape(text)}</summary>${tree(value.children, text, depth + 1)}</details>`;
  }
  return `<details ${depth < 2 ? "open" : ""}><summary>${escape(label)}</summary>${Object.entries(
    value,
  )
    .map(([name, item]) => tree(item, name, depth + 1))
    .join("")}</details>`;
}
function formatHex(raw) {
  if (Array.isArray(raw))
    raw = raw
      .map((byte) => Number(byte).toString(16).padStart(2, "0"))
      .join("");
  const value = String(raw ?? "");
  if (/\n/.test(value)) return value;
  const hex = value.replace(/[^a-fA-F0-9]/g, "");
  if (hex.length % 2) return value;
  const bytes = hex.match(/../g) || [];
  const lines = [];
  for (let i = 0; i < bytes.length; i += 16) {
    const slice = bytes.slice(i, i + 16);
    lines.push(
      i.toString(16).padStart(6, "0") +
        "  " +
        slice.join(" ").padEnd(47, " ") +
        "  " +
        slice
          .map((b) => {
            const code = parseInt(b, 16);
            return code >= 32 && code <= 126 ? String.fromCharCode(code) : ".";
          })
          .join(""),
    );
  }
  return lines.join("\n");
}

export { escape, tree, formatHex };

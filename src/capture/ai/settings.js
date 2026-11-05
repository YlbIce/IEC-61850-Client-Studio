"use strict";
const fs = require("node:fs/promises");
const path = require("node:path");
const { randomUUID } = require("node:crypto");
class AiSettings {
  constructor(directory, secure) {
    this.file = path.join(directory, "capture-ai.json");
    this.secure = secure;
    this.queue = Promise.resolve();
  }
  async read() {
    try {
      const v = JSON.parse(await fs.readFile(this.file, "utf8"));
      if (v.version !== 1) throw Error("不支持的 AI 配置版本");
      return v;
    } catch (e) {
      if (e.code === "ENOENT") return { version: 1, model: "deepseek-flash" };
      throw Error("AI 配置无法读取，请清除配置后重新保存");
    }
  }
  async status() {
    const v = await this.read();
    return {
      configured: !!v.encryptedKey,
      model: v.model,
      encrypted: true,
      endpoint: "https://api.deepseek.com",
      path: this.file,
    };
  }
  async credentials() {
    const v = await this.read();
    if (!v.encryptedKey) throw Error("请先在 AI 设置中保存 API Key");
    try {
      return {
        key: this.secure.decryptString(Buffer.from(v.encryptedKey, "base64")),
        model: v.model,
      };
    } catch {
      throw Error("无法解密本地 API Key，请在当前 Windows 用户下重新保存");
    }
  }
  save(args) {
    const task = this.queue.then(() => this.write(args));
    this.queue = task.catch(() => {});
    return task;
  }
  async write({ key, model = "deepseek-flash", clear = false } = {}) {
    if (clear) {
      await fs.rm(this.file, { force: true });
      return this.status();
    }
    if (typeof model !== "string" || !/^[a-zA-Z0-9._-]{1,80}$/.test(model))
      throw Error("模型名称无效");
    const v = await this.read();
    if (key !== undefined && key !== "") {
      if (
        typeof key !== "string" ||
        key.length > 512 ||
        !/^\S{8,512}$/.test(key)
      )
        throw Error("API Key 格式无效");
      if (!this.secure.isEncryptionAvailable())
        throw Error("系统密钥保护不可用，未保存 Key");
      v.encryptedKey = this.secure.encryptString(key).toString("base64");
    }
    v.model = model;
    v.version = 1;
    await fs.mkdir(path.dirname(this.file), { recursive: true });
    const temp = this.file + "." + randomUUID() + ".tmp";
    try {
      const f = await fs.open(temp, "wx", 0o600);
      try {
        await f.writeFile(JSON.stringify(v));
        await f.sync();
      } finally {
        await f.close();
      }
      await fs.rename(temp, this.file);
    } finally {
      await fs.rm(temp, { force: true });
    }
    return this.status();
  }
}
module.exports = { AiSettings };

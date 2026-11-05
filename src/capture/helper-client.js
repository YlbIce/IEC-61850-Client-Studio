"use strict";
const { spawn } = require("node:child_process");
const { EventEmitter } = require("node:events");
const fs = require("node:fs");
const path = require("node:path");

/** Owns one optional capture process. No DLL is loaded into Electron. */
class CaptureClient extends EventEmitter {
  constructor({ root, resourcesPath, isPackaged = false }) {
    super();
    this.executable = isPackaged
      ? path.join(resourcesPath, "capture", "capture-helper.exe")
      : path.join(root, "backend", "capture", "bin", "capture-helper.exe");
    this.pending = new Map();
    this.sequence = 0;
    this.child = null;
    this.buffer = "";
    this.closed = false;
  }
  start() {
    if (this.child) return;
    if (!fs.existsSync(this.executable))
      throw new Error(
        "未安装可选报文分析模块（capture-helper.exe）。IEC61850 主应用可继续完整使用。",
      );
    this.child = spawn(this.executable, [], {
      windowsHide: true,
      stdio: ["pipe", "pipe", "pipe"],
    });
    this.child.stdout.setEncoding("utf8");
    this.child.stdout.on("data", (chunk) => {
      this.buffer += chunk;
      if (this.buffer.length > 64 * 1024 * 1024) {
        this.fail(new Error("报文分析模块返回数据超过安全上限"));
        this.close();
        return;
      }
      let newline;
      while ((newline = this.buffer.indexOf("\n")) >= 0) {
        const line = this.buffer.slice(0, newline);
        this.buffer = this.buffer.slice(newline + 1);
        if (!line.trim()) continue;
        try {
          const message = JSON.parse(line),
            pending = this.pending.get(message.id);
          if (pending) {
            clearTimeout(pending.timer);
            this.pending.delete(message.id);
            message.error
              ? pending.reject(
                  new Error(message.error.message || String(message.error)),
                )
              : pending.resolve(message.result);
          } else this.emit("event", message);
        } catch {
          this.fail(new Error("报文分析模块返回无效 JSON"));
        }
      }
    });
    this.child.stderr.on("data", () => {});
    this.child.stdin.on("error", (error) => this.fail(error));
    this.child.on("error", (error) => this.fail(error));
    this.child.on("exit", (code) => {
      this.child = null;
      this.fail(new Error(`报文分析模块已退出（${code ?? "已关闭"}）`));
    });
  }
  fail(error) {
    for (const pending of this.pending.values()) {
      clearTimeout(pending.timer);
      pending.reject(error);
    }
    this.pending.clear();
    if (!this.closed) this.emit("failure", error.message);
  }
  request(command, args = {}) {
    if (this.closed) return Promise.reject(new Error("报文分析窗口已关闭"));
    try {
      this.start();
    } catch (error) {
      return Promise.reject(error);
    }
    return new Promise((resolve, reject) => {
      const id = ++this.sequence;
      const timer = setTimeout(
        () => {
          this.pending.delete(id);
          const error = new Error(
            `报文分析操作超时：${command}。分析进程已停止，请重新打开文件。`,
          );
          reject(error);
          this.fail(error);
          this.child?.kill();
        },
        ["open", "packets", "detail", "replay", "sessions"].includes(command)
          ? 330000
          : 60000,
      );
      this.pending.set(id, { resolve, reject, timer });
      this.child.stdin.write(
        JSON.stringify({ id, command, args }) + "\n",
        (error) => {
          if (error) {
            clearTimeout(timer);
            this.pending.delete(id);
            reject(error);
          }
        },
      );
    });
  }
  close() {
    if (this.closed) return;
    this.closed = true;
    const child = this.child;
    if (child) {
      child.stdin.end(
        JSON.stringify({ id: ++this.sequence, command: "close", args: {} }) +
          "\n",
      );
      const timer = setTimeout(() => {
        if (child.exitCode === null) child.kill();
      }, 4000);
      timer.unref();
      child.once("exit", () => clearTimeout(timer));
    }
    this.fail(new Error("窗口已关闭"));
  }
}
module.exports = { CaptureClient };

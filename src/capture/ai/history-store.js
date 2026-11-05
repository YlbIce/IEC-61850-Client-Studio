"use strict";
const fs = require("node:fs/promises");
const path = require("node:path");
const { randomUUID } = require("node:crypto");
const validId = (id) => typeof id === "string" && /^[0-9a-f-]{36}$/.test(id);
class HistoryStore {
  constructor(directory) {
    this.directory = directory;
    this.queue = Promise.resolve();
  }
  async atomic(file, value) {
    const tmp = file + ".tmp";
    await fs.writeFile(tmp, JSON.stringify(value), "utf8");
    await fs.rename(tmp, file);
  }
  write(record) {
    const operation = this.queue.then(async () => {
      if (!validId(record.id)) throw Error("历史记录标识无效");
      await fs.mkdir(this.directory, { recursive: true });
      await this.atomic(path.join(this.directory, record.id + ".json"), record);
      const summary = {
        id: record.id,
        question: record.question,
        at: record.at,
        mode: record.mode,
        status: record.status,
        model: record.model,
        captureFile: record.captureFile,
        frames: record.frames || [],
      };
      await this.atomic(
        path.join(this.directory, record.id + ".summary.json"),
        summary,
      );
      return record;
    });
    this.queue = operation.catch(() => {});
    return operation;
  }
  async create(fields) {
    return this.write({ ...fields, id: randomUUID(), schemaVersion: 1 });
  }
  async read(id) {
    if (!validId(id)) throw Error("历史记录标识无效");
    await this.queue;
    return JSON.parse(
      await fs.readFile(path.join(this.directory, id + ".json"), "utf8"),
    );
  }
  async list({ query = "", offset = 0 } = {}) {
    await this.queue;
    await fs.mkdir(this.directory, { recursive: true });
    const files = await fs.readdir(this.directory),
      rows = [];
    let unreadable = 0;
    for (const file of files.filter((f) => /^[0-9a-f-]{36}\.json$/.test(f))) {
      try {
        const summary = file.replace(".json", ".summary.json");
        let r;
        try {
          r = JSON.parse(
            await fs.readFile(path.join(this.directory, summary), "utf8"),
          );
        } catch {
          r = JSON.parse(
            await fs.readFile(path.join(this.directory, file), "utf8"),
          );
        }
        if (!validId(r.id)) throw Error("无效记录");
        if (
          `${r.question} ${r.captureFile || ""} ${r.model || ""}`
            .toLowerCase()
            .includes(String(query).slice(0, 1000).toLowerCase())
        )
          rows.push({
            id: r.id,
            question: r.question,
            at: r.at,
            mode: r.mode,
            status: r.status,
            model: r.model,
            captureFile: r.captureFile,
            frames: r.frames || [],
          });
      } catch {
        unreadable++;
      }
    }
    rows.sort((a, b) => b.at.localeCompare(a.at));
    const start = Number.isSafeInteger(offset) && offset >= 0 ? offset : 0;
    return {
      rows: rows.slice(start, start + 50),
      total: rows.length,
      offset: start,
      unreadable,
      path: this.directory,
    };
  }
  remove(id) {
    const operation = this.queue.then(async () => {
      if (!validId(id)) throw Error("历史记录标识无效");
      await fs.rm(path.join(this.directory, id + ".json"), { force: true });
      await fs.rm(path.join(this.directory, id + ".summary.json"), {
        force: true,
      });
    });
    this.queue = operation.catch(() => {});
    return operation;
  }
}
module.exports = { HistoryStore };

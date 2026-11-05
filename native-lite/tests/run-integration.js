"use strict";
const path = require("node:path");
const fs = require("node:fs");
const { spawn } = require("node:child_process");
const serviceRoot = path.resolve(__dirname, "../../../Iec61850ServiceStudio");
const { Backend } = require(path.join(serviceRoot, "src/main/backend"));
const { defaultProject } = require(path.join(serviceRoot, "src/shared/project"));
async function main() {
  const backend = new Backend(serviceRoot, path.join(serviceRoot, "backend/bin/iec61850-service-backend.exe"));
  try {
    await backend.start();
    const project = defaultProject(); project.port = 18274;
    // Owned loopback instance only; never operate a user's field IED.
    await backend.call("Apply", project); await backend.call("Start");
    const qt = process.env.LITE_QT_ROOT || "D:/Qt/5.15.2/mingw81_64";
    const gcc = process.env.LITE_MINGW_ROOT || "D:/Qt/Tools/mingw810_64";
    const output = path.resolve(__dirname, "../test-results.txt");
    const executable = path.resolve(__dirname, "../build/lite_tests.exe");
    const code = await new Promise((resolve, reject) => {
      const child = spawn(executable, ["-o", `${output},txt`], {
        windowsHide: true, cwd: path.resolve(__dirname, ".."),
        env: { ...process.env, LITE_TEST_PORT: String(project.port), PATH: `${qt}/bin;${gcc}/bin;${process.env.PATH}` },
        stdio: "inherit",
      });
      const timer = setTimeout(() => { child.kill(); reject(new Error("Native integration timeout")); }, 90000);
      child.once("error", reject); child.once("exit", code => { clearTimeout(timer); resolve(code); });
    });
    process.stdout.write(fs.readFileSync(output, "utf8")); process.exitCode = code || 0;
  } finally { await backend.close(); }
}
main().catch(error => { console.error(error); process.exitCode = 1; });

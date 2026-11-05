const { spawn } = require("node:child_process");
const fs = require("node:fs");
const path = require("node:path");
const root = path.resolve(__dirname, ".."),
  env = { ...process.env };
delete env.ELECTRON_RUN_AS_NODE;
const child = spawn(
  require("electron"),
  [path.join(root, "tests/capture-ai-ui.js")],
  { cwd: root, env, stdio: "ignore", windowsHide: true },
);
child.on("error", (e) => {
  console.error(e.message);
  process.exitCode = 1;
});
child.on("exit", (code) => {
  const file = path.join(root, "capture-ai-ui-result.json");
  if (fs.existsSync(file)) {
    const r = JSON.parse(fs.readFileSync(file, "utf8"));
    console.log(
      JSON.stringify({
        passed: r.passed,
        transport: r.transport,
        dpapi: r.dpapi,
        cloudConsent: r.cloudConsent,
        error: r.error,
      }),
    );
  }
  process.exitCode = code || 0;
});

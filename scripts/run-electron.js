const { spawn, spawnSync } = require('child_process');
const path = require('path');

const root = path.resolve(__dirname, '..');
const args = new Set(process.argv.slice(2));
const devMode = args.has('--dev');

const build = spawnSync(process.execPath, [path.join(root, 'scripts', 'build-backend.js')], {
  cwd: root,
  stdio: 'inherit',
  shell: false
});

if (build.error) throw build.error;
if (build.status !== 0) process.exit(build.status ?? 1);

let electronPath;
try {
  electronPath = require('electron');
} catch {
  console.error('Electron is not installed. Run npm install first.');
  process.exit(1);
}

const electronArgs = [];
if (devMode) {
  electronArgs.push('--remote-debugging-port=9233');
}
electronArgs.push(root);

const child = spawn(electronPath, electronArgs, {
  cwd: root,
  stdio: 'inherit',
  shell: false,
  env: {
    ...process.env,
    ELECTRON_DEV: devMode ? '1' : process.env.ELECTRON_DEV ?? '0'
  }
});

child.on('error', (error) => {
  console.error(error);
  process.exit(1);
});

child.on('exit', (code) => {
  process.exit(code ?? 0);
});

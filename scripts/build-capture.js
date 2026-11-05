const { spawnSync } = require('node:child_process');
const fs = require('node:fs');
const path = require('node:path');
const root = path.resolve(__dirname, '..');
const source = path.join(root, 'backend/capture');
const build = path.join(source, 'build');
const vcpkg = process.env.VCPKG_ROOT || path.resolve(root, '../tools/vcpkg');
const developer = process.env.VSDEVCMD || 'C:\\Program Files\\Microsoft Visual Studio\\2022\\Community\\Common7\\Tools\\VsDevCmd.bat';
fs.mkdirSync(build, { recursive: true });
const testClient = process.argv.includes('--test-client');
const commands = [`cmake -S "${source}" -B "${build}" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCAPTURE_BUILD_TEST_CLIENT=${testClient ? 'ON' : 'OFF'} -DCMAKE_INCLUDE_PATH="${vcpkg}/installed/x64-windows/include"`, `cmake --build "${build}"`];
for (const command of commands) {
  const file = path.join(build, 'build.cmd');
  fs.writeFileSync(file, `@echo off\r\nchcp 65001 >nul\r\ncall "${developer}" -arch=x64 >nul\r\nif errorlevel 1 exit /b %errorlevel%\r\nset VSLANG=1033\r\n${command}\r\nexit /b %errorlevel%\r\n`);
  const result = spawnSync('cmd.exe', ['/d', '/c', file], { cwd: root, stdio: 'inherit' });
  if (result.error) throw result.error;
  if (result.status) process.exit(result.status);
}

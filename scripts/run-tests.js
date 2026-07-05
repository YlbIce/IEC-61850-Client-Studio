const { spawnSync } = require('child_process');
const fs = require('fs');
const path = require('path');

const root = path.resolve(__dirname, '..');
const workspaceRoot = path.resolve(root, '..');
const buildDir = path.join(root, 'backend', 'build-tests');
const binDir = path.join(root, 'backend', 'bin');
const vcpkgRoot = path.join(workspaceRoot, 'tools', 'vcpkg');
const toolchainFile = path.join(vcpkgRoot, 'scripts', 'buildsystems', 'vcpkg.cmake');
const vsDevCmd = 'C:\\Program Files\\Microsoft Visual Studio\\2022\\Community\\Common7\\Tools\\VsDevCmd.bat';

function requireFile(file, label) {
  if (!fs.existsSync(file)) {
    console.error(`${label} not found: ${file}`);
    process.exit(1);
  }
}

function quote(value) {
  return `"${String(value).replace(/"/g, '\\"')}"`;
}

function runInVsDev(command) {
  requireFile(vsDevCmd, 'Visual Studio Developer Command Prompt');
  fs.mkdirSync(buildDir, { recursive: true });
  const batchFile = path.join(buildDir, 'run-vsdev-command.cmd');
  fs.writeFileSync(
    batchFile,
    [
      '@echo off',
      `call "${vsDevCmd}" -arch=x64 >nul`,
      'if errorlevel 1 exit /b %errorlevel%',
      command,
      'exit /b %errorlevel%',
      ''
    ].join('\r\n')
  );

  const result = spawnSync('cmd.exe', ['/d', '/c', batchFile], {
    cwd: root,
    stdio: 'inherit',
    shell: false,
    env: { ...process.env, VSLANG: '1033' }
  });
  if (result.error) throw result.error;
  if (result.status !== 0) process.exit(result.status ?? 1);
}

requireFile(toolchainFile, 'vcpkg CMake toolchain');

console.log('Configuring unit tests with CMake (BUILD_STUDIO_TESTS=ON)');
runInVsDev([
  'cmake',
  '-S', quote(root),
  '-B', quote(buildDir),
  '-G', quote('Ninja'),
  '-DCMAKE_BUILD_TYPE=Release',
  `-DCMAKE_TOOLCHAIN_FILE=${quote(toolchainFile)}`,
  '-DVCPKG_TARGET_TRIPLET=x64-windows',
  '-DBUILD_STUDIO_TESTS=ON'
].join(' '));

console.log('Building unit tests');
runInVsDev(['cmake', '--build', quote(buildDir), '--config', 'Release', '--target', 'studio-tests'].join(' '));

const testExe = path.join(binDir, 'studio-tests.exe');
requireFile(testExe, 'studio-tests executable');

console.log('\n' + '='.repeat(70));
console.log('Running unit tests');
console.log('='.repeat(70) + '\n');

const result = spawnSync(testExe, [], { stdio: 'inherit', shell: false });
process.exit(result.status ?? 1);

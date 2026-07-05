const { spawnSync } = require('child_process');
const fs = require('fs');
const path = require('path');

const root = path.resolve(__dirname, '..');
const workspaceRoot = path.resolve(root, '..');
const protoFile = path.join(root, 'proto', 'iec61850studio.proto');
const generatedDir = path.join(root, 'backend', 'generated');
const buildDir = path.join(root, 'backend', 'build');
const binDir = path.join(root, 'backend', 'bin');
const vcpkgRoot = path.join(workspaceRoot, 'tools', 'vcpkg');
const vcpkgInstalled = path.join(vcpkgRoot, 'installed', 'x64-windows');
const protoc = path.join(vcpkgInstalled, 'tools', 'protobuf', 'protoc.exe');
const grpcPlugin = path.join(vcpkgInstalled, 'tools', 'grpc', 'grpc_cpp_plugin.exe');
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

function run(command, args, options = {}) {
  const result = spawnSync(command, args, {
    cwd: root,
    stdio: 'inherit',
    shell: false,
    ...options
  });
  if (result.error) throw result.error;
  if (result.status !== 0) process.exit(result.status ?? 1);
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

requireFile(protoFile, 'Protobuf schema');
requireFile(protoc, 'vcpkg protoc');
requireFile(grpcPlugin, 'grpc_cpp_plugin');
requireFile(toolchainFile, 'vcpkg CMake toolchain');

fs.mkdirSync(generatedDir, { recursive: true });
fs.mkdirSync(buildDir, { recursive: true });
fs.mkdirSync(binDir, { recursive: true });

console.log('Generating C++ Protobuf/gRPC sources');
run(protoc, [
  `--proto_path=${path.join(root, 'proto')}`,
  `--cpp_out=${generatedDir}`,
  `--grpc_out=${generatedDir}`,
  `--plugin=protoc-gen-grpc=${grpcPlugin}`,
  protoFile
]);

const configureArgs = [
  'cmake',
  '-S',
  quote(root),
  '-B',
  quote(buildDir),
  '-G',
  quote('Ninja'),
  '-DCMAKE_BUILD_TYPE=Release'
];

if (!fs.existsSync(path.join(buildDir, 'CMakeCache.txt'))) {
  configureArgs.push(`-DCMAKE_TOOLCHAIN_FILE=${quote(toolchainFile)}`);
  configureArgs.push('-DVCPKG_TARGET_TRIPLET=x64-windows');
}

console.log('Configuring C++ backend with CMake + libiec61850');
runInVsDev(configureArgs.join(' '));

console.log('Building C++ backend');
runInVsDev(['cmake', '--build', quote(buildDir), '--config', 'Release'].join(' '));

console.log(`Backend ready: ${path.join(binDir, process.platform === 'win32' ? 'iec61850-client-backend.exe' : 'iec61850-client-backend')}`);

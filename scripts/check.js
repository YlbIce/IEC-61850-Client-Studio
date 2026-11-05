const { readdirSync } = require('node:fs');
const { join } = require('node:path');
const { spawnSync } = require('node:child_process');
function check(dir) {
  for (const f of readdirSync(dir, { withFileTypes: true })) {
    const p = join(dir, f.name);
    if (f.isDirectory()) check(p);
    else if (/\.(js|mjs)$/.test(f.name)) {
      const r = spawnSync(process.execPath, ['--check', p], { stdio: 'inherit' });
      if (r.status) process.exit(r.status);
    }
  }
}
check(join(__dirname, '../src')); check(__dirname);
console.log('JavaScript syntax checks passed');

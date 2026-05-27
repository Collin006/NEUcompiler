const fs = require('fs');
const os = require('os');
const path = require('path');

const appDir = path.resolve(__dirname, '..');
const projectDir = path.resolve(appDir, '..');
const binaryName = process.platform === 'win32' ? 'NEUcompiler.exe' : 'NEUcompiler';
const platformDir = `${process.platform}-${process.arch}`;

const candidates = [
  path.resolve(projectDir, 'build', 'Release', binaryName),
  path.resolve(projectDir, 'build', binaryName),
  path.resolve(projectDir, 'cmake-build-release', binaryName),
  path.resolve(projectDir, 'cmake-build-debug', binaryName),
  path.resolve(projectDir, 'release', binaryName)
];

const source = candidates.find(candidate => fs.existsSync(candidate));

if (!source) {
  console.error(`Cannot find ${binaryName}. Build the native compiler first.`);
  console.error('Checked:');
  for (const candidate of candidates) {
    console.error(`  - ${candidate}`);
  }
  process.exit(1);
}

const targetDir = path.resolve(appDir, 'resources', 'bin', platformDir);
const target = path.resolve(targetDir, binaryName);

fs.mkdirSync(targetDir, { recursive: true });
fs.copyFileSync(source, target);

if (os.platform() !== 'win32') {
  fs.chmodSync(target, 0o755);
}

console.log(`Bundled native compiler: ${source} -> ${target}`);

if (process.platform !== 'win32') {
  console.error('dist:win must be run on Windows so the bundled NEUcompiler.exe matches the Electron package.');
  process.exit(1);
}

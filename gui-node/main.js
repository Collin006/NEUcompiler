const { app, BrowserWindow, dialog, ipcMain } = require('electron');
const path = require('path');
const fs = require('fs');
const { spawn } = require('child_process');

const headings = [
  { key: 'tokens', title: '===== 词法分析 =====' },
  { key: 'expression', title: '===== 表达式分析表构建 =====' },
  { key: 'parse', title: '===== 语法分析 =====' },
  { key: 'rawQuadruples', title: '===== 四元式输出 =====' },
  { key: 'optimizedQuadruples', title: '===== 四元式优化 =====' },
  { key: 'activeMark', title: '===== 活跃信息标记 =====' },
  { key: 'targetCode', title: '===== 目标代码生成 =====' }
];

function createWindow() {
  const window = new BrowserWindow({
    width: 1100,
    height: 750,
    webPreferences: {
      contextIsolation: true,
      preload: path.join(__dirname, 'preload.js')
    }
  });

  window.loadFile(path.join(__dirname, 'index.html'));
}

function getCompilerPath() {
  const envPath = process.env.NEUCOMPILER_BIN;
  if (envPath && envPath.trim().length > 0) {
    return envPath;
  }
  const binaryName = process.platform === 'win32' ? 'NEUcompiler.exe' : 'NEUcompiler';
  return path.resolve(__dirname, '..', 'build', binaryName);
}

function parseStages(output) {
  const stages = {
    tokens: '',
    expression: '',
    parse: '',
    rawQuadruples: '',
    optimizedQuadruples: '',
    activeMark: '',
    targetCode: ''
  };

  const lines = output.split(/\r?\n/);
  let currentKey = null;

  for (const line of lines) {
    const heading = headings.find(item => item.title === line.trim());
    if (heading) {
      currentKey = heading.key;
      continue;
    }
    if (currentKey) {
      stages[currentKey] += line + '\n';
    }
  }

  return stages;
}

function buildFileStatus(sourcePath) {
  const suffixes = {
    rawQuadruples: '_raw_quadruples.txt',
    parseLog: '_parse_log.txt',
    quadruples: '_quadruples.txt',
    activeMark: '_active_mark.txt',
    targetCode: '_target_code.txt'
  };

  const result = {};
  for (const [key, suffix] of Object.entries(suffixes)) {
    const filePath = sourcePath + suffix;
    result[key] = {
      path: filePath,
      exists: fs.existsSync(filePath)
    };
  }
  return result;
}

function runCompiler(sourcePath) {
  return new Promise(resolve => {
    const compilerPath = getCompilerPath();
    if (!fs.existsSync(compilerPath)) {
      resolve({
        success: false,
        errorMessage: `找不到编译器可执行文件: ${compilerPath}`,
        compilerPath
      });
      return;
    }

    const child = spawn(compilerPath, [sourcePath], { windowsHide: true });
    let stdout = '';
    let stderr = '';

    child.stdout.on('data', data => {
      stdout += data.toString('utf8');
    });
    child.stderr.on('data', data => {
      stderr += data.toString('utf8');
    });

    child.on('error', error => {
      resolve({
        success: false,
        errorMessage: error.message,
        compilerPath
      });
    });

    child.on('close', code => {
      const stages = parseStages(stdout);
      const files = buildFileStatus(sourcePath);
      resolve({
        success: code === 0,
        exitCode: code,
        stdout,
        stderr,
        stages,
        files,
        compilerPath
      });
    });
  });
}

app.whenReady().then(() => {
  createWindow();

  app.on('activate', () => {
    if (BrowserWindow.getAllWindows().length === 0) {
      createWindow();
    }
  });
});

app.on('window-all-closed', () => {
  if (process.platform !== 'darwin') {
    app.quit();
  }
});

ipcMain.handle('select-source', async () => {
  const result = await dialog.showOpenDialog({
    title: '选择源文件',
    properties: ['openFile'],
    filters: [
      { name: 'Source Files', extensions: ['txt', 'pas', 'pascal'] },
      { name: 'All Files', extensions: ['*'] }
    ]
  });

  if (result.canceled || result.filePaths.length === 0) {
    return '';
  }

  return result.filePaths[0];
});

ipcMain.handle('run-compile', async (_event, sourcePath) => {
  if (!sourcePath) {
    return { success: false, errorMessage: '请先选择源文件。' };
  }

  return await runCompiler(sourcePath);
});

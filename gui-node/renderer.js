const fileInput = document.getElementById('sourcePath');
const browseButton = document.getElementById('browse');
const runButton = document.getElementById('run');
const statusBar = document.getElementById('status');
const errorBox = document.getElementById('error');

const outputMap = {
  tokens: document.getElementById('output-tokens'),
  expression: document.getElementById('output-expression'),
  parse: document.getElementById('output-parse'),
  rawQuadruples: document.getElementById('output-raw-quadruples'),
  optimizedQuadruples: document.getElementById('output-optimized-quadruples'),
  activeMark: document.getElementById('output-active'),
  targetCode: document.getElementById('output-target'),
  files: document.getElementById('output-files'),
  fullOutput: document.getElementById('output-full')
};

function setStatus(message) {
  statusBar.textContent = message;
}

function clearOutputs() {
  Object.values(outputMap).forEach(element => {
    element.textContent = '';
  });
}

function updateFilesSummary(files) {
  if (!files) {
    outputMap.files.textContent = '';
    return;
  }
  const lines = [
    `原始四元式: ${files.rawQuadruples?.exists ? files.rawQuadruples.path : '未生成'}`,
    `语法日志: ${files.parseLog?.exists ? files.parseLog.path : '未生成'}`,
    `优化后四元式: ${files.quadruples?.exists ? files.quadruples.path : '未生成'}`,
    `活跃信息标记: ${files.activeMark?.exists ? files.activeMark.path : '未生成'}`,
    `目标代码: ${files.targetCode?.exists ? files.targetCode.path : '未生成'}`
  ];
  outputMap.files.textContent = lines.join('\n');
}

function updateStageOutputs(stages) {
  if (!stages) {
    return;
  }
  outputMap.tokens.textContent = stages.tokens || '';
  outputMap.expression.textContent = stages.expression || '';
  outputMap.parse.textContent = stages.parse || '';
  outputMap.rawQuadruples.textContent = stages.rawQuadruples || '';
  outputMap.optimizedQuadruples.textContent = stages.optimizedQuadruples || '';
  outputMap.activeMark.textContent = stages.activeMark || '';
  outputMap.targetCode.textContent = stages.targetCode || '';
}

function setError(message) {
  if (!message) {
    errorBox.textContent = '';
    errorBox.classList.add('hidden');
    return;
  }
  errorBox.textContent = message;
  errorBox.classList.remove('hidden');
}

browseButton.addEventListener('click', async () => {
  const selected = await window.api.selectSource();
  if (selected) {
    fileInput.value = selected;
  }
});

runButton.addEventListener('click', async () => {
  setError('');
  clearOutputs();
  const sourcePath = fileInput.value.trim();
  if (!sourcePath) {
    setError('请先选择源文件。');
    return;
  }

  setStatus('编译中...');
  const result = await window.api.runCompile(sourcePath);

  updateStageOutputs(result.stages);
  updateFilesSummary(result.files);
  outputMap.fullOutput.textContent = result.stdout || '';

  if (result.success) {
    setStatus('编译完成');
  } else if (result.errorMessage) {
    setStatus('编译失败');
    setError(result.errorMessage);
  } else if (result.stderr) {
    setStatus('编译失败');
    setError(result.stderr.trim());
  } else {
    setStatus('编译失败');
  }
});

const tabButtons = document.querySelectorAll('[data-tab]');
const tabPanels = document.querySelectorAll('.tab-panel');

tabButtons.forEach(button => {
  button.addEventListener('click', () => {
    const target = button.dataset.tab;
    tabButtons.forEach(btn => btn.classList.toggle('active', btn === button));
    tabPanels.forEach(panel => {
      panel.classList.toggle('active', panel.id === target);
    });
  });
});

const { contextBridge, ipcRenderer } = require('electron');

contextBridge.exposeInMainWorld('api', {
  selectSource: () => ipcRenderer.invoke('select-source'),
  runCompile: (sourcePath) => ipcRenderer.invoke('run-compile', sourcePath)
});

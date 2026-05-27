组长（我）：郑玉豪（负责整体框架搭建，符号表定义，词法分析，语法分析中的LR1）
组员：周锐（负责语法分析中的递归下降，函数，结构体，数组等复杂细节和语义动作），覃塘（基本块划分 ，优化处理），孙维宏（目标代码生成，活跃信息标注）
在另一个分支实现了node后端的GUI。

## GUI 一键打包

GUI 位于 `gui-node`，现在使用 Electron 打包，并会把 C++ 编译器可执行文件一起放进应用资源目录。老师运行打包产物时不需要安装 Node、npm 或项目依赖。

Windows 上生成便携版 exe（需要在 Windows 上执行，保证内置的是 Windows 版 `NEUcompiler.exe`）：

```bat
cd gui-node
npm install
npm run dist:win
```

输出文件在 `gui-node\dist\NEUcompiler-GUI-1.0.0-win-x64.exe`（具体架构以后缀为准）。提交给老师时上传这个 exe 即可。

本机调试：

```bash
cd gui-node
npm install
npm start
```

如果需要手动指定后端编译器路径，可以设置 `NEUCOMPILER_BIN` 环境变量；正常打包不需要设置。

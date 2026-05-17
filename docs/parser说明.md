# Parser 说明文档

本文档面向 `parser.h` / `parser.cpp`，说明语法分析器中主要函数的功能与分工。

## 1. 总体职责

`Parser` 采用“递归下降 + 表达式 LR(1)”的混合方式，负责：

- 按 `docs/文法.md` 进行语法分析
- 在分析过程中输出层级日志
- 进行恐慌模式错误恢复
- 同步执行语义动作：维护符号表并生成四元式

## 2. 对外入口函数

- `Parser(const vector<Token>& tokens)`  
  构造解析器，接收词法阶段产生的 token 序列。

- `bool parse()`  
  总入口。重置内部状态后调用 `parseProgram()`；结束时检查 token 是否消费完，并写入“通过/失败”日志。

- `string getLog() const` / `bool writeLogToFile(const string& filepath) const`  
  获取或落盘语法分析日志。

- `static bool getExpressionAnalysisDump(string& dump, string& error)`  
  输出表达式 LR(1) 自动构建结果（SELECT 集、ACTION/GOTO 表），用于调试和展示。

- `string getQuadrupleDump() const`  
  导出当前生成的四元式序列。

- `string getSymbolTableDump() const`  
  导出符号相关表（SYNBL / TYPEL / PFINFL / PARAMBL）。

## 3. 语法规则主干函数

### 3.1 程序与声明

- `parseProgram()`：解析 `program ID ; <分程序> .`，设置程序符号信息。  
- `parseSubProgram(bool enterNewScope)`：解析 `<说明部分><复合语句>`，并可控制是否进入新作用域。  
- `parseDeclarationPart()` / `parseDeclarationStatement()`：循环识别 `var / function / procedure / type` 声明。

### 3.2 变量、函数、过程

- `parseVariableDeclaration()`、`parseVariableDefinitionList()`、`parseVariableDefinition()`：解析变量声明段并绑定类型。  
- `parseIdentifierList()`、`parseIdentifierListTail()`：收集逗号分隔标识符。  
- `parseFunctionDeclaration()`：解析函数头和函数体，记录返回类型，生成函数入口/返回相关四元式。  
- `parseProcedureDeclaration()`：解析过程声明，逻辑与函数类似但无返回值类型。  
- `parseFormalParameters()`、`parseParameterList()`、`parseParameterDefinition()`、`parseValueParameter()`、`parseVarParameter()`：解析形参并填充参数信息。

### 3.3 类型系统

- `parseTypeDeclaration()`、`parseTypeDefinitionList()`、`parseTypeDefinition()`：处理 `type` 声明段。  
- `parseTypeConstructor(int& typelIndex)`：解析 `array[...] of ...` 或 `record ... end`，构建 TYPEL/AINFL/RINFL 条目。  
- `parseFieldList(int& currentOff)`、`parseField(int& currentOff)`：解析记录字段并分配偏移。  
- `parseType()`：解析内建类型或用户自定义类型名，更新 `lastParsedTypeIndex_`。

### 3.4 语句与控制流

- `parseCompoundStatement()`、`parseStatementList()`：解析 `begin ... end` 及语句序列。  
- `parseStatement()`：语句分发入口。  
- `parseIfStatement()`：解析 `if-then-else`，生成 `if/el/ie` 控制流四元式并回填。  
- `parseWhileStatement()`：解析 `while-do`，生成 `wh/do/we` 循环控制四元式。  
- `parseAssignOrCallStatement()`：统一处理“赋值语句 / 过程调用语句”。  
- `parseCallSuffix()`、`parseActualParameterList()`、`parseActualParameterListTail()`：解析调用括号和实参列表。

### 3.5 表达式

- `parseExpression(const vector<string>& stopTokens)`：
  - 按 `stopTokens` 截取当前表达式 token 片段
  - 调用 LR(1) 分析器进行移进/规约
  - 在规约回调中生成算术、逻辑、关系、函数调用等四元式
  - 结果地址写入 `lastExpressionPlace_`

## 4. 词法游标与匹配辅助

- `current()` / `peek()` / `advance()` / `isAtEnd()` / `currentLine()`：token 游标管理。  
- `checkType()` / `matchType()`、`checkKeyword()` / `matchKeyword()`、`checkDelimiter()` / `matchDelimiter()`、`checkId()` / `matchId()`：带消费行为的匹配接口。  
- `isExpressionStart()`：判断当前位置是否可作为表达式起点。

## 5. 日志与错误恢复

- `enterRule()` / `exitRule()`：记录规则进入与退出，维护日志缩进层级。  
- `logMatch()` / `logInfo()`：记录 token 匹配与普通信息。  
- `error()`：记录第一条错误信息并触发恢复。  
- `synchronize()`：恐慌模式，跳到 `;` 或关键结构起点后继续分析。

## 6. 语义动作与中间代码辅助

- `emitQuad()`、`backpatchQuadResult()`：四元式生成与回填。  
- `newTemp()`、`newLabel()`：生成临时变量与标签。  
- `enterScope()` / `leaveScope()`、`enterRoutine()` / `leaveRoutine()`：作用域与过程/函数上下文管理。  
- `declarePendingIdentifiers()`：将待声明标识符批量写入符号表并分配地址。  
- `newScopedName()`、`calleeRetName()`、`calleeResultName()`：构造带作用域后缀的名字，支持嵌套调用场景。

## 7. 典型调用流程

`parse()` 入口后的主流程可以概括为：

1. `parseProgram()`
2. `parseSubProgram()`
3. 声明阶段（变量/函数/过程/类型）
4. 语句阶段（赋值、调用、if、while、复合语句）
5. 表达式阶段由 `parseExpression()` 交给 LR(1)
6. 汇总日志、符号表和四元式输出

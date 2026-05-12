#ifndef NEUCOMPILER_PARSER_H
#define NEUCOMPILER_PARSER_H

#include <string>
#include <vector>
#include <sstream>
#include "global.h"
#include "synbl.h"

using namespace std;

// ============================================================
// Parser —— 递归下降语法分析器
//
// 负责：
//   1. 按 docs/文法.md 中的产生式进行语法分析
//   2. 输出结构化的解析日志（带缩进层次）
//   3. 错误恢复（恐慌模式）
//
// 表达式部分使用自动构建的 LR(1) 分析器（SELECT 集与分析表自动生成）。
//
// 语义动作预留：
//   每个 parseXxx() 函数返回 bool（成功/失败），函数体内用
//   /* SEMANTIC: ... */ 注释标记将来插入语义动作的位置。
//   语义分析阶段只需在这些标记处填入符号表操作和四元式生成代码，
//   无需修改函数签名或控制流。
// ============================================================
class Parser {
public:
    // 构造函数：接收词法分析器输出的 token 序列（只读引用）
    explicit Parser(const vector<Token>& tokens);

    // 主入口：执行语法分析
    // 返回 true 表示未发现语法错误（可能经过错误恢复）
    bool parse();

    // 获取累积的日志字符串
    string getLog() const;

    // 将日志写入 UTF-8 文件（便于查看）
    bool writeLogToFile(const string& filepath) const;

    // 是否有语法错误
    bool hasError() const { return hasError_; }

    // 获取第一条错误消息
    string getErrorMessage() const { return errorMsg_; }

private:
    // ==================== Token 导航 ====================
    const vector<Token>& tokens_;
    size_t pos_;                    // 当前 token 下标

    Token current() const;          // 当前 token
    Token peek(size_t ahead = 0) const; // 前瞻（0 = 当前）
    Token advance();                // 消耗当前并返回
    bool   isAtEnd() const;         // 是否到达末尾
    int    currentLine() const;     // 当前行号

    // ==================== Token 匹配（带日志） ====================
    bool checkType(const string& type) const;
    bool matchType(const string& type);

    bool checkKeyword(const string& kw) const;
    bool matchKeyword(const string& kw);

    bool checkDelimiter(const string& delim) const;
    bool matchDelimiter(const string& delim);

    bool checkId() const;
    bool matchId();

    // 判断当前 token 是否可以作为表达式的起始
    bool isExpressionStart() const;

    // ==================== 日志系统 ====================
    ostringstream log_;
    int indent_;                    // 当前缩进层级

    void enterRule(const string& name);
    void exitRule(const string& name, bool ok);
    void logMatch(const Token& token);
    void logInfo(const string& msg);

    // ==================== 错误处理 ====================
    bool   hasError_;
    string errorMsg_;
    bool expressionAnalysisPrinted_;

    void error(const string& msg);  // 报告错误
    void synchronize();             // 恐慌模式恢复

    // token 到人类可读字符串
    string tokenToString(const Token& t) const;

    // ==================== 语法规则（按文法 § 编号） ====================

    // §1  程序定义
    bool parseProgram();
    bool parseSubProgram();

    // §2  说明部分
    bool parseDeclarationPart();
    bool parseDeclarationStatement();

    // §3  变量说明
    bool parseVariableDeclaration();
    bool parseVariableDefinitionList();
    bool parseVariableDefinition();
    bool parseIdentifierList();
    bool parseIdentifierListTail();

    // §4  函数说明
    bool parseFunctionDeclaration();
    bool parseFormalParameters();
    bool parseParameterList();
    bool parseParameterListTail();
    bool parseParameterDefinition();
    bool parseValueParameter();
    bool parseVarParameter();

    // §5  过程说明
    bool parseProcedureDeclaration();

    // §6  复合语句
    bool parseCompoundStatement();
    bool parseStatementList();

    // §7  语句
    bool parseStatement();
    bool parseIfStatement();
    bool parseWhileStatement();
    bool parseAssignOrCallStatement();

    // §8  调用后缀
    bool parseCallSuffix();

    // §9  实参表
    bool parseActualParameterList();
    bool parseActualParameterListTail();

    // §10 表达式（占位，后续替换为 LR）
    bool parseExpression(const vector<string>& stopTokens = {});

    // §11 类型
    bool parseType();
};

#endif // NEUCOMPILER_PARSER_H

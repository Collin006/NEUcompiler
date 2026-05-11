#include "parser.h"
#include <stdexcept>
#include <algorithm>
#include <fstream>

// ============================================================
// 构造 & 顶层入口
// ============================================================

Parser::Parser(const vector<Token>& tokens)
    : tokens_(tokens)
    , pos_(0)
    , indent_(0)
    , hasError_(false)
{
}

bool Parser::parse() {
    log_ = ostringstream();
    indent_ = 0;
    hasError_ = false;
    errorMsg_.clear();

    if (tokens_.empty()) {
        logInfo("token 序列为空，无需分析");
        return true;
    }

    bool ok = parseProgram();

    if (!isAtEnd() && !hasError_) {
        error("解析结束后仍有未消耗的 token");
        ok = false;
    }

    if (ok && !hasError_) {
        logInfo("===== 语法分析通过 =====");
    } else {
        logInfo("===== 语法分析失败 =====");
    }

    return ok && !hasError_;
}

string Parser::getLog() const {
    return log_.str();
}

bool Parser::writeLogToFile(const string& filepath) const {
    ofstream out(filepath, ios::out | ios::trunc);
    if (!out.is_open()) return false;
    out << log_.str();
    out.close();
    return true;
}

// ============================================================
// Token 导航辅助
// ============================================================

Token Parser::current() const {
    if (isAtEnd()) {
        // 返回一个虚拟 token，避免越界
        Token dummy;
        dummy.type = "EOF";
        dummy.value = "";
        dummy.line = tokens_.empty() ? 0 : tokens_.back().line;
        return dummy;
    }
    return tokens_[pos_];
}

Token Parser::peek(size_t ahead) const {
    size_t idx = pos_ + ahead;
    if (idx >= tokens_.size()) {
        Token dummy;
        dummy.type = "EOF";
        dummy.value = "";
        dummy.line = tokens_.empty() ? 0 : tokens_.back().line;
        return dummy;
    }
    return tokens_[idx];
}

Token Parser::advance() {
    if (!isAtEnd()) {
        return tokens_[pos_++];
    }
    return current(); // EOF
}

bool Parser::isAtEnd() const {
    return pos_ >= tokens_.size();
}

int Parser::currentLine() const {
    if (isAtEnd() && !tokens_.empty()) {
        return tokens_.back().line;
    }
    return current().line;
}

// ============================================================
// Token 匹配
// ============================================================

bool Parser::checkType(const string& type) const {
    return !isAtEnd() && current().type == type;
}

bool Parser::matchType(const string& type) {
    if (checkType(type)) {
        logMatch(advance());
        return true;
    }
    return false;
}

bool Parser::checkKeyword(const string& kw) const {
    if (isAtEnd()) return false;
    Token t = current();
    if (t.type != "KEYWORD") return false;
    int idx = stoi(t.value);
    if (idx < 0 || idx >= static_cast<int>(ctx.keywordTable.size())) return false;
    return ctx.keywordTable[idx] == kw;
}

bool Parser::matchKeyword(const string& kw) {
    if (checkKeyword(kw)) {
        logMatch(advance());
        return true;
    }
    return false;
}

bool Parser::checkDelimiter(const string& delim) const {
    if (isAtEnd()) return false;
    Token t = current();
    if (t.type != "DELIMITER") return false;
    int idx = stoi(t.value);
    if (idx < 0 || idx >= static_cast<int>(ctx.delimiterTable.size())) return false;
    return ctx.delimiterTable[idx] == delim;
}

bool Parser::matchDelimiter(const string& delim) {
    if (checkDelimiter(delim)) {
        logMatch(advance());
        return true;
    }
    return false;
}

bool Parser::checkId() const {
    return !isAtEnd() && current().type == "ID";
}

bool Parser::matchId() {
    if (checkId()) {
        logMatch(advance());
        return true;
    }
    return false;
}

bool Parser::isExpressionStart() const {
    if (isAtEnd()) return false;
    Token t = current();

    // 标识符
    if (t.type == "ID") return true;

    // 常量
    if (t.type == "CONSL1" || t.type == "CONSL2" || t.type == "STRING")
        return true;

    // 关键字 true / false
    if (t.type == "KEYWORD") {
        int idx = stoi(t.value);
        if (idx >= 0 && idx < static_cast<int>(ctx.keywordTable.size())) {
            const string& kw = ctx.keywordTable[idx];
            if (kw == "true" || kw == "false") return true;
        }
    }

    // 界符 ( - !
    if (t.type == "DELIMITER") {
        int idx = stoi(t.value);
        if (idx >= 0 && idx < static_cast<int>(ctx.delimiterTable.size())) {
            const string& d = ctx.delimiterTable[idx];
            if (d == "(" || d == "-" || d == "!") return true;
        }
    }

    return false;
}

// ============================================================
// 日志 & 错误处理
// ============================================================

void Parser::enterRule(const string& name) {
    log_ << string(indent_ * 2, ' ') << "[ENTER] <" << name << ">\n";
    indent_++;
}

void Parser::exitRule(const string& name, bool ok) {
    indent_--;
    log_ << string(indent_ * 2, ' ') << "[EXIT]  <" << name << "> "
         << (ok ? "OK" : "FAIL") << "\n";
}

void Parser::logMatch(const Token& token) {
    log_ << string(indent_ * 2, ' ') << "[MATCH] "
         << token.type << " \"" << tokenToString(token) << "\""
         << "  (line " << token.line << ")\n";
}

void Parser::logInfo(const string& msg) {
    log_ << string(indent_ * 2, ' ') << "[INFO]  " << msg << "\n";
}

void Parser::error(const string& msg) {
    hasError_ = true;
    string fullMsg = "line " + to_string(currentLine()) + ": " + msg;
    if (errorMsg_.empty()) {
        errorMsg_ = fullMsg;
    }
    log_ << string(indent_ * 2, ' ') << "[ERROR] " << fullMsg << "\n";
    synchronize();
}

void Parser::synchronize() {
    // 恐慌模式：跳过 token 直到找到同步点
    while (!isAtEnd()) {
        Token t = current();

        // ; 是良好的同步点（消耗它）
        if (t.type == "DELIMITER") {
            int idx = stoi(t.value);
            if (idx >= 0 && idx < static_cast<int>(ctx.delimiterTable.size())) {
                if (ctx.delimiterTable[idx] == ";") {
                    advance();
                    return;
                }
            }
        }

        // 这些关键字标志着新结构的开始，停下来但不消耗
        if (t.type == "KEYWORD") {
            int idx = stoi(t.value);
            if (idx >= 0 && idx < static_cast<int>(ctx.keywordTable.size())) {
                const string& kw = ctx.keywordTable[idx];
                if (kw == "begin" || kw == "end" || kw == "var" ||
                    kw == "function" || kw == "procedure" ||
                    kw == "if" || kw == "while") {
                    return;
                }
            }
        }

        advance();
    }
}

string Parser::tokenToString(const Token& t) const {
    if (t.type == "KEYWORD") {
        int idx = stoi(t.value);
        if (idx >= 0 && idx < static_cast<int>(ctx.keywordTable.size()))
            return ctx.keywordTable[idx];
    } else if (t.type == "ID") {
        int idx = stoi(t.value);
        if (idx >= 0 && idx < static_cast<int>(ctx.synbl.size()))
            return ctx.synbl[idx].name;
    } else if (t.type == "DELIMITER") {
        int idx = stoi(t.value);
        if (idx >= 0 && idx < static_cast<int>(ctx.delimiterTable.size()))
            return ctx.delimiterTable[idx];
    } else if (t.type == "CONSL1") {
        int idx = stoi(t.value);
        if (idx >= 0 && idx < static_cast<int>(ctx.consl1.size()))
            return to_string(ctx.consl1[idx]);
    } else if (t.type == "CONSL2") {
        int idx = stoi(t.value);
        if (idx >= 0 && idx < static_cast<int>(ctx.consl2.size()))
            return to_string(ctx.consl2[idx]);
    } else if (t.type == "STRING") {
        return "'" + t.value + "'";
    } else if (t.type == "EOF") {
        return "<EOF>";
    }
    return "?";
}

// ============================================================
// §1  程序定义
//     〈程序〉 → program ID ; 〈分程序〉 .
//     〈分程序〉 → 〈说明部分〉〈复合语句〉
// ============================================================

bool Parser::parseProgram() {
    enterRule("程序");

    if (!matchKeyword("program")) {
        error("缺少关键字 'program'");
        exitRule("程序", false);
        return false;
    }

    /* SEMANTIC: 程序名入符号表 */

    if (!matchId()) {
        error("缺少程序名（标识符）");
        exitRule("程序", false);
        return false;
    }

    if (!matchDelimiter(";")) {
        error("程序名后缺少 ';'");
        exitRule("程序", false);
        return false;
    }

    if (!parseSubProgram()) {
        exitRule("程序", false);
        return false;
    }

    if (!matchDelimiter(".")) {
        error("程序末尾缺少 '.'");
        exitRule("程序", false);
        return false;
    }

    exitRule("程序", true);
    return true;
}

bool Parser::parseSubProgram() {
    enterRule("分程序");

    /* SEMANTIC: 进入新的作用域层级 */

    if (!parseDeclarationPart()) {
        exitRule("分程序", false);
        return false;
    }

    if (!parseCompoundStatement()) {
        exitRule("分程序", false);
        return false;
    }

    /* SEMANTIC: 退出作用域层级 */

    exitRule("分程序", true);
    return true;
}

// ============================================================
// §2  说明部分
//     〈说明部分〉 → 〈说明语句〉〈说明部分〉 | ε
//     〈说明语句〉 → 〈变量说明〉 | 〈函数说明〉 | 〈过程说明〉
// ============================================================

bool Parser::parseDeclarationPart() {
    enterRule("说明部分");

    // FIRST(〈说明语句〉) = { var, function, procedure }
    while (checkKeyword("var") || checkKeyword("function") || checkKeyword("procedure")) {
        if (!parseDeclarationStatement()) {
            exitRule("说明部分", false);
            return false;
        }
    }
    // ε 情形：直接退出

    exitRule("说明部分", true);
    return true;
}

bool Parser::parseDeclarationStatement() {
    enterRule("说明语句");

    bool ok = false;
    if (checkKeyword("var")) {
        ok = parseVariableDeclaration();
    } else if (checkKeyword("function")) {
        ok = parseFunctionDeclaration();
    } else if (checkKeyword("procedure")) {
        ok = parseProcedureDeclaration();
    } else {
        error("缺少 var / function / procedure");
        ok = false;
    }

    exitRule("说明语句", ok);
    return ok;
}

// ============================================================
// §3  变量说明
//     〈变量说明〉 → var 〈变量定义表〉
//     〈变量定义表〉 → 〈变量定义〉 ; 〈变量定义表〉
//                    | 〈变量定义〉 ;
//     〈变量定义〉 → 〈标识符表〉 : 〈类型〉
//     〈标识符表〉 → ID 〈标识符表尾〉
//     〈标识符表尾〉 → , ID 〈标识符表尾〉 | ε
// ============================================================

bool Parser::parseVariableDeclaration() {
    enterRule("变量说明");

    if (!matchKeyword("var")) {
        error("缺少关键字 'var'");
        exitRule("变量说明", false);
        return false;
    }

    if (!parseVariableDefinitionList()) {
        exitRule("变量说明", false);
        return false;
    }

    exitRule("变量说明", true);
    return true;
}

bool Parser::parseVariableDefinitionList() {
    enterRule("变量定义表");

    // 至少一条变量定义
    if (!parseVariableDefinition()) {
        exitRule("变量定义表", false);
        return false;
    }

    if (!matchDelimiter(";")) {
        error("变量定义后缺少 ';'（分号为终止符）");
        exitRule("变量定义表", false);
        return false;
    }

    /* SEMANTIC: 将解析到的标识符注册到符号表 */

    // 循环处理后续定义：FIRST(〈变量定义〉) = { ID }
    while (checkId()) {
        if (!parseVariableDefinition()) {
            exitRule("变量定义表", false);
            return false;
        }

        if (!matchDelimiter(";")) {
            error("变量定义后缺少 ';'（分号为终止符）");
            exitRule("变量定义表", false);
            return false;
        }

        /* SEMANTIC: 将解析到的标识符注册到符号表 */
    }

    exitRule("变量定义表", true);
    return true;
}

bool Parser::parseVariableDefinition() {
    enterRule("变量定义");

    if (!parseIdentifierList()) {
        exitRule("变量定义", false);
        return false;
    }

    if (!matchDelimiter(":")) {
        error("缺少 ':'（变量定义中类型前）");
        exitRule("变量定义", false);
        return false;
    }

    if (!parseType()) {
        exitRule("变量定义", false);
        return false;
    }

    /* SEMANTIC: 为标识符表关联类型 */

    exitRule("变量定义", true);
    return true;
}

bool Parser::parseIdentifierList() {
    enterRule("标识符表");

    if (!matchId()) {
        error("缺少标识符");
        exitRule("标识符表", false);
        return false;
    }

    /* SEMANTIC: 收集标识符 */

    if (!parseIdentifierListTail()) {
        exitRule("标识符表", false);
        return false;
    }

    exitRule("标识符表", true);
    return true;
}

bool Parser::parseIdentifierListTail() {
    enterRule("标识符表尾");

    while (matchDelimiter(",")) {
        if (!matchId()) {
            error("',' 后缺少标识符");
            exitRule("标识符表尾", false);
            return false;
        }
        /* SEMANTIC: 收集标识符 */
    }
    // ε 情形

    exitRule("标识符表尾", true);
    return true;
}

// ============================================================
// §4  函数说明
//     〈函数说明〉 → function ID 〈形式参数〉 : 〈类型〉 ;
//                    〈分程序〉 ;
//     〈形式参数〉 → ( 〈参数表〉 ) | ε
//     〈参数表〉 → 〈参数定义〉〈参数表尾〉
//     〈参数表尾〉 → ; 〈参数定义〉〈参数表尾〉 | ε
//     〈参数定义〉 → 〈值参数〉 | 〈变量参数〉
//     〈值参数〉 → 〈标识符表〉 : 〈类型〉
//     〈变量参数〉 → var 〈标识符表〉 : 〈类型〉
// ============================================================

bool Parser::parseFunctionDeclaration() {
    enterRule("函数说明");

    if (!matchKeyword("function")) {
        error("缺少关键字 'function'");
        exitRule("函数说明", false);
        return false;
    }

    if (!matchId()) {
        error("缺少函数名");
        exitRule("函数说明", false);
        return false;
    }

    /* SEMANTIC: 函数名入符号表，cat = 'f' */

    if (!parseFormalParameters()) {
        exitRule("函数说明", false);
        return false;
    }

    if (!matchDelimiter(":")) {
        error("函数缺少返回类型前的 ':'");
        exitRule("函数说明", false);
        return false;
    }

    if (!parseType()) {
        exitRule("函数说明", false);
        return false;
    }

    /* SEMANTIC: 设置函数返回类型 */

    if (!matchDelimiter(";")) {
        error("函数返回类型后缺少 ';'");
        exitRule("函数说明", false);
        return false;
    }

    if (!parseSubProgram()) {
        exitRule("函数说明", false);
        return false;
    }

    if (!matchDelimiter(";")) {
        error("函数体后缺少 ';'");
        exitRule("函数说明", false);
        return false;
    }

    /* SEMANTIC: 函数定义结束，回填地址 */

    exitRule("函数说明", true);
    return true;
}

bool Parser::parseFormalParameters() {
    enterRule("形式参数");

    if (matchDelimiter("(")) {
        if (!checkDelimiter(")")) {
            // 非空参数表
            if (!parseParameterList()) {
                exitRule("形式参数", false);
                return false;
            }
        }
        // ε 情形（空括号内）：不做额外处理

        if (!matchDelimiter(")")) {
            error("缺少 ')' 关闭形式参数");
            exitRule("形式参数", false);
            return false;
        }
    }
    // ε 情形（无括号）：直接返回

    /* SEMANTIC: 记录形参个数 */

    exitRule("形式参数", true);
    return true;
}

bool Parser::parseParameterList() {
    enterRule("参数表");

    if (!parseParameterDefinition()) {
        exitRule("参数表", false);
        return false;
    }

    if (!parseParameterListTail()) {
        exitRule("参数表", false);
        return false;
    }

    exitRule("参数表", true);
    return true;
}

bool Parser::parseParameterListTail() {
    enterRule("参数表尾");

    while (matchDelimiter(";")) {
        if (!parseParameterDefinition()) {
            exitRule("参数表尾", false);
            return false;
        }
    }
    // ε 情形

    exitRule("参数表尾", true);
    return true;
}

bool Parser::parseParameterDefinition() {
    enterRule("参数定义");

    bool ok;
    if (checkKeyword("var")) {
        ok = parseVarParameter();
    } else {
        ok = parseValueParameter();
    }

    exitRule("参数定义", ok);
    return ok;
}

bool Parser::parseValueParameter() {
    enterRule("值参数");

    if (!parseIdentifierList()) {
        exitRule("值参数", false);
        return false;
    }

    if (!matchDelimiter(":")) {
        error("值参数缺少类型前的 ':'");
        exitRule("值参数", false);
        return false;
    }

    if (!parseType()) {
        exitRule("值参数", false);
        return false;
    }

    /* SEMANTIC: 值参数入符号表，cat = 'vf' */

    exitRule("值参数", true);
    return true;
}

bool Parser::parseVarParameter() {
    enterRule("变量参数");

    if (!matchKeyword("var")) {
        error("缺少关键字 'var'");
        exitRule("变量参数", false);
        return false;
    }

    if (!parseIdentifierList()) {
        exitRule("变量参数", false);
        return false;
    }

    if (!matchDelimiter(":")) {
        error("变量参数缺少类型前的 ':'");
        exitRule("变量参数", false);
        return false;
    }

    if (!parseType()) {
        exitRule("变量参数", false);
        return false;
    }

    /* SEMANTIC: 变量参数入符号表，cat = 'vn' */

    exitRule("变量参数", true);
    return true;
}

// ============================================================
// §5  过程说明
//     〈过程说明〉 → procedure ID 〈形式参数〉 ; 〈分程序〉 ;
// ============================================================

bool Parser::parseProcedureDeclaration() {
    enterRule("过程说明");

    if (!matchKeyword("procedure")) {
        error("缺少关键字 'procedure'");
        exitRule("过程说明", false);
        return false;
    }

    if (!matchId()) {
        error("缺少过程名");
        exitRule("过程说明", false);
        return false;
    }

    /* SEMANTIC: 过程名入符号表 */

    if (!parseFormalParameters()) {
        exitRule("过程说明", false);
        return false;
    }

    if (!matchDelimiter(";")) {
        error("过程参数后缺少 ';'");
        exitRule("过程说明", false);
        return false;
    }

    if (!parseSubProgram()) {
        exitRule("过程说明", false);
        return false;
    }

    if (!matchDelimiter(";")) {
        error("过程体后缺少 ';'");
        exitRule("过程说明", false);
        return false;
    }

    exitRule("过程说明", true);
    return true;
}

// ============================================================
// §6  复合语句
//     〈复合语句〉 → begin 〈语句表〉 end
//     〈语句表〉 → 〈语句〉 ; 〈语句表〉 | ε
// ============================================================

bool Parser::parseCompoundStatement() {
    enterRule("复合语句");

    if (!matchKeyword("begin")) {
        error("缺少关键字 'begin'");
        exitRule("复合语句", false);
        return false;
    }

    if (!parseStatementList()) {
        exitRule("复合语句", false);
        return false;
    }

    if (!matchKeyword("end")) {
        error("缺少关键字 'end'");
        exitRule("复合语句", false);
        return false;
    }

    exitRule("复合语句", true);
    return true;
}

bool Parser::parseStatementList() {
    enterRule("语句表");

    // FIRST(〈语句〉) = { ID, begin, if, while }
    while (checkId() || checkKeyword("begin") || checkKeyword("if") || checkKeyword("while")) {
        if (!parseStatement()) {
            exitRule("语句表", false);
            return false;
        }

        if (!matchDelimiter(";")) {
            error("语句后缺少 ';'（分号为终止符）");
            exitRule("语句表", false);
            return false;
        }
    }
    // ε 情形

    exitRule("语句表", true);
    return true;
}

// ============================================================
// §7  语句定义
//
// 递归下降中对 if 语句采用贪婪 else 策略，自然消解悬空 else 歧义。
// 〈匹配语句〉/〈开放语句〉 的区分在递归下降中无需显式编码，
// 由 parseIfStatement 内 eager else 匹配保证正确绑定。
// ============================================================

bool Parser::parseStatement() {
    enterRule("语句");

    bool ok = false;
    if (checkKeyword("if")) {
        ok = parseIfStatement();
    } else if (checkKeyword("while")) {
        ok = parseWhileStatement();
    } else if (checkKeyword("begin")) {
        ok = parseCompoundStatement();
    } else if (checkId()) {
        ok = parseAssignOrCallStatement();
    } else {
        error("不是合法的语句起始");
        ok = false;
    }

    exitRule("语句", ok);
    return ok;
}

bool Parser::parseIfStatement() {
    enterRule("if语句");

    if (!matchKeyword("if")) {
        error("缺少关键字 'if'");
        exitRule("if语句", false);
        return false;
    }

    // 条件表达式
    if (!parseExpression({"then"})) {
        exitRule("if语句", false);
        return false;
    }

    if (!matchKeyword("then")) {
        error("缺少关键字 'then'");
        exitRule("if语句", false);
        return false;
    }

    /* SEMANTIC: 生成条件跳转四元式（真出口待回填） */

    // then 分支
    if (!parseStatement()) {
        exitRule("if语句", false);
        return false;
    }

    /* SEMANTIC: 回填真出口 / 生成无条件跳转（跳过 else） */

    // 可选的 else 分支
    if (matchKeyword("else")) {
        /* SEMANTIC: 处理 else 前的跳转 */

        if (!parseStatement()) {
            exitRule("if语句", false);
            return false;
        }

        /* SEMANTIC: 回填假出口 */
    } else {
        /* SEMANTIC: 回填假出口到当前位置 */
    }

    exitRule("if语句", true);
    return true;
}

bool Parser::parseWhileStatement() {
    enterRule("while语句");

    if (!matchKeyword("while")) {
        error("缺少关键字 'while'");
        exitRule("while语句", false);
        return false;
    }

    /* SEMANTIC: 记录循环起始地址 */

    if (!parseExpression({"do"})) {
        exitRule("while语句", false);
        return false;
    }

    if (!matchKeyword("do")) {
        error("缺少关键字 'do'");
        exitRule("while语句", false);
        return false;
    }

    /* SEMANTIC: 生成条件跳转四元式 */

    if (!parseStatement()) {
        exitRule("while语句", false);
        return false;
    }

    /* SEMANTIC: 生成无条件跳转回循环头 + 回填假出口 */

    exitRule("while语句", true);
    return true;
}

bool Parser::parseAssignOrCallStatement() {
    enterRule("赋值或调用语句");

    if (!matchId()) {
        error("缺少标识符");
        exitRule("赋值或调用语句", false);
        return false;
    }

    /* SEMANTIC: 查符号表获取该标识符信息 */

    if (checkDelimiter(":=")) {
        // 赋值语句
        if (!matchDelimiter(":=")) {
            error("缺少 ':='");
            exitRule("赋值或调用语句", false);
            return false;
        }

        if (!parseExpression({";", "end", "else"})) {
            exitRule("赋值或调用语句", false);
            return false;
        }

        /* SEMANTIC: 生成赋值四元式 */
    } else {
        // 过程调用（含无参调用）
        if (!parseCallSuffix()) {
            exitRule("赋值或调用语句", false);
            return false;
        }

        /* SEMANTIC: 生成过程调用四元式（或函数调用丢弃返回值） */
    }

    exitRule("赋值或调用语句", true);
    return true;
}

// ============================================================
// §8  调用后缀
//     〈调用后缀〉 → ( 〈实参表〉 ) | ε
// ============================================================

bool Parser::parseCallSuffix() {
    enterRule("调用后缀");

    if (matchDelimiter("(")) {
        if (!checkDelimiter(")")) {
            if (!parseActualParameterList()) {
                exitRule("调用后缀", false);
                return false;
            }
        }

        if (!matchDelimiter(")")) {
            error("缺少 ')' 关闭实参表");
            exitRule("调用后缀", false);
            return false;
        }
    }
    // ε 情形：无参调用

    exitRule("调用后缀", true);
    return true;
}

// ============================================================
// §9  实参表
//     〈实参表〉 → 〈表达式〉〈实参表尾〉 | ε
//     〈实参表尾〉 → , 〈表达式〉〈实参表尾〉 | ε
// ============================================================

bool Parser::parseActualParameterList() {
    enterRule("实参表");

    // FIRST(〈表达式〉) → isExpressionStart()
    if (isExpressionStart()) {
        if (!parseExpression({",", ")"})) {
            exitRule("实参表", false);
            return false;
        }

        /* SEMANTIC: 记录一个实参 */

        if (!parseActualParameterListTail()) {
            exitRule("实参表", false);
            return false;
        }
    }
    // ε 情形

    exitRule("实参表", true);
    return true;
}

bool Parser::parseActualParameterListTail() {
    enterRule("实参表尾");

    while (matchDelimiter(",")) {
        if (!parseExpression({",", ")"})) {
            exitRule("实参表尾", false);
            return false;
        }

        /* SEMANTIC: 记录一个实参 */
    }
    // ε 情形

    exitRule("实参表尾", true);
    return true;
}

// ============================================================
// §10 表达式（占位实现）
//
// 完整的表达式分析（布尔表达式、关系表达式、算术表达式）
// 后续由 LR / SLR 分析器接管。
// 当前占位策略：依次消耗表达式 token，遇到 stopToken 或语句边界即停止。
// ============================================================

bool Parser::parseExpression(const vector<string>& stopTokens) {
    enterRule("表达式");

    int parenDepth = 0; // 跟踪括号嵌套

    while (!isAtEnd()) {
        Token t = current();

        // 检查是否为停止 token（只在括号深度为 0 时生效）
        if (t.type == "KEYWORD" && parenDepth == 0) {
            int idx = stoi(t.value);
            if (idx >= 0 && idx < static_cast<int>(ctx.keywordTable.size())) {
                const string& kw = ctx.keywordTable[idx];
                bool isStop = false;
                for (const string& s : stopTokens) {
                    if (kw == s) { isStop = true; break; }
                }
                if (isStop) break;
            }
        }

        if (t.type == "DELIMITER" && parenDepth == 0) {
            int idx = stoi(t.value);
            if (idx >= 0 && idx < static_cast<int>(ctx.delimiterTable.size())) {
                const string& d = ctx.delimiterTable[idx];
                bool isStop = false;
                for (const string& s : stopTokens) {
                    if (d == s) { isStop = true; break; }
                }
                if (isStop) break;
            }
        }

        // 跟踪括号深度
        if (t.type == "DELIMITER") {
            int idx = stoi(t.value);
            if (idx >= 0 && idx < static_cast<int>(ctx.delimiterTable.size())) {
                const string& d = ctx.delimiterTable[idx];
                if (d == "(") parenDepth++;
                if (d == ")") {
                    parenDepth--;
                    if (parenDepth < 0) break; // 多余的 )
                }
            }
        }

        // 消耗并记录 token
        logMatch(advance());
    }

    logInfo("表达式解析（占位实现，LR 分析待完成）");

    exitRule("表达式", true);
    return true;
}

// ============================================================
// §11 类型
//     〈类型〉 → integer | real | char | boolean | string
// ============================================================

bool Parser::parseType() {
    enterRule("类型");

    if (matchKeyword("integer") || matchKeyword("real") ||
        matchKeyword("char")    || matchKeyword("boolean") ||
        matchKeyword("string")) {
        /* SEMANTIC: 返回类型编码（tval） */
        exitRule("类型", true);
        return true;
    }

    error("缺少类型（integer / real / char / boolean / string）");
    exitRule("类型", false);
    return false;
}

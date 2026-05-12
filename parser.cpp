#include "parser.h"
#include <stdexcept>
#include <algorithm>
#include <fstream>
#include <set>
#include <map>
#include <queue>
#include <unordered_set>

namespace {

const string EPSILON = "ε";

struct Production {
    string lhs;
    vector<string> rhs;
};

struct LR1Item {
    int productionIndex;
    int dotPos;
    string lookahead;

    bool operator==(const LR1Item& other) const {
        return productionIndex == other.productionIndex &&
               dotPos == other.dotPos &&
               lookahead == other.lookahead;
    }

    bool operator<(const LR1Item& other) const {
        if (productionIndex != other.productionIndex) return productionIndex < other.productionIndex;
        if (dotPos != other.dotPos) return dotPos < other.dotPos;
        return lookahead < other.lookahead;
    }
};

struct LRAction {
    enum Type { Error, Shift, Reduce, Accept } type = Error;
    int value = -1; // Shift: state, Reduce: productionIndex
};

class ExpressionLR1Builder {
public:
    ExpressionLR1Builder() {
        initGrammar();
        buildOk_ = computeFirstSets() && buildCanonicalCollection() && buildParsingTable();
        if (buildOk_) {
            computeSelectSets();
        }
    }

    bool isBuildOk() const { return buildOk_; }
    string buildError() const { return buildError_; }

    bool parse(const vector<string>& inputSymbols, string& errorMsg) const {
        vector<int> stateStack;
        stateStack.push_back(0);
        size_t ip = 0;

        while (true) {
            int state = stateStack.back();
            string lookahead = (ip < inputSymbols.size()) ? inputSymbols[ip] : "$";
            LRAction action = getAction(state, lookahead);

            if (action.type == LRAction::Shift) {
                stateStack.push_back(action.value);
                ip++;
                continue;
            }

            if (action.type == LRAction::Reduce) {
                const Production& p = productions_[action.value];
                for (size_t i = 0; i < p.rhs.size(); ++i) {
                    if (stateStack.empty()) {
                        errorMsg = "LR 栈下溢（规约阶段）";
                        return false;
                    }
                    stateStack.pop_back();
                }

                if (stateStack.empty()) {
                    errorMsg = "LR 栈为空，无法 GOTO";
                    return false;
                }

                int fromState = stateStack.back();
                auto gIt = gotoTable_.find({fromState, p.lhs});
                if (gIt == gotoTable_.end()) {
                    errorMsg = "缺少 GOTO[" + to_string(fromState) + ", " + p.lhs + "]";
                    return false;
                }
                stateStack.push_back(gIt->second);
                continue;
            }

            if (action.type == LRAction::Accept) {
                if (ip != inputSymbols.size()) {
                    errorMsg = "表达式未完全消耗";
                    return false;
                }
                return true;
            }

            errorMsg = "无法在状态 " + to_string(state) + " 处理符号 '" + lookahead + "'";
            vector<string> expected = expectedTerminals(state);
            if (!expected.empty()) {
                errorMsg += "，期望: ";
                for (size_t i = 0; i < expected.size(); ++i) {
                    if (i) errorMsg += ", ";
                    errorMsg += expected[i];
                }
            }
            return false;
        }
    }

    string dumpSelectAndTable() const {
        ostringstream out;
        out << "===== 表达式文法 SELECT 集（自动构建） =====\n";
        for (size_t i = 1; i < productions_.size(); ++i) { // 跳过增广文法
            out << "P" << i << ": " << productionToString(i) << "\n";
            out << "  SELECT = {";
            bool first = true;
            auto it = selectSets_.find(static_cast<int>(i));
            if (it != selectSets_.end()) {
                for (const string& t : it->second) {
                    if (!first) out << ", ";
                    out << t;
                    first = false;
                }
            }
            out << "}\n";
        }

        out << "===== 表达式 LR(1) ACTION 表（自动构建） =====\n";
        for (size_t state = 0; state < states_.size(); ++state) {
            bool printed = false;
            for (const string& t : orderedTerminalsWithEnd_) {
                auto it = actionTable_.find({static_cast<int>(state), t});
                if (it == actionTable_.end()) continue;
                if (!printed) {
                    out << "State " << state << ":\n";
                    printed = true;
                }
                out << "  ACTION[" << t << "] = " << actionToString(it->second) << "\n";
            }
        }

        out << "===== 表达式 LR(1) GOTO 表（自动构建） =====\n";
        for (size_t state = 0; state < states_.size(); ++state) {
            bool printed = false;
            for (const string& nt : orderedNonTerminals_) {
                if (nt == startPrime_) continue;
                auto it = gotoTable_.find({static_cast<int>(state), nt});
                if (it == gotoTable_.end()) continue;
                if (!printed) {
                    out << "State " << state << ":\n";
                    printed = true;
                }
                out << "  GOTO[" << nt << "] = " << it->second << "\n";
            }
        }
        return out.str();
    }

private:
    string startSymbol_ = "Expr";
    string startPrime_ = "Expr'";

    vector<Production> productions_;
    set<string> nonTerminals_;
    set<string> terminals_;

    map<string, set<string>> firstSets_;

    vector<set<LR1Item>> states_;
    map<pair<int, string>, int> transitions_;
    map<pair<int, string>, LRAction> actionTable_;
    map<pair<int, string>, int> gotoTable_;
    map<int, set<string>> selectSets_;

    vector<string> orderedTerminalsWithEnd_;
    vector<string> orderedNonTerminals_;

    bool buildOk_ = false;
    string buildError_;

private:
    void initGrammar() {
        // 增广文法
        productions_.push_back({startPrime_, {startSymbol_}});

        // 形式化表达式文法
        productions_.push_back({"Expr", {"BoolExpr"}});
        productions_.push_back({"BoolExpr", {"BoolExpr", "||", "BoolTerm"}});
        productions_.push_back({"BoolExpr", {"BoolTerm"}});
        productions_.push_back({"BoolTerm", {"BoolTerm", "&&", "BoolFactor"}});
        productions_.push_back({"BoolTerm", {"BoolFactor"}});
        productions_.push_back({"BoolFactor", {"!", "BoolFactor"}});
        productions_.push_back({"BoolFactor", {"RelExpr"}});
        productions_.push_back({"RelExpr", {"ArithExpr", "RelOp", "ArithExpr"}});
        productions_.push_back({"RelExpr", {"ArithExpr"}});
        productions_.push_back({"RelOp", {">"}});
        productions_.push_back({"RelOp", {">="}});
        productions_.push_back({"RelOp", {"<"}});
        productions_.push_back({"RelOp", {"<="}});
        productions_.push_back({"RelOp", {"="}});
        productions_.push_back({"RelOp", {"<>"}});
        productions_.push_back({"ArithExpr", {"ArithExpr", "+", "Term"}});
        productions_.push_back({"ArithExpr", {"ArithExpr", "-", "Term"}});
        productions_.push_back({"ArithExpr", {"Term"}});
        productions_.push_back({"Term", {"Term", "*", "Factor"}});
        productions_.push_back({"Term", {"Term", "/", "Factor"}});
        productions_.push_back({"Term", {"Factor"}});
        productions_.push_back({"Factor", {"id", "CallSuffix"}});
        productions_.push_back({"Factor", {"int"}});
        productions_.push_back({"Factor", {"real"}});
        productions_.push_back({"Factor", {"string_lit"}});
        productions_.push_back({"Factor", {"true"}});
        productions_.push_back({"Factor", {"false"}});
        productions_.push_back({"Factor", {"(", "Expr", ")"}});
        productions_.push_back({"Factor", {"-", "Factor"}});
        productions_.push_back({"CallSuffix", {"(", "ActualParamList", ")"}});
        productions_.push_back({"CallSuffix", {}}); // ε
        productions_.push_back({"ActualParamList", {"Expr", "ActualParamListTail"}});
        productions_.push_back({"ActualParamList", {}}); // ε
        productions_.push_back({"ActualParamListTail", {",", "Expr", "ActualParamListTail"}});
        productions_.push_back({"ActualParamListTail", {}}); // ε

        for (const Production& p : productions_) {
            nonTerminals_.insert(p.lhs);
        }

        for (const Production& p : productions_) {
            for (const string& sym : p.rhs) {
                if (!nonTerminals_.count(sym)) {
                    terminals_.insert(sym);
                }
            }
        }

        orderedNonTerminals_.assign(nonTerminals_.begin(), nonTerminals_.end());
        orderedTerminalsWithEnd_.assign(terminals_.begin(), terminals_.end());
        orderedTerminalsWithEnd_.push_back("$");
    }

    bool isTerminal(const string& sym) const {
        return terminals_.count(sym) > 0 || sym == "$";
    }

    bool computeFirstSets() {
        for (const string& t : terminals_) {
            firstSets_[t].insert(t);
        }
        firstSets_["$"].insert("$");
        for (const string& nt : nonTerminals_) {
            (void)firstSets_[nt];
        }

        bool changed = true;
        while (changed) {
            changed = false;
            for (const Production& p : productions_) {
                if (p.rhs.empty()) {
                    if (firstSets_[p.lhs].insert(EPSILON).second) changed = true;
                    continue;
                }

                bool allNullable = true;
                for (const string& sym : p.rhs) {
                    const set<string>& fs = firstSets_[sym];
                    for (const string& x : fs) {
                        if (x == EPSILON) continue;
                        if (firstSets_[p.lhs].insert(x).second) changed = true;
                    }

                    if (!fs.count(EPSILON)) {
                        allNullable = false;
                        break;
                    }
                }

                if (allNullable) {
                    if (firstSets_[p.lhs].insert(EPSILON).second) changed = true;
                }
            }
        }
        return true;
    }

    set<string> firstOfSequence(const vector<string>& sequence) const {
        set<string> result;
        if (sequence.empty()) {
            result.insert(EPSILON);
            return result;
        }

        bool allNullable = true;
        for (const string& sym : sequence) {
            auto it = firstSets_.find(sym);
            if (it == firstSets_.end()) {
                result.insert(sym);
                allNullable = false;
                break;
            }

            for (const string& x : it->second) {
                if (x != EPSILON) result.insert(x);
            }

            if (!it->second.count(EPSILON)) {
                allNullable = false;
                break;
            }
        }

        if (allNullable) result.insert(EPSILON);
        return result;
    }

    set<LR1Item> closure(const set<LR1Item>& items) const {
        set<LR1Item> result = items;
        bool changed = true;
        while (changed) {
            changed = false;
            vector<LR1Item> snapshot(result.begin(), result.end());
            for (const LR1Item& item : snapshot) {
                const Production& p = productions_[item.productionIndex];
                if (item.dotPos >= static_cast<int>(p.rhs.size())) continue;

                const string& B = p.rhs[item.dotPos];
                if (!nonTerminals_.count(B)) continue;

                vector<string> beta;
                for (size_t i = static_cast<size_t>(item.dotPos + 1); i < p.rhs.size(); ++i) {
                    beta.push_back(p.rhs[i]);
                }
                beta.push_back(item.lookahead);

                set<string> lookaheads = firstOfSequence(beta);
                for (size_t pi = 0; pi < productions_.size(); ++pi) {
                    if (productions_[pi].lhs != B) continue;
                    for (const string& la : lookaheads) {
                        if (la == EPSILON) continue;
                        LR1Item ni{static_cast<int>(pi), 0, la};
                        if (result.insert(ni).second) changed = true;
                    }
                }
            }
        }
        return result;
    }

    set<LR1Item> gotoItems(const set<LR1Item>& items, const string& symbol) const {
        set<LR1Item> moved;
        for (const LR1Item& item : items) {
            const Production& p = productions_[item.productionIndex];
            if (item.dotPos < static_cast<int>(p.rhs.size()) && p.rhs[item.dotPos] == symbol) {
                moved.insert({item.productionIndex, item.dotPos + 1, item.lookahead});
            }
        }
        if (moved.empty()) return moved;
        return closure(moved);
    }

    int findState(const set<LR1Item>& state) const {
        for (size_t i = 0; i < states_.size(); ++i) {
            if (states_[i] == state) return static_cast<int>(i);
        }
        return -1;
    }

    bool buildCanonicalCollection() {
        set<LR1Item> initItems;
        initItems.insert({0, 0, "$"});
        states_.push_back(closure(initItems));

        queue<int> q;
        q.push(0);

        set<string> symbols = terminals_;
        symbols.insert(nonTerminals_.begin(), nonTerminals_.end());

        while (!q.empty()) {
            int i = q.front();
            q.pop();

            for (const string& X : symbols) {
                set<LR1Item> g = gotoItems(states_[i], X);
                if (g.empty()) continue;

                int j = findState(g);
                if (j < 0) {
                    states_.push_back(g);
                    j = static_cast<int>(states_.size() - 1);
                    q.push(j);
                }
                transitions_[{i, X}] = j;
            }
        }
        return true;
    }

    bool setActionWithCheck(int state, const string& terminal, const LRAction& action) {
        pair<int, string> key = {state, terminal};
        auto it = actionTable_.find(key);
        if (it == actionTable_.end()) {
            actionTable_[key] = action;
            return true;
        }

        if (it->second.type == action.type && it->second.value == action.value) {
            return true;
        }

        buildError_ = "LR(1) ACTION 冲突: state=" + to_string(state) +
                      ", terminal=" + terminal +
                      ", old=" + actionToString(it->second) +
                      ", new=" + actionToString(action);
        return false;
    }

    bool buildParsingTable() {
        for (size_t i = 0; i < states_.size(); ++i) {
            for (const LR1Item& item : states_[i]) {
                const Production& p = productions_[item.productionIndex];
                if (item.dotPos < static_cast<int>(p.rhs.size())) {
                    const string& a = p.rhs[item.dotPos];
                    auto tr = transitions_.find({static_cast<int>(i), a});
                    if (tr == transitions_.end()) continue;

                    if (terminals_.count(a)) {
                        if (!setActionWithCheck(static_cast<int>(i), a, {LRAction::Shift, tr->second})) {
                            return false;
                        }
                    } else if (nonTerminals_.count(a)) {
                        gotoTable_[{static_cast<int>(i), a}] = tr->second;
                    }
                } else {
                    if (p.lhs == startPrime_ && item.lookahead == "$") {
                        if (!setActionWithCheck(static_cast<int>(i), "$", {LRAction::Accept, 0})) {
                            return false;
                        }
                    } else {
                        if (!setActionWithCheck(static_cast<int>(i), item.lookahead, {LRAction::Reduce, item.productionIndex})) {
                            return false;
                        }
                    }
                }
            }
        }
        return true;
    }

    void computeSelectSets() {
        for (const auto& entry : actionTable_) {
            const string& terminal = entry.first.second;
            const LRAction& action = entry.second;
            if (action.type == LRAction::Reduce) {
                selectSets_[action.value].insert(terminal);
            }
        }
    }

    LRAction getAction(int state, const string& terminal) const {
        auto it = actionTable_.find({state, terminal});
        if (it == actionTable_.end()) return {};
        return it->second;
    }

    vector<string> expectedTerminals(int state) const {
        vector<string> res;
        for (const string& t : orderedTerminalsWithEnd_) {
            auto it = actionTable_.find({state, t});
            if (it != actionTable_.end() && it->second.type != LRAction::Error) {
                res.push_back(t);
            }
        }
        return res;
    }

    string productionToString(size_t productionIndex) const {
        const Production& p = productions_[productionIndex];
        ostringstream out;
        out << p.lhs << " -> ";
        if (p.rhs.empty()) {
            out << EPSILON;
        } else {
            for (size_t i = 0; i < p.rhs.size(); ++i) {
                if (i) out << " ";
                out << p.rhs[i];
            }
        }
        return out.str();
    }

    static string actionToString(const LRAction& action) {
        if (action.type == LRAction::Shift) return "s" + to_string(action.value);
        if (action.type == LRAction::Reduce) return "r" + to_string(action.value);
        if (action.type == LRAction::Accept) return "acc";
        return "err";
    }
};

bool tokenMatchesStop(const Token& token, const vector<string>& stopTokens) {
    if (stopTokens.empty()) return false;

    auto parseIndex = [](const string& text, int& out) -> bool {
        try {
            size_t pos = 0;
            int value = stoi(text, &pos);
            if (pos != text.size()) return false;
            out = value;
            return true;
        } catch (...) {
            return false;
        }
    };

    if (token.type == "KEYWORD") {
        int idx = -1;
        if (!parseIndex(token.value, idx)) return false;
        if (idx >= 0 && idx < static_cast<int>(ctx.keywordTable.size())) {
            const string& keyword = ctx.keywordTable[idx];
            return find(stopTokens.begin(), stopTokens.end(), keyword) != stopTokens.end();
        }
        return false;
    }

    if (token.type == "DELIMITER") {
        int idx = -1;
        if (!parseIndex(token.value, idx)) return false;
        if (idx >= 0 && idx < static_cast<int>(ctx.delimiterTable.size())) {
            const string& d = ctx.delimiterTable[idx];
            return find(stopTokens.begin(), stopTokens.end(), d) != stopTokens.end();
        }
        return false;
    }

    return false;
}

bool tokenToExpressionSymbol(const Token& token, string& symbol) {
    auto parseIndex = [](const string& text, int& out) -> bool {
        try {
            size_t pos = 0;
            int value = stoi(text, &pos);
            if (pos != text.size()) return false;
            out = value;
            return true;
        } catch (...) {
            return false;
        }
    };

    if (token.type == "ID") {
        symbol = "id";
        return true;
    }
    if (token.type == "CONSL1") {
        symbol = "int";
        return true;
    }
    if (token.type == "CONSL2") {
        symbol = "real";
        return true;
    }
    if (token.type == "STRING") {
        symbol = "string_lit";
        return true;
    }

    if (token.type == "KEYWORD") {
        int idx = -1;
        if (!parseIndex(token.value, idx)) return false;
        if (idx < 0 || idx >= static_cast<int>(ctx.keywordTable.size())) return false;
        const string& kw = ctx.keywordTable[idx];
        if (kw == "true" || kw == "false") {
            symbol = kw;
            return true;
        }
        return false;
    }

    if (token.type == "DELIMITER") {
        int idx = -1;
        if (!parseIndex(token.value, idx)) return false;
        if (idx < 0 || idx >= static_cast<int>(ctx.delimiterTable.size())) return false;
        const string& d = ctx.delimiterTable[idx];
        static const set<string> allowed = {
            "(", ")", ",", "+", "-", "*", "/", ">", ">=", "<", "<=", "=", "<>", "&&", "||", "!"
        };
        if (allowed.count(d)) {
            symbol = d;
            return true;
        }
    }

    return false;
}

} // namespace

// ============================================================
// 构造 & 顶层入口
// ============================================================

Parser::Parser(const vector<Token>& tokens)
    : tokens_(tokens)
    , pos_(0)
    , indent_(0)
    , hasError_(false)
    , expressionAnalysisPrinted_(false)
{
}

bool Parser::parse() {
    log_ = ostringstream();
    indent_ = 0;
    hasError_ = false;
    errorMsg_.clear();
    expressionAnalysisPrinted_ = false;

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
// §10 表达式（LR(1) 自动构建）
// ============================================================

bool Parser::parseExpression(const vector<string>& stopTokens) {
    enterRule("表达式");

    // 表达式文法固定不变：使用静态构建器复用 SELECT/ACTION/GOTO，避免重复建表开销。
    static ExpressionLR1Builder lr1Builder;
    if (!lr1Builder.isBuildOk()) {
        error("表达式 LR(1) 自动构建失败: " + lr1Builder.buildError());
        exitRule("表达式", false);
        return false;
    }

    if (!expressionAnalysisPrinted_) {
        logInfo("自动构建表达式 SELECT 集和 LR(1) 分析表如下：");
        istringstream iss(lr1Builder.dumpSelectAndTable());
        string line;
        while (getline(iss, line)) {
            logInfo(line);
        }
        expressionAnalysisPrinted_ = true;
    }

    size_t beginPos = pos_;
    size_t scanPos = pos_;
    int parenDepth = 0;

    while (scanPos < tokens_.size()) {
        const Token& t = tokens_[scanPos];

        if (parenDepth == 0 && tokenMatchesStop(t, stopTokens)) {
            break;
        }

        if (t.type == "DELIMITER") {
            int idx = -1;
            bool isIndexValid = false;
            try {
                size_t p = 0;
                idx = stoi(t.value, &p);
                isIndexValid = (p == t.value.size());
            } catch (...) {
                isIndexValid = false;
            }

            if (isIndexValid && idx >= 0 && idx < static_cast<int>(ctx.delimiterTable.size())) {
                const string& d = ctx.delimiterTable[idx];
                if (d == "(") {
                    parenDepth++;
                } else if (d == ")") {
                    if (parenDepth > 0) {
                        parenDepth--;
                    } else {
                        // 非平衡右括号交由外层规则处理
                        break;
                    }
                }
            }
        }

        scanPos++;
    }

    if (scanPos == beginPos) {
        error("缺少表达式");
        exitRule("表达式", false);
        return false;
    }

    vector<string> symbols;
    symbols.reserve(scanPos - beginPos);

    for (size_t i = beginPos; i < scanPos; ++i) {
        const Token& t = tokens_[i];
        string sym;
        if (!tokenToExpressionSymbol(t, sym)) {
            error("表达式中出现非法记号: " + tokenToString(t));
            exitRule("表达式", false);
            return false;
        }
        symbols.push_back(sym);
    }

    string lrError;
    if (!lr1Builder.parse(symbols, lrError)) {
        error("LR(1) 表达式分析失败: " + lrError);
        exitRule("表达式", false);
        return false;
    }

    while (pos_ < scanPos) {
        logMatch(advance());
    }

    logInfo("表达式解析（LR(1) 自动分析）");
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

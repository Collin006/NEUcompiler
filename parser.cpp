#include "parser.h"
#include <stdexcept>
#include <algorithm>
#include <fstream>
#include <set>
#include <map>
#include <queue>
#include <unordered_set>
#include <functional>

namespace {

const string EPSILON = "ε";
const int INVALID_TYPE_INDEX = -1;

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

// ============================================================
// 语义值 —— 表达式分析过程中传递的中间结果
// Shift 时记录终结符的原文和行号
// Reduce 时由回调生成非终结符的语义值
// 语义分析/代码生成阶段：替换 ReduceCallback 为四元式生成逻辑即可
// ============================================================
struct SemanticValue {
    string symbol;   // 文法符号名（终结符如 "id"/"+", 非终结符如 "Expr"）
    string text;     // 终结符的原文（标识符名/常量值），非终结符由规约生成
    int    line;     // 所在行号
    int    typ = INVALID_TYPE_INDEX;
    bool isCall = false;
    vector<string> args;
};

int ensureBuiltinTypeShared(const string& tval) {
    for (int i = 0; i < static_cast<int>(ctx.typel.size()); ++i) {
        if (ctx.typel[i].tval == tval) return i;
    }
    TypelItem item;
    item.tval = tval;
    item.tpoint = -1;
    ctx.typel.push_back(item);
    LenlItem len;
    len.length = (tval == "r") ? 2 : 1;
    ctx.lenl.push_back(len);
    return static_cast<int>(ctx.typel.size()) - 1;
}

// 规约回调：产生式编号、LHS、RHS 值列表 → 规约结果
// 传入 nullptr 时使用默认行为（仅透传符号名，不做语义分析）
using ReduceCallback = std::function<SemanticValue(
    int prodIndex, const std::string& lhs, const std::vector<SemanticValue>& rhs)>;

// 文法符号名 → 中文翻译（用于日志输出）
string symbolToChinese(const string& sym) {
    static const map<string, string> trans = {
        {"Expr", "表达式"}, {"BoolExpr", "布尔表达式"}, {"BoolTerm", "布尔项"},
        {"BoolFactor", "布尔因子"}, {"RelExpr", "关系表达式"}, {"RelOp", "关系运算符"},
        {"ArithExpr", "算术表达式"}, {"Term", "项"}, {"Factor", "因子"},
        {"CallSuffix", "调用后缀"}, {"ActualParamList", "实参表"},
        {"ActualParamListTail", "实参表尾"},
        {"id", "标识符"}, {"int", "整数"}, {"real", "实数"},
        {"string_lit", "字符串"}, {"true", "true"}, {"false", "false"},
        {"ε", "ε"},
    };
    auto it = trans.find(sym);
    return it != trans.end() ? it->second : sym;
}

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

    // 表达式语法分析（纯语法校验，保留旧接口兼容）
    bool parse(const vector<string>& inputSymbols, string& errorMsg) const {
        vector<SemanticValue> dummyValues;
        dummyValues.reserve(inputSymbols.size());
        for (const string& sym : inputSymbols) {
            SemanticValue v;
            v.symbol = sym;
            v.line = 0;
            dummyValues.push_back(v);
        }
        return parse(dummyValues, errorMsg, nullptr);
    }

    // 表达式语法分析 + 语义值传递
    // onReduce: 每次规约时调用，传入产生式编号、LHS、RHS 值列表，返回规约结果
    //           传 nullptr 则使用默认行为（仅透传符号名）
    // lrLog:    非空时输出每次 Shift/Reduce/Accept 的详细日志
    bool parse(const vector<SemanticValue>& inputValues, string& errorMsg,
               const ReduceCallback& onReduce, ostream* lrLog = nullptr,
               SemanticValue* acceptValue = nullptr) const {
        vector<int> stateStack;
        vector<SemanticValue> valueStack;  // 语义值栈，与状态栈同步
        stateStack.push_back(0);
        // 初始压入空值占位（状态栈从 0 开始，值栈对齐）
        SemanticValue initVal;
        initVal.symbol = "$";
        valueStack.push_back(initVal);
        size_t ip = 0;

        // 默认回调：仅记录符号名，不做语义分析
        auto defaultReduce = [](int /*prodIndex*/, const string& lhs,
                                const vector<SemanticValue>& /*rhs*/) -> SemanticValue {
            SemanticValue result;
            result.symbol = lhs;
            return result;
        };

        const ReduceCallback& reduce = onReduce ? onReduce : defaultReduce;

        while (true) {
            int state = stateStack.back();
            string lookahead = (ip < inputValues.size()) ? inputValues[ip].symbol : "$";
            LRAction action = getAction(state, lookahead);

            if (action.type == LRAction::Shift) {
                stateStack.push_back(action.value);
                if (lrLog) {
                    const SemanticValue& val = inputValues[ip];
                    *lrLog << "  [LR] Shift  " << symbolToChinese(val.symbol);
                    if (!val.text.empty() && val.text != val.symbol)
                        *lrLog << " (\"" << val.text << "\")";
                    *lrLog << "  → state " << action.value << "\n";
                }
                valueStack.push_back(inputValues[ip]);  // 语义值入栈
                ip++;
                continue;
            }

            if (action.type == LRAction::Reduce) {
                const Production& p = productions_[action.value];

                // 从语义值栈弹出 |RHS| 个值
                vector<SemanticValue> rhsValues;
                rhsValues.reserve(p.rhs.size());
                for (size_t i = 0; i < p.rhs.size(); ++i) {
                    if (stateStack.empty() || valueStack.size() <= 1) {
                        errorMsg = "LR 栈下溢（规约阶段）";
                        return false;
                    }
                    stateStack.pop_back();
                    rhsValues.push_back(valueStack.back());
                    valueStack.pop_back();
                }
                // RHS 是倒序弹出的，翻转恢复原始顺序
                reverse(rhsValues.begin(), rhsValues.end());

                if (stateStack.empty()) {
                    errorMsg = "LR 栈为空，无法 GOTO";
                    return false;
                }

                /* SEMANTIC: 规约回调 —— 在此生成四元式
                   调用 reduce(prodIndex, p.lhs, rhsValues)
                   返回的 SemanticValue 压入值栈
                   例如：P17 (ArithExpr → ArithExpr + Term)
                     rhsValues = [ArithExpr(a), +, Term(b)]
                     reduce 应返回 ArithExpr(t1)，并生成四元式 (+, a, b, t1)
                */
                if (lrLog) {
                    *lrLog << "  [LR] Reduce P" << action.value << ": "
                           << symbolToChinese(p.lhs) << " ←";
                    for (const string& sym : p.rhs)
                        *lrLog << " " << symbolToChinese(sym);
                    *lrLog << "\n";
                }
                SemanticValue result = reduce(action.value, p.lhs, rhsValues);
                valueStack.push_back(result);

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
                if (ip != inputValues.size()) {
                    errorMsg = "表达式未完全消耗";
                    return false;
                }
                if (lrLog) *lrLog << "  [LR] Accept  (表达式分析成功)\n";
                // 顶层规约结果的语义值在 valueStack.top()
                if (acceptValue && !valueStack.empty()) {
                    *acceptValue = valueStack.back();
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

const ExpressionLR1Builder& expressionLR1Builder() {
    static ExpressionLR1Builder builder;
    return builder;
}

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

// 将 Token 转换为表达式分析器的语义值（符号名 + 原文 + 行号）
// 返回 false 表示该 token 不能出现在表达式中
bool tokenToExpressionValue(const Token& token, SemanticValue& val) {
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

    val.line = token.line;

    if (token.type == "ID") {
        val.symbol = "id";
        int idx = -1;
        if (parseIndex(token.value, idx) && idx >= 0 && idx < static_cast<int>(ctx.synbl.size())) {
            val.text = ctx.synbl[idx].name;
            val.typ = ctx.synbl[idx].typ;
        } else
            val.text = "?";
        return true;
    }
    if (token.type == "CONSL1") {
        val.symbol = "int";
        int idx = -1;
        if (parseIndex(token.value, idx) && idx >= 0 && idx < static_cast<int>(ctx.consl1.size()))
            val.text = to_string(ctx.consl1[idx]);
        else
            val.text = "?";
        val.typ = ensureBuiltinTypeShared("i");
        return true;
    }
    if (token.type == "CONSL2") {
        val.symbol = "real";
        int idx = -1;
        if (parseIndex(token.value, idx) && idx >= 0 && idx < static_cast<int>(ctx.consl2.size()))
            val.text = to_string(ctx.consl2[idx]);
        else
            val.text = "?";
        val.typ = ensureBuiltinTypeShared("r");
        return true;
    }
    if (token.type == "STRING") {
        val.symbol = "string_lit";
        val.text = token.value;  // 原文（不含两端引号）
        val.typ = ensureBuiltinTypeShared("s");
        return true;
    }

    if (token.type == "KEYWORD") {
        int idx = -1;
        if (!parseIndex(token.value, idx)) return false;
        if (idx < 0 || idx >= static_cast<int>(ctx.keywordTable.size())) return false;
        const string& kw = ctx.keywordTable[idx];
        if (kw == "true" || kw == "false") {
            val.symbol = kw;
            val.text = kw;
            val.typ = ensureBuiltinTypeShared("b");
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
            val.symbol = d;
            val.text = d;
            return true;
        }
    }

    return false;
}

// 旧接口保留兼容（内部调用 tokenToExpressionValue）
bool tokenToExpressionSymbol(const Token& token, string& symbol) {
    SemanticValue val;
    if (!tokenToExpressionValue(token, val)) return false;
    symbol = val.symbol;
    return true;
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
{
}

bool Parser::parse() {
    log_ = ostringstream();
    indent_ = 0;
    hasError_ = false;
    errorMsg_.clear();
    quadruples_.clear();
    pendingIdentifiers_.clear();
    pendingActualArgs_.clear();
    scopeLevel_ = 0;
    tempCounter_ = 0;
    currentRoutineSymbolIndex_ = -1;
    currentRoutineParamCount_ = 0;
    lastParsedTypeIndex_ = INVALID_TYPE_INDEX;
    lastParsedTypeCode_.clear();
    lastExpressionPlace_.clear();
    lastStatementIdentifier_.clear();

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
        logInfo(getSymbolTableDump());
        logInfo(getQuadrupleDump());
        logInfo("===== 语法分析通过 =====");
    } else {
        logInfo("===== 语法分析失败 =====");
    }

    return ok && !hasError_;
}

bool Parser::getExpressionAnalysisDump(string& dump, string& error) {
    const ExpressionLR1Builder& lr1Builder = expressionLR1Builder();
    if (!lr1Builder.isBuildOk()) {
        error = lr1Builder.buildError();
        dump.clear();
        return false;
    }

    dump = lr1Builder.dumpSelectAndTable();
    error.clear();
    return true;
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

string Parser::getQuadrupleDump() const {
    ostringstream out;
    out << "===== 四元式 =====\n";
    for (size_t i = 0; i < quadruples_.size(); ++i) {
        const Quadruple& q = quadruples_[i];
        out << i << ": (" << q.op << ", " << q.arg1 << ", "
            << q.arg2 << ", " << q.result << ")\n";
    }
    if (quadruples_.empty()) {
        out << "(empty)\n";
    }
    return out.str();
}

string Parser::getSymbolTableDump() const {
    ostringstream out;
    out << "===== 符号表(SYNBL) =====\n";
    out << "idx\tname\ttyp\tcat\taddr\n";
    for (size_t i = 0; i < ctx.synbl.size(); ++i) {
        const SynblItem& s = ctx.synbl[i];
        out << i << '\t' << s.name << '\t' << s.typ << '\t'
            << s.cat << '\t' << s.addr << '\n';
    }
    if (ctx.synbl.empty()) out << "(empty)\n";

    out << "===== 类型表(TYPEL) =====\n";
    out << "idx\ttval\ttpoint\n";
    for (size_t i = 0; i < ctx.typel.size(); ++i) {
        const TypelItem& t = ctx.typel[i];
        out << i << '\t' << t.tval << '\t' << t.tpoint << '\n';
    }
    if (ctx.typel.empty()) out << "(empty)\n";

    out << "===== 过程/函数信息表(PFINFL) =====\n";
    out << "idx\tlevel\toff\tfn\tentry\tparam\n";
    for (size_t i = 0; i < ctx.pfinfl.size(); ++i) {
        const PfinflItem& p = ctx.pfinfl[i];
        out << i << '\t' << p.level << '\t' << p.off << '\t'
            << p.fn << '\t' << p.entry << '\t' << p.param << '\n';
    }
    if (ctx.pfinfl.empty()) out << "(empty)\n";
    return out.str();
}

int Parser::currentIdIndex() const {
    if (!checkId()) return -1;
    try {
        return stoi(current().value);
    } catch (...) {
        return -1;
    }
}

int Parser::ensureBuiltinType(const string& tval) {
    return ensureBuiltinTypeShared(tval);
}

string Parser::newTemp(int typ) {
    string name = "_t" + to_string(++tempCounter_);
    SynblItem item;
    item.name = name;
    item.typ = typ;
    item.cat = "v";
    item.addr = scopeLevel_;
    ctx.synbl.push_back(item);
    return name;
}

int Parser::emitQuad(const string& op, const string& arg1, const string& arg2, const string& result) {
    quadruples_.push_back({op, arg1, arg2, result});
    return static_cast<int>(quadruples_.size()) - 1;
}

void Parser::backpatchQuadResult(int quadIndex, int target) {
    if (quadIndex < 0 || quadIndex >= static_cast<int>(quadruples_.size())) return;
    quadruples_[quadIndex].result = to_string(target);
}

void Parser::declarePendingIdentifiers(const string& cat, int typ) {
    for (int idx : pendingIdentifiers_) {
        if (idx < 0 || idx >= static_cast<int>(ctx.synbl.size())) continue;
        ctx.synbl[idx].typ = typ;
        ctx.synbl[idx].cat = cat;
        ctx.synbl[idx].addr = scopeLevel_;
    }
    pendingIdentifiers_.clear();
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
    int programIdx = currentIdIndex();

    if (!matchId()) {
        error("缺少程序名（标识符）");
        exitRule("程序", false);
        return false;
    }
    if (programIdx >= 0 && programIdx < static_cast<int>(ctx.synbl.size())) {
        ctx.synbl[programIdx].cat = "p";
        ctx.synbl[programIdx].addr = 0;
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
    scopeLevel_++;

    if (!parseDeclarationPart()) {
        exitRule("分程序", false);
        return false;
    }

    if (!parseCompoundStatement()) {
        exitRule("分程序", false);
        return false;
    }

    /* SEMANTIC: 退出作用域层级 */
    if (scopeLevel_ > 0) scopeLevel_--;

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

    pendingIdentifiers_.clear();

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
    declarePendingIdentifiers("v", lastParsedTypeIndex_);

    exitRule("变量定义", true);
    return true;
}

bool Parser::parseIdentifierList() {
    enterRule("标识符表");

    int firstId = currentIdIndex();

    if (!matchId()) {
        error("缺少标识符");
        exitRule("标识符表", false);
        return false;
    }

    /* SEMANTIC: 收集标识符 */
    if (firstId >= 0) pendingIdentifiers_.push_back(firstId);

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
        int idIndex = currentIdIndex();
        if (!matchId()) {
            error("',' 后缺少标识符");
            exitRule("标识符表尾", false);
            return false;
        }
        /* SEMANTIC: 收集标识符 */
        if (idIndex >= 0) pendingIdentifiers_.push_back(idIndex);
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

    int funcIdx = currentIdIndex();

    if (!matchId()) {
        error("缺少函数名");
        exitRule("函数说明", false);
        return false;
    }

    /* SEMANTIC: 函数名入符号表，cat = 'f' */
    currentRoutineSymbolIndex_ = funcIdx;
    currentRoutineParamCount_ = 0;
    if (funcIdx >= 0 && funcIdx < static_cast<int>(ctx.synbl.size())) {
        ctx.synbl[funcIdx].cat = "f";
        ctx.synbl[funcIdx].addr = scopeLevel_;
    }

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
    if (currentRoutineSymbolIndex_ >= 0 && currentRoutineSymbolIndex_ < static_cast<int>(ctx.synbl.size())) {
        ctx.synbl[currentRoutineSymbolIndex_].typ = lastParsedTypeIndex_;
    }

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
    currentRoutineSymbolIndex_ = -1;

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
    if (currentRoutineSymbolIndex_ >= 0) {
        PfinflItem item{};
        item.level = scopeLevel_;
        item.off = 0;
        item.fn = currentRoutineParamCount_;
        item.entry = -1;
        item.param = -1;
        ctx.pfinfl.push_back(item);
    }

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

    pendingIdentifiers_.clear();

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
    int paramCount = static_cast<int>(pendingIdentifiers_.size());
    declarePendingIdentifiers("vf", lastParsedTypeIndex_);
    currentRoutineParamCount_ += paramCount;

    exitRule("值参数", true);
    return true;
}

bool Parser::parseVarParameter() {
    enterRule("变量参数");

    pendingIdentifiers_.clear();

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
    int paramCount = static_cast<int>(pendingIdentifiers_.size());
    declarePendingIdentifiers("vn", lastParsedTypeIndex_);
    currentRoutineParamCount_ += paramCount;

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

    int procIdx = currentIdIndex();

    if (!matchId()) {
        error("缺少过程名");
        exitRule("过程说明", false);
        return false;
    }

    /* SEMANTIC: 过程名入符号表 */
    currentRoutineSymbolIndex_ = procIdx;
    currentRoutineParamCount_ = 0;
    if (procIdx >= 0 && procIdx < static_cast<int>(ctx.synbl.size())) {
        ctx.synbl[procIdx].cat = "p";
        ctx.synbl[procIdx].addr = scopeLevel_;
    }

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

    currentRoutineSymbolIndex_ = -1;

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
    int jfalseIndex = emitQuad("jfalse", lastExpressionPlace_, "", "?");

    // then 分支
    if (!parseStatement()) {
        exitRule("if语句", false);
        return false;
    }

    /* SEMANTIC: 回填真出口 / 生成无条件跳转（跳过 else） */
    int jmpOverElse = -1;

    // 可选的 else 分支
    if (matchKeyword("else")) {
        /* SEMANTIC: 处理 else 前的跳转 */
        jmpOverElse = emitQuad("j", "", "", "?");
        backpatchQuadResult(jfalseIndex, static_cast<int>(quadruples_.size()));

        if (!parseStatement()) {
            exitRule("if语句", false);
            return false;
        }

        /* SEMANTIC: 回填假出口 */
        backpatchQuadResult(jmpOverElse, static_cast<int>(quadruples_.size()));
    } else {
        /* SEMANTIC: 回填假出口到当前位置 */
        backpatchQuadResult(jfalseIndex, static_cast<int>(quadruples_.size()));
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
    int loopBegin = static_cast<int>(quadruples_.size());

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
    int jfalseIndex = emitQuad("jfalse", lastExpressionPlace_, "", "?");

    if (!parseStatement()) {
        exitRule("while语句", false);
        return false;
    }

    /* SEMANTIC: 生成无条件跳转回循环头 + 回填假出口 */
    emitQuad("j", "", "", to_string(loopBegin));
    backpatchQuadResult(jfalseIndex, static_cast<int>(quadruples_.size()));

    exitRule("while语句", true);
    return true;
}

bool Parser::parseAssignOrCallStatement() {
    enterRule("赋值或调用语句");

    string idName = tokenToString(current());

    if (!matchId()) {
        error("缺少标识符");
        exitRule("赋值或调用语句", false);
        return false;
    }

    /* SEMANTIC: 查符号表获取该标识符信息 */
    lastStatementIdentifier_ = idName;

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
        emitQuad(":=", lastExpressionPlace_, "", lastStatementIdentifier_);
    } else {
        // 过程调用（含无参调用）
        if (!parseCallSuffix()) {
            exitRule("赋值或调用语句", false);
            return false;
        }

        /* SEMANTIC: 生成过程调用四元式（或函数调用丢弃返回值） */
        for (const string& arg : pendingActualArgs_) {
            emitQuad("param", arg, "", "");
        }
        emitQuad("call", lastStatementIdentifier_, to_string(pendingActualArgs_.size()), "");
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
    pendingActualArgs_.clear();

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
        pendingActualArgs_.push_back(lastExpressionPlace_);

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
        pendingActualArgs_.push_back(lastExpressionPlace_);
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
    const ExpressionLR1Builder& lr1Builder = expressionLR1Builder();
    if (!lr1Builder.isBuildOk()) {
        error("表达式 LR(1) 自动构建失败: " + lr1Builder.buildError());
        exitRule("表达式", false);
        return false;
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

    // 收集表达式的语义值序列（每个 token 映射为语义值，保留原文和行号）
    vector<SemanticValue> exprValues;
    exprValues.reserve(scanPos - beginPos);

    for (size_t i = beginPos; i < scanPos; ++i) {
        const Token& t = tokens_[i];
        SemanticValue val;
        if (!tokenToExpressionValue(t, val)) {
            error("表达式中出现非法记号: " + tokenToString(t));
            exitRule("表达式", false);
            return false;
        }
        exprValues.push_back(val);
    }

    auto reduce = [this](int prodIndex, const string& lhs, const vector<SemanticValue>& rhs) -> SemanticValue {
        SemanticValue result;
        result.symbol = lhs;
        if (!rhs.empty()) result.line = rhs.front().line;

        auto passThrough = [&](size_t idx) {
            if (idx < rhs.size()) {
                result.text = rhs[idx].text;
                result.isCall = rhs[idx].isCall;
                result.args = rhs[idx].args;
                result.line = rhs[idx].line;
                result.typ = rhs[idx].typ;
            }
        };

        switch (prodIndex) {
            // 产生式编号见 ExpressionLR1Builder::initGrammar()
            // 1/3/5/7/9/18/21/23~28: 语义透传
            case 1: case 3: case 5: case 7: case 9:
            case 18: case 21: case 23: case 24: case 25:
            case 26: case 27: case 28:
                passThrough(0);
                break;
            case 2: {
                int boolTyp = ensureBuiltinType("b");
                string t = newTemp(boolTyp);
                emitQuad("||", rhs[0].text, rhs[2].text, t);
                result.text = t;
                result.typ = boolTyp;
                break;
            }
            case 4: {
                int boolTyp = ensureBuiltinType("b");
                string t = newTemp(boolTyp);
                emitQuad("&&", rhs[0].text, rhs[2].text, t);
                result.text = t;
                result.typ = boolTyp;
                break;
            }
            case 6: {
                int boolTyp = ensureBuiltinType("b");
                string t = newTemp(boolTyp);
                emitQuad("!", rhs[1].text, "", t);
                result.text = t;
                result.typ = boolTyp;
                break;
            }
            case 8: {
                string op = rhs[1].text;
                int boolTyp = ensureBuiltinType("b");
                string t = newTemp(boolTyp);
                emitQuad(op, rhs[0].text, rhs[2].text, t);
                result.text = t;
                result.typ = boolTyp;
                break;
            }
            case 10: case 11: case 12: case 13: case 14: case 15:
                passThrough(0);
                break;
            case 16: {
                int realTyp = ensureBuiltinType("r");
                int resultTyp = (rhs[0].typ == realTyp || rhs[2].typ == realTyp)
                                    ? realTyp
                                    : rhs[0].typ;
                string t = newTemp(resultTyp);
                emitQuad("+", rhs[0].text, rhs[2].text, t);
                result.text = t;
                result.typ = resultTyp;
                break;
            }
            case 17: {
                int realTyp = ensureBuiltinType("r");
                int resultTyp = (rhs[0].typ == realTyp || rhs[2].typ == realTyp)
                                    ? realTyp
                                    : rhs[0].typ;
                string t = newTemp(resultTyp);
                emitQuad("-", rhs[0].text, rhs[2].text, t);
                result.text = t;
                result.typ = resultTyp;
                break;
            }
            case 19: {
                int realTyp = ensureBuiltinType("r");
                int resultTyp = (rhs[0].typ == realTyp || rhs[2].typ == realTyp)
                                    ? realTyp
                                    : rhs[0].typ;
                string t = newTemp(resultTyp);
                emitQuad("*", rhs[0].text, rhs[2].text, t);
                result.text = t;
                result.typ = resultTyp;
                break;
            }
            case 20: {
                int realTyp = ensureBuiltinType("r");
                int resultTyp = (rhs[0].typ == realTyp || rhs[2].typ == realTyp)
                                    ? realTyp
                                    : rhs[0].typ;
                string t = newTemp(resultTyp);
                emitQuad("/", rhs[0].text, rhs[2].text, t);
                result.text = t;
                result.typ = resultTyp;
                break;
            }
            case 22: {
                if (rhs[1].isCall) {
                    for (const string& arg : rhs[1].args) {
                        emitQuad("param", arg, "", "");
                    }
                    int callResultTyp = rhs[0].typ;
                    if (callResultTyp < 0) callResultTyp = ensureBuiltinType("i");
                    string t = newTemp(callResultTyp);
                    emitQuad("call", rhs[0].text, to_string(rhs[1].args.size()), t);
                    result.text = t;
                    result.typ = callResultTyp;
                } else {
                    result.text = rhs[0].text;
                    result.typ = rhs[0].typ;
                }
                break;
            }
            case 29: {
                int intTyp = ensureBuiltinType("i");
                int realTyp = ensureBuiltinType("r");
                int unaryTyp = rhs[1].typ;
                if (unaryTyp != intTyp && unaryTyp != realTyp) unaryTyp = intTyp;
                string t = newTemp(unaryTyp);
                emitQuad("uminus", rhs[1].text, "", t);
                result.text = t;
                result.typ = unaryTyp;
                break;
            }
            case 30:
                result.isCall = true;
                result.args = rhs[1].args;
                break;
            case 31:
                result.isCall = false;
                break;
            case 32:
                result.args.push_back(rhs[0].text);
                result.args.insert(result.args.end(), rhs[1].args.begin(), rhs[1].args.end());
                break;
            case 33:
                result.args.clear();
                break;
            case 34:
                result.args.push_back(rhs[1].text);
                result.args.insert(result.args.end(), rhs[2].args.begin(), rhs[2].args.end());
                break;
            case 35:
                result.args.clear();
                break;
            default:
                passThrough(0);
                break;
        }
        return result;
    };

    string lrError;
    SemanticValue acceptValue;
    // 传入规约回调：在表达式规约时完成四元式生成
    if (!lr1Builder.parse(exprValues, lrError, reduce, &log_, &acceptValue)) {
        error("LR(1) 表达式分析失败: " + lrError);
        exitRule("表达式", false);
        return false;
    }

    lastExpressionPlace_ = acceptValue.text;

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

    if (matchKeyword("integer")) {
        lastParsedTypeCode_ = "i";
        lastParsedTypeIndex_ = ensureBuiltinType(lastParsedTypeCode_);
        /* SEMANTIC: 返回类型编码（tval） */
        exitRule("类型", true);
        return true;
    }
    if (matchKeyword("real")) {
        lastParsedTypeCode_ = "r";
        lastParsedTypeIndex_ = ensureBuiltinType(lastParsedTypeCode_);
        /* SEMANTIC: 返回类型编码（tval） */
        exitRule("类型", true);
        return true;
    }
    if (matchKeyword("char")) {
        lastParsedTypeCode_ = "c";
        lastParsedTypeIndex_ = ensureBuiltinType(lastParsedTypeCode_);
        /* SEMANTIC: 返回类型编码（tval） */
        exitRule("类型", true);
        return true;
    }
    if (matchKeyword("boolean")) {
        lastParsedTypeCode_ = "b";
        lastParsedTypeIndex_ = ensureBuiltinType(lastParsedTypeCode_);
        /* SEMANTIC: 返回类型编码（tval） */
        exitRule("类型", true);
        return true;
    }
    if (matchKeyword("string")) {
        lastParsedTypeCode_ = "s";
        lastParsedTypeIndex_ = ensureBuiltinType(lastParsedTypeCode_);
        /* SEMANTIC: 返回类型编码（tval） */
        exitRule("类型", true);
        return true;
    }

    error("缺少类型（integer / real / char / boolean / string）");
    exitRule("类型", false);
    return false;
}

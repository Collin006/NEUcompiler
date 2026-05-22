// ============================================================
// Parser 实现文件
//
// 核心职责：
//   1. LR(1) 分析表的自动构建（FIRST集、标准族、ACTION/GOTO表）
//   2. 表达式语法分析（纯语法校验）
//   3. 递归下降语法分析器（处理程序、声明、语句等）
//   4. 语义动作（符号表填表、四元式生成）
//   5. 错误恢复与详细日志输出
// ============================================================
#include "parser.h"
#include <stdexcept>
#include <algorithm>
#include <fstream>
#include <set>
#include <map>
#include <queue>
#include <unordered_set>
#include <functional>
#include <iomanip>

namespace {

// ============================================================
// 全局常量
// ============================================================
const string EPSILON = "ε";              // 空产生式标记
const int INVALID_TYPE_INDEX = -1;       // 无效类型索引标记

// ============================================================
// 产生式结构体
// ============================================================
struct Production {
    string lhs;                           // 左侧非终结符
    vector<string> rhs;                   // 右侧符号序列（可能为空表示 ε）
};

// ============================================================
// LR(1) 项结构体
// ============================================================
struct LR1Item {
    int productionIndex;                  // 产生式编号
    int dotPos;                           // 圆点位置（0 到 RHS 长度）
    string lookahead;                     // 前瞻符号

    // 相等性判断：三个字段都相等才认为项相同
    bool operator==(const LR1Item& other) const {
        return productionIndex == other.productionIndex &&
               dotPos == other.dotPos &&
               lookahead == other.lookahead;
    }

    // 偏序关系（用于 set 容器自动排序）
    bool operator<(const LR1Item& other) const {
        if (productionIndex != other.productionIndex) return productionIndex < other.productionIndex;
        if (dotPos != other.dotPos) return dotPos < other.dotPos;
        return lookahead < other.lookahead;
    }
};

// ============================================================
// LR(1) 动作结构体
// ============================================================
struct LRAction {
    enum Type { Error, Shift, Reduce, Accept } type = Error;
    int value = -1;                       // Shift 时为状态号，Reduce 时为产生式编号
};

// ============================================================
// 语义值结构体 —— 表达式分析过程中传递的中间结果
//
// Shift 时：从终结符 token 创建，记录源文本和行号
// Reduce 时：由规约回调生成，记录规约结果的中间表示（如临时变量名）
//
// 使用 typedef ReduceCallback 传入回调来实现语义动作
// ============================================================
struct SemanticValue {
    string symbol;                        // 文法符号名（终结符如 "id"、"+", 非终结符如 "Expr"）
    string text;                          // 终结符的原文（标识符名、常量值等），非终结符由规约生成得出
    int    line;                          // 所在源代码行号
    int    typ = INVALID_TYPE_INDEX;      // 类型索引（用于类型检查）
    bool isCall = false;                  // 标记是否为函数调用
    vector<string> args;                  // 若为函数调用，存储实参列表
};

//  确保内置类型在 TYPEL 表中存在（若不存在则插入）
// 参数：tval - 类型值字符串（如 "i"、"r" 等）
// 返回：类型在 TYPEL 中的索引
int ensureBuiltinTypeShared(const string& tval) {
    for (int i = 0; i < static_cast<int>(ctx.typel.size()); ++i) {
        if (ctx.typel[i].tval == tval) return i;
    }
    // 不存在则新增
    TypelItem item;
    item.tval = tval;
    item.tpoint = -1;
    ctx.typel.push_back(item);
    LenlItem len;
    len.length = (tval == "r") ? 2 : 1;
    ctx.lenl.push_back(len);
    return static_cast<int>(ctx.typel.size()) - 1;
}

// ============================================================
// 规约回调函数类型定义
//
// 参数：
//   - prodIndex：产生式编号
//   - lhs：左侧非终结符名
//   - rhs：右侧各符号的语义值列表（按顺序）
// 返回：规约结果的语义值
//
// 传入 nullptr 时使用默认行为（仅透传符号名，不做语义分析）
// ============================================================
using ReduceCallback = std::function<SemanticValue(
    int prodIndex, const std::string& lhs, const std::vector<SemanticValue>& rhs)>;

// ============================================================
// 文法符号名 → 中文翻译（用于日志输出，便于调试）
// ============================================================
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

// ============================================================
// ExpressionLR1Builder —— 表达式文法的 LR(1) 分析器构建类
//
// 职责：
//   1. 初始化表达式文法（包括算术、关系、逻辑运算等）
//   2. 自动计算 FIRST 集合
//   3. 构建 LR(1) 标准族（所有可达状态）
//   4. 生成 ACTION 和 GOTO 分析表
//   5. 完成表达式语法分析（带或不带语义动作）
// ============================================================
class ExpressionLR1Builder {
public:
    // 构造函数：一次性完成所有初始化和表格构建
    ExpressionLR1Builder() {
        initGrammar();
        // 依次执行：初始化文法 → 计算FIRST集 → 构建标准族 → 生成分析表 → 计算SELECT集
        buildOk_ = computeFirstSets() && buildCanonicalCollection() && buildParsingTable();
        if (buildOk_) {
            computeSelectSets();
        }
    }

    // 查询构建是否成功
    bool isBuildOk() const { return buildOk_; }

    // 获取构建失败时的错误信息
    string buildError() const { return buildError_; }

    // ========== 纯语法分析接口（无语义动作） ==========
    //
    // 参数：inputSymbols - 输入符号序列（通常来自词法分析）
    // 输出：errorMsg - 若分析失败，填充错误消息
    // 返回：true 表示符合文法，false 表示有语法错误
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

    // ========== 表达式语法分析 + 语义值传递 ==========
    //
    // 参数：
    //   inputValues   - 输入的语义值序列（每个元素包含符号和原文）
    //   errorMsg      - 分析失败时的错误消息
    //   onReduce      - 每次规约时的回调（可为 nullptr 使用默认行为）
    //   lrLog         - 若提供，输出详细的 Shift/Reduce/Accept 日志
    //   acceptValue   - 若提供，保存最终的接受状态的语义值
    //
    // 返回：true 表示分析成功，false 表示有语法错误
    bool parse(const vector<SemanticValue>& inputValues, string& errorMsg,
               const ReduceCallback& onReduce, ostream* lrLog = nullptr,
               SemanticValue* acceptValue = nullptr) const {
        // ========== 初始化栈式 LR 分析机 ==========
        vector<int> stateStack;
        vector<SemanticValue> valueStack;      // 语义值栈，与状态栈 1:1 对应
        stateStack.push_back(0);                // 初始状态为 0

        // 初始压入一个虚拟占位值，使状态栈和值栈对齐
        SemanticValue initVal;
        initVal.symbol = "$";
        valueStack.push_back(initVal);
        size_t ip = 0;                          // 当前输入指针

        // 默认规约回调：仅透传符号名，无语义分析
        auto defaultReduce = [](int /*prodIndex*/, const string& lhs,
                                const vector<SemanticValue>& /*rhs*/) -> SemanticValue {
            SemanticValue result;
            result.symbol = lhs;
            return result;
        };

        const ReduceCallback& reduce = onReduce ? onReduce : defaultReduce;

        // ========== 主分析循环 ==========
        while (true) {
            int state = stateStack.back();                          // 当前状态
            string lookahead = (ip < inputValues.size()) ? inputValues[ip].symbol : "$";  // 前瞻符号或 EOF
            LRAction action = getAction(state, lookahead);           // 查表获得下一步动作

            // ========== SHIFT 动作 ==========
            if (action.type == LRAction::Shift) {
                stateStack.push_back(action.value);                  // 压入新状态
                if (lrLog) {
                    const SemanticValue& val = inputValues[ip];
                    *lrLog << "  [LR] Shift  " << symbolToChinese(val.symbol);
                    if (!val.text.empty() && val.text != val.symbol)
                        *lrLog << " (\"" << val.text << "\")";
                    *lrLog << "  → state " << action.value << "\n";
                }
                valueStack.push_back(inputValues[ip]);              // 压入语义值
                ip++;
                continue;
            }

            // ========== REDUCE 动作 ==========
            if (action.type == LRAction::Reduce) {
                const Production& p = productions_[action.value];   // 取出产生式

                // 从栈中弹出 |RHS| 个状态和语义值
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
                // RHS 是倒序弹出的，需要翻转恢复原始顺序
                reverse(rhsValues.begin(), rhsValues.end());

                if (stateStack.empty()) {
                    errorMsg = "LR 栈为空，无法 GOTO";
                    return false;
                }

                /* ========== 语义动作：规约回调 ==========

                   在此处调用规约回调来生成四元式或其他中间代码。
                   回调接收：
                     - 产生式编号（action.value）
                     - LHS 非终结符名（p.lhs）
                     - 右侧各符号的语义值列表（rhsValues）

                   规约回调应返回 LHS 对应的新语义值。

                   例子：产生式 P17: ArithExpr → ArithExpr + Term
                     rhsValues = [ArithExpr(@a), Token(+), Term(@b)]
                     规约回调计算得出：t1 = a + b
                     生成四元式：(+, a, b, t1)
                     返回语义值：SemanticValue(symbol="ArithExpr", text="t1")
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

                // 查 GOTO 表：从规约前的状态通过 LHS 转移到新状态
                int fromState = stateStack.back();
                auto gIt = gotoTable_.find({fromState, p.lhs});
                if (gIt == gotoTable_.end()) {
                    errorMsg = "缺少 GOTO[" + to_string(fromState) + ", " + p.lhs + "]";
                    return false;
                }
                stateStack.push_back(gIt->second);
                continue;
            }

            // ========== ACCEPT 动作 ==========
            if (action.type == LRAction::Accept) {
                if (ip != inputValues.size()) {
                    errorMsg = "表达式未完全消耗";
                    return false;
                }
                if (lrLog) *lrLog << "  [LR] Accept  (表达式分析成功)\n";
                // 保存顶层规约结果的语义值
                if (acceptValue && !valueStack.empty()) {
                    *acceptValue = valueStack.back();
                }
                return true;
            }

            // ========== 错误处理 ==========
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

    // ========== 诊断输出方法：导出 SELECT 集与 LR(1) 分析表 ==========
    //
    // 返回包含以下内容的文本：
    //   - 所有非ε产生式的 SELECT 集（用于递归下降分析的分支选择）
    //   - ACTION 表（Shift/Reduce/Accept 的最终分析决策）
    //   - GOTO 表（规约后的状态转移）
    //
    // 主要用于调试和验证 LR(1) 构建的正确性
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
    // ========== 数据成员 ==========
    string startSymbol_ = "Expr";          // 文法起始符号
    string startPrime_ = "Expr'";          // 增广文法的起始符号（E' → E）

    vector<Production> productions_;       // 所有产生式（包括增广）
    set<string> nonTerminals_;             // 所有非终结符
    set<string> terminals_;                // 所有终结符

    map<string, set<string>> firstSets_;   // 每个符号的 FIRST 集

    vector<set<LR1Item>> states_;          // LR(1) 标准族的所有状态
    map<pair<int, string>, int> transitions_;  // 转移函数
    map<pair<int, string>, LRAction> actionTable_;  // ACTION 表：状态+终结符 → 动作
    map<pair<int, string>, int> gotoTable_;        // GOTO 表：状态+非终结符 → 新状态
    map<int, set<string>> selectSets_;    // 每条产生式的 SELECT 集

    vector<string> orderedTerminalsWithEnd_;  // 有序终结符列表（含 $）
    vector<string> orderedNonTerminals_;      // 有序非终结符列表

    bool buildOk_ = false;                 // 构建是否成功
    string buildError_;                    // 构建错误消息

private:
    // ========== 初始化：定义表达式文法 ==========
    //
    // 定义的文法包括：
    //   - 布尔表达式（逻辑或、逻辑与）
    //   - 关系表达式（< > <= >= = <>）
    //   - 算术表达式（+ - * /）
    //   - 因子（标识符、常量、括号、一元负号、函数调用）
    void initGrammar() {
        // 增广文法：E' → E （用于 LR 自动机的接受态）
        productions_.push_back({startPrime_, {startSymbol_}});

        // 形式化表达式文法（根据优先级从低到高排序）
        // 第一优先级：逻辑或（||）
        productions_.push_back({"Expr", {"BoolExpr"}});
        productions_.push_back({"BoolExpr", {"BoolExpr", "||", "BoolTerm"}});

        // 第二优先级：逻辑与（&&）
        productions_.push_back({"BoolExpr", {"BoolTerm"}});
        productions_.push_back({"BoolTerm", {"BoolTerm", "&&", "BoolFactor"}});

        // 第三优先级：逻辑非（!）、关系表达式
        productions_.push_back({"BoolTerm", {"BoolFactor"}});
        productions_.push_back({"BoolFactor", {"!", "BoolFactor"}});
        productions_.push_back({"BoolFactor", {"RelExpr"}});

        // 第四优先级：关系运算符（<、>、<=、>=、=、<>）
        productions_.push_back({"RelExpr", {"ArithExpr", "RelOp", "ArithExpr"}});
        productions_.push_back({"RelExpr", {"ArithExpr"}});
        productions_.push_back({"RelOp", {">"}});
        productions_.push_back({"RelOp", {">="}});
        productions_.push_back({"RelOp", {"<"}});
        productions_.push_back({"RelOp", {"<="}});
        productions_.push_back({"RelOp", {"="}});
        productions_.push_back({"RelOp", {"<>"}});

        // 第五优先级：加减法（+ -）
        productions_.push_back({"ArithExpr", {"ArithExpr", "+", "Term"}});
        productions_.push_back({"ArithExpr", {"ArithExpr", "-", "Term"}});
        productions_.push_back({"ArithExpr", {"Term"}});

        // 第六优先级：乘除法（* /）
        productions_.push_back({"Term", {"Term", "*", "Factor"}});
        productions_.push_back({"Term", {"Term", "/", "Factor"}});
        productions_.push_back({"Term", {"Factor"}});

        // 第七优先级：因子（最高，包括常量、标识符、括号、一元操作等）
        productions_.push_back({"Factor", {"id", "CallSuffix"}});  // id 或 函数调用
        productions_.push_back({"Factor", {"int"}});               // 整数常量
        productions_.push_back({"Factor", {"real"}});              // 实数常量
        productions_.push_back({"Factor", {"string_lit"}});        // 字符串常量
        productions_.push_back({"Factor", {"true"}});              // 布尔常量
        productions_.push_back({"Factor", {"false"}});             // 布尔常量
        productions_.push_back({"Factor", {"(", "Expr", ")"}});    // 括号表达式
        productions_.push_back({"Factor", {"-", "Factor"}});       // 一元负号

        // 函数调用后缀
        productions_.push_back({"CallSuffix", {"(", "ActualParamList", ")"}});  // 有实参
        productions_.push_back({"CallSuffix", {}});                              // ε（无实参）

        // 实参表列表
        productions_.push_back({"ActualParamList", {"Expr", "ActualParamListTail"}});
        productions_.push_back({"ActualParamList", {}});            // ε
        productions_.push_back({"ActualParamListTail", {",", "Expr", "ActualParamListTail"}});
        productions_.push_back({"ActualParamListTail", {}});         // ε

        // 收集非终结符和终结符
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

        // 建立有序同步集
        orderedNonTerminals_.assign(nonTerminals_.begin(), nonTerminals_.end());
        orderedTerminalsWithEnd_.assign(terminals_.begin(), terminals_.end());
        orderedTerminalsWithEnd_.push_back("$");  // 添加 EOF 标记
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
    scopeOffsets_.assign(1, 0);
}

/**
 * 解析器的主解析函数，负责初始化解析状态并开始解析程序
 * @return 解析是否成功
 */
bool Parser::parse() {
    // 初始化日志输出流
    log_ = ostringstream();
    // 初始化缩进级别
    indent_ = 0;
    // 初始化错误标志
    hasError_ = false;
    // 清空错误信息
    errorMsg_.clear();
    // 清空四元式列表
    quadruples_.clear();
    // 清空待处理标识符列表
    pendingIdentifiers_.clear();
    // 清空实际参数列表
    pendingActualArgs_.clear();
    // 初始化作用域级别为0
    scopeLevel_ = 0;
    // 初始化作用域偏移量列表，初始大小为1，值为0
    scopeOffsets_.assign(1, 0);
    // 清空作用域路径
    scopePath_.clear();
    // 初始化临时变量计数器
    tempCounter_ = 0;
    // 初始化标签计数器
    labelCounter_ = 0;
    // 初始化当前例程符号表索引为-1
    currentRoutineSymbolIndex_ = -1;
    // 初始化当前例程参数计数为0
    currentRoutineParamCount_ = 0;
    // 初始化最后解析的类型索引为无效类型索引
    lastParsedTypeIndex_ = INVALID_TYPE_INDEX;
    // 清空最后解析的类型代码
    lastParsedTypeCode_.clear();
    // 清空最后表达式位置信息
    lastExpressionPlace_.clear();
    // 清空最后语句标识符信息
    lastStatementIdentifier_.clear();

    // 检查token序列是否为空
    if (tokens_.empty()) {
        logInfo("token 序列为空，无需分析");
        return true;
    }

    // 开始解析程序
    bool ok = parseProgram();

    // 检查是否到达token序列末尾且没有错误
    if (!isAtEnd() && !hasError_) {
        error("解析结束后仍有未消耗的 token");
        ok = false;
    }

    // 根据解析结果输出相应信息
    if (ok && !hasError_) {
        logInfo(getSymbolTableDump());
        logInfo("===== 语法分析通过 =====");
    } else {
        logInfo("===== 语法分析失败 =====");
    }

    // 返回解析结果
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
        const FourTuple& q = quadruples_[i];
        out << i << ": (" << q.operator_str << ", " << q.first_value << ", "
            << q.second_value << ", " << q.dist << ")\n";
    }
    if (quadruples_.empty()) {
        out << "(empty)\n";
    }
    return out.str();
}

const vector<FourTuple>& Parser::getQuadruples() const {
    return quadruples_;
}

void Parser::setQuadruples(const vector<FourTuple>& quadruples) {
    quadruples_ = quadruples;
}

string Parser::getSymbolTableDump() const {
    using std::left;
    using std::setw;
    ostringstream out;
    out << "===== 符号表(SYNBL) =====\n";
    out << left << setw(6) << "idx" << setw(24) << "name"
        << setw(8) << "typ" << setw(8) << "cat" << setw(16) << "addr" << '\n';
    for (size_t i = 0; i < ctx.synbl.size(); ++i) {
        const SynblItem& s = ctx.synbl[i];
        out << left << setw(6) << i << setw(24) << s.name
            << setw(8) << s.typ << setw(8) << s.cat << setw(16) << s.addr << '\n';
    }
    if (ctx.synbl.empty()) out << "(empty)\n";

    out << "===== 类型表(TYPEL) =====\n";
    out << left << setw(6) << "idx" << setw(8) << "tval" << setw(8) << "tpoint" << '\n';
    for (size_t i = 0; i < ctx.typel.size(); ++i) {
        const TypelItem& t = ctx.typel[i];
        out << left << setw(6) << i << setw(8) << t.tval << setw(8) << t.tpoint << '\n';
    }
    if (ctx.typel.empty()) out << "(empty)\n";

    out << "===== 过程/函数信息表(PFINFL) =====\n";
    out << left << setw(6) << "idx" << setw(8) << "level" << setw(8) << "off"
        << setw(8) << "fn" << setw(8) << "entry" << setw(8) << "param" << '\n';
    for (size_t i = 0; i < ctx.pfinfl.size(); ++i) {
        const PfinflItem& p = ctx.pfinfl[i];
        out << left << setw(6) << i << setw(8) << p.level << setw(8) << p.off
            << setw(8) << p.fn << setw(8) << p.entry << setw(8) << p.param << '\n';
    }
    if (ctx.pfinfl.empty()) out << "(empty)\n";

    out << "===== 形参表(PARAMBL) =====\n";
    out << left << setw(6) << "idx" << setw(24) << "name"
        << setw(8) << "typ" << setw(8) << "cat" << setw(16) << "addr" << '\n';
    for (size_t i = 0; i < ctx.parambl.size(); ++i) {
        const SynblItem& s = ctx.parambl[i];
        out << left << setw(6) << i << setw(24) << s.name
            << setw(8) << s.typ << setw(8) << s.cat << setw(16) << s.addr << '\n';
    }
    if (ctx.parambl.empty()) out << "(empty)\n";

    out << "===== 长度表(LENL) =====\n";
    out << left << setw(6) << "idx" << setw(8) << "length" << '\n';
    for (size_t i = 0; i < ctx.lenl.size(); ++i) {
        const LenlItem& l = ctx.lenl[i];
        out << left << setw(6) << i << setw(8) << l.length << '\n';
    }
    if (ctx.lenl.empty()) out << "(empty)\n";

    out << "===== 数组表(AINFL) =====\n";
    out << left << setw(6) << "idx" << setw(8) << "low"
        << setw(8) << "up" << setw(8) << "ctp" << setw(8) << "clen" << '\n';
    for (size_t i = 0; i < ctx.ainfl.size(); ++i) {
        const AinflItem& a = ctx.ainfl[i];
        out << left << setw(6) << i << setw(8) << a.low
            << setw(8) << a.up << setw(8) << a.ctp << setw(8) << a.clen << '\n';
    }
    if (ctx.ainfl.empty()) out << "(empty)\n";

    out << "===== 记录表(RINFL) =====\n";
    out << left << setw(6) << "idx" << setw(24) << "id"
        << setw(8) << "off" << setw(8) << "tp" << '\n';
    for (size_t i = 0; i < ctx.rinfl.size(); ++i) {
        const RinflItem& r = ctx.rinfl[i];
        out << left << setw(6) << i << setw(24) << r.id
            << setw(8) << r.off << setw(8) << r.tp << '\n';
    }
    if (ctx.rinfl.empty()) out << "(empty)\n";

    out << "===== 常量表(CONSL1) =====\n";
    out << left << setw(6) << "idx" << setw(16) << "value" << '\n';
    for (size_t i = 0; i < ctx.consl1.size(); ++i) {
        out << left << setw(6) << i << setw(16) << ctx.consl1[i] << '\n';
    }
    if (ctx.consl1.empty()) out << "(empty)\n";

    out << "===== 常量表(CONSL2) =====\n";
    out << left << setw(6) << "idx" << setw(16) << "value" << '\n';
    for (size_t i = 0; i < ctx.consl2.size(); ++i) {
        out << left << setw(6) << i << setw(16) << ctx.consl2[i] << '\n';
    }
    if (ctx.consl2.empty()) out << "(empty)\n";

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

void Parser::enterScope() {
    ++scopeLevel_;
    if (scopeLevel_ >= static_cast<int>(scopeOffsets_.size())) {
        scopeOffsets_.resize(scopeLevel_ + 1, 0);
    }
    scopeOffsets_[scopeLevel_] = 0;
}

void Parser::leaveScope() {
    if (scopeLevel_ > 0) --scopeLevel_;
}

void Parser::enterRoutine(const string& name) {
    scopePath_.push_back(name);
    enterScope();
}

void Parser::leaveRoutine() {
    if (!scopePath_.empty()) scopePath_.pop_back();
    leaveScope();
}

string Parser::newScopedName(const string& name) const {
    string result = name;
    for (const string& seg : scopePath_) {
        result += "$" + seg;
    }
    return result;
}

string Parser::calleeResultName(const string& callee) const {
    string result = "_result";
    for (const string& seg : scopePath_) result += "$" + seg;
    result += "$" + callee;
    return result;
}

int Parser::allocateOffsetForCurrentScope() {
    if (scopeLevel_ < 0) return 0;
    if (scopeLevel_ >= static_cast<int>(scopeOffsets_.size())) {
        scopeOffsets_.resize(scopeLevel_ + 1, 0);
    }
    return scopeOffsets_[scopeLevel_]++;
}

string Parser::formatAddr(int level, int offset) const {
    return "(" + to_string(level) + ", " + to_string(offset) + ")";
}

string Parser::newTemp(int typ) {
    string name = newScopedName("_t" + to_string(++tempCounter_));
    SynblItem item;
    item.name = name;
    item.typ = typ;
    item.cat = "v";
    item.addr = formatAddr(scopeLevel_, allocateOffsetForCurrentScope());
    ctx.synbl.push_back(item);
    return name;
}

string Parser::newLabel() {
    return "L_" + to_string(++labelCounter_);
}

int Parser::emitQuad(const string& op, const string& arg1, const string& arg2, const string& result) {
    quadruples_.push_back({op, arg1, arg2, result});
    return static_cast<int>(quadruples_.size()) - 1;
}

void Parser::backpatchQuadResult(int quadIndex, int target) {
    if (quadIndex < 0 || quadIndex >= static_cast<int>(quadruples_.size())) return;
    quadruples_[quadIndex].dist = to_string(target);
}

void Parser::backpatchQuadResult(int quadIndex, const string& target) {
    if (quadIndex < 0 || quadIndex >= static_cast<int>(quadruples_.size())) return;
    quadruples_[quadIndex].dist = target;
}

void Parser::declarePendingIdentifiers(const string& cat, int typ) {
    for (int idx : pendingIdentifiers_) {
        if (idx < 0 || idx >= static_cast<int>(ctx.synbl.size())) continue;
        ctx.synbl[idx].typ = typ;
        ctx.synbl[idx].cat = cat;
        ctx.synbl[idx].addr = formatAddr(scopeLevel_, allocateOffsetForCurrentScope());
        if (cat == "vf" || cat == "vn") {
            ctx.parambl.push_back(ctx.synbl[idx]);
        }
    }
    pendingIdentifiers_.clear();
}

// ============================================================
// 语义验证：数组下标越界 & 记录字段存在性
// ============================================================

void Parser::validateSubscriptBound(const string& baseName, const string& indexText) {
    // 仅在索引为常整数时做静态检查
    if (indexText.empty()) return;
    int indexVal = 0;
    try {
        size_t p = 0;
        indexVal = stoi(indexText, &p);
        if (p != indexText.size()) return;   // 不是纯整数 → 跳过
    } catch (...) { return; }

    // 查找基础变量在符号表中的入口
    int synblIdx = -1;
    for (int i = 0; i < static_cast<int>(ctx.synbl.size()); ++i) {
        if (ctx.synbl[i].name == baseName) { synblIdx = i; break; }
    }
    if (synblIdx < 0) return;  // 未找到（可能是拼写错误，由其他阶段报告）

    int typIdx = ctx.synbl[synblIdx].typ;
    if (typIdx < 0 || typIdx >= static_cast<int>(ctx.typel.size())) return;

    const TypelItem& ti = ctx.typel[typIdx];
    if (ti.tval != "a") return;  // 不是数组类型

    int ainflIdx = ti.tpoint;
    if (ainflIdx < 0 || ainflIdx >= static_cast<int>(ctx.ainfl.size())) return;

    const AinflItem& ai = ctx.ainfl[ainflIdx];
    if (indexVal < ai.low || indexVal > ai.up) {
        string msg = "数组下标越界: " + baseName + "[" + indexText + "]"
                     + " 应在 [" + to_string(ai.low) + ".." + to_string(ai.up) + "] 内";
        logInfo(msg);
    }
}

void Parser::validateFieldExists(const string& baseName, const string& fieldName) {
    if (fieldName.empty()) return;

    // 查找基础变量
    int synblIdx = -1;
    for (int i = 0; i < static_cast<int>(ctx.synbl.size()); ++i) {
        if (ctx.synbl[i].name == baseName) { synblIdx = i; break; }
    }
    if (synblIdx < 0) return;

    int typIdx = ctx.synbl[synblIdx].typ;
    if (typIdx < 0 || typIdx >= static_cast<int>(ctx.typel.size())) return;

    const TypelItem& ti = ctx.typel[typIdx];
    if (ti.tval != "d") return;  // 不是记录类型

    int rinflStart = ti.tpoint;
    if (rinflStart < 0 || rinflStart >= static_cast<int>(ctx.rinfl.size())) return;

    // 扫描记录的所有字段
    bool found = false;
    for (int i = rinflStart; i < static_cast<int>(ctx.rinfl.size()); ++i) {
        if (ctx.rinfl[i].id == fieldName) { found = true; break; }
        // 遇到下一个结构体的起始则停止（不同记录类型不交叉）
        // 简化处理：检查是否遇到另一条记录的字段开始
    }

    if (!found) {
        string msg = "字段不存在: " + baseName + "." + fieldName + " 不在记录定义中";
        logInfo(msg);
    }
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
// Token 类型与关键字匹配
// ============================================================

// 检查当前 token 类型是否匹配
//
// 参数：type - 期望的 token 类型（如 "ID"、"INT"、"REAL" 等）
// 返回：true 若当前 token 类型匹配，false 否则
bool Parser::checkType(const string& type) const {
    return !isAtEnd() && current().type == type;
}

// 检查并消耗指定 token 类型
//
// 参数：type - 期望的 token 类型
// 返回：true 若匹配成功并消耗，false 若不匹配
bool Parser::matchType(const string& type) {
    if (checkType(type)) {
        logMatch(advance());
        return true;
    }
    return false;
}

// 检查当前 token 是否为指定关键字
//
// 关键字存储在全局 ctx.keywordTable 中，token.value 是索引值
// 参数：kw - 期望的关键字（如 "if"、"while"、"var" 等）
// 返回：true 若当前 token 是该关键字，false 否则
bool Parser::checkKeyword(const string& kw) const {
    if (isAtEnd()) return false;
    Token t = current();
    if (t.type != "KEYWORD") return false;
    int idx = stoi(t.value);
    if (idx < 0 || idx >= static_cast<int>(ctx.keywordTable.size())) return false;
    return ctx.keywordTable[idx] == kw;
}

// 检查并消耗指定关键字
//
// 参数：kw - 期望的关键字
// 返回：true 若匹配成功并消耗，false 若不匹配
bool Parser::matchKeyword(const string& kw) {
    if (checkKeyword(kw)) {
        logMatch(advance());
        return true;
    }
    return false;
}

// 检查当前 token 是否为指定分隔符/操作符
//
// 分隔符存储在全局 ctx.delimiterTable 中，token.value 是索引值
// 参数：delim - 期望的分隔符/操作符（如 "("、"+"、";" 等）
// 返回：true 若当前 token 是该分隔符，false 否则
bool Parser::checkDelimiter(const string& delim) const {
    if (isAtEnd()) return false;
    Token t = current();
    if (t.type != "DELIMITER") return false;
    int idx = stoi(t.value);
    if (idx < 0 || idx >= static_cast<int>(ctx.delimiterTable.size())) return false;
    return ctx.delimiterTable[idx] == delim;
}

// 检查并消耗指定分隔符/操作符
//
// 参数：delim - 期望的分隔符/操作符
// 返回：true 若匹配成功并消耗，false 若不匹配
bool Parser::matchDelimiter(const string& delim) {
    if (checkDelimiter(delim)) {
        logMatch(advance());
        return true;
    }
    return false;
}

// 检查当前 token 是否为标识符
bool Parser::checkId() const {
    return !isAtEnd() && current().type == "ID";
}

// 检查并消耗标识符
//
// 返回：true 若当前 token 是标识符并成功消耗，false 否则
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

/**
 * 解析程序结构的函数
 * @return 解析成功返回true，失败返回false
 */
bool Parser::parseProgram() {
    // 进入规则"程序"
    enterRule("程序");

    // 检查是否存在关键字"program"
    if (!matchKeyword("program")) {
        error("缺少关键字 'program'");
        exitRule("程序", false);
        return false;
    }

    /* SEMANTIC: 程序名入符号表 */
    // 获取当前标识符的索引
    int programIdx = currentIdIndex();

    // 检查程序名是否为有效标识符
    if (!matchId()) {
        error("缺少程序名（标识符）");
        exitRule("程序", false);
        return false;
    }
    // 如果索引有效，将符号表中的对应项标记为程序(p)，并设置地址
    if (programIdx >= 0 && programIdx < static_cast<int>(ctx.synbl.size())) {
        ctx.synbl[programIdx].cat = "p";
        ctx.synbl[programIdx].addr = formatAddr(0, -1);
    }

    // 检查程序名后是否有分号
    if (!matchDelimiter(";")) {
        error("程序名后缺少 ';'");
        exitRule("程序", false);
        return false;
    }

    // 解析子程序
    if (!parseSubProgram()) {
        exitRule("程序", false);
        return false;
    }

    // 检查程序末尾是否有句号
    if (!matchDelimiter(".")) {
        error("程序末尾缺少 '.'");
        exitRule("程序", false);
        return false;
    }

    // 退出规则"程序"，返回解析成功
    exitRule("程序", true);
    return true;
}

bool Parser::parseSubProgram(bool enterNewScope) {
    enterRule("分程序");

    if (enterNewScope) {
        /* SEMANTIC: 进入新的作用域层级 */
        enterScope();
    }

    if (!parseDeclarationPart()) {
        if (enterNewScope) leaveScope();
        exitRule("分程序", false);
        return false;
    }

    if (!parseCompoundStatement()) {
        if (enterNewScope) leaveScope();
        exitRule("分程序", false);
        return false;
    }

    if (enterNewScope) {
        /* SEMANTIC: 退出作用域层级 */
        leaveScope();
    }

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

    // FIRST(〈说明语句〉) = { var, function, procedure, type }
    while (checkKeyword("var") || checkKeyword("function") || checkKeyword("procedure") || checkKeyword("type")) {
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
    } else if (checkKeyword("type")) {
        ok = parseTypeDeclaration();
    } else {
        error("缺少 var / function / procedure / type");
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
    string funcName = (funcIdx >= 0 && funcIdx < static_cast<int>(ctx.synbl.size()))
                      ? ctx.synbl[funcIdx].name : "?";
    if (funcIdx >= 0 && funcIdx < static_cast<int>(ctx.synbl.size())) {
        ctx.synbl[funcIdx].cat = "f";
        ctx.synbl[funcIdx].addr = "PFINFL[-1]";
    }

    // 生成函数入口标签（scopePath 还不含本函数名）
    {
        string entryLabel = funcName + "_entry";
        for (const string& seg : scopePath_) entryLabel += "$" + seg;
        emitQuad("lb", entryLabel, "", "");
    }
    enterRoutine(funcName);

    if (!parseFormalParameters()) {
        leaveRoutine();
        exitRule("函数说明", false);
        return false;
    }

    if (!matchDelimiter(":")) {
        leaveRoutine();
        error("函数缺少返回类型前的 ':'");
        exitRule("函数说明", false);
        return false;
    }

    if (!parseType()) {
        leaveRoutine();
        exitRule("函数说明", false);
        return false;
    }

    /* SEMANTIC: 设置函数返回类型 */
    if (currentRoutineSymbolIndex_ >= 0 && currentRoutineSymbolIndex_ < static_cast<int>(ctx.synbl.size())) {
        ctx.synbl[currentRoutineSymbolIndex_].typ = lastParsedTypeIndex_;
    }

    if (!matchDelimiter(";")) {
        leaveRoutine();
        error("函数返回类型后缺少 ';'");
        exitRule("函数说明", false);
        return false;
    }

    if (!parseSubProgram(false)) {
        leaveRoutine();
        exitRule("函数说明", false);
        return false;
    }

    if (!matchDelimiter(";")) {
        leaveRoutine();
        error("函数体后缺少 ';'");
        exitRule("函数说明", false);
        return false;
    }

    /* SEMANTIC: 函数结束，返回调用点 */
    emitQuad("ret", "", "", "");

    currentRoutineSymbolIndex_ = -1;
    leaveRoutine();

    exitRule("函数说明", true);
    return true;
}

bool Parser::parseFormalParameters() {
    enterRule("形式参数");
    int paramStart = static_cast<int>(ctx.parambl.size());

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
        item.param = (currentRoutineParamCount_ > 0 ? paramStart : -1);
        ctx.pfinfl.push_back(item);
        int pfinflIndex = static_cast<int>(ctx.pfinfl.size()) - 1;
        if (currentRoutineSymbolIndex_ < static_cast<int>(ctx.synbl.size())) {
            ctx.synbl[currentRoutineSymbolIndex_].addr = "PFINFL[" + to_string(pfinflIndex) + "]";
        }
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
    string procName = (procIdx >= 0 && procIdx < static_cast<int>(ctx.synbl.size()))
                      ? ctx.synbl[procIdx].name : "?";
    if (procIdx >= 0 && procIdx < static_cast<int>(ctx.synbl.size())) {
        ctx.synbl[procIdx].cat = "p";
        ctx.synbl[procIdx].addr = "PFINFL[-1]";
    }

    // 生成过程入口标签
    {
        string entryLabel = procName + "_entry";
        for (const string& seg : scopePath_) entryLabel += "$" + seg;
        emitQuad("lb", entryLabel, "", "");
    }
    enterRoutine(procName);

    if (!parseFormalParameters()) {
        leaveRoutine();
        exitRule("过程说明", false);
        return false;
    }

    if (!matchDelimiter(";")) {
        leaveRoutine();
        error("过程参数后缺少 ';'");
        exitRule("过程说明", false);
        return false;
    }

    if (!parseSubProgram(false)) {
        leaveRoutine();
        exitRule("过程说明", false);
        return false;
    }

    if (!matchDelimiter(";")) {
        leaveRoutine();
        error("过程体后缺少 ';'");
        exitRule("过程说明", false);
        return false;
    }

    /* SEMANTIC: 过程结束，返回调用点 */
    emitQuad("ret", "", "", "");

    currentRoutineSymbolIndex_ = -1;
    leaveRoutine();

    exitRule("过程说明", true);
    return true;
}

// ============================================================
// §6  类型说明（type 声明段）
//     〈类型说明〉 → type 〈类型定义表〉
//     〈类型定义表〉 → 〈类型定义〉 ; 〈类型定义表〉 | 〈类型定义〉 ;
//     〈类型定义〉 → ID = 〈类型构造器〉
//     〈类型构造器〉 → array [ INT .. INT ] of 〈类型引用〉
//                     | record 〈字段表〉 end
//     〈字段表〉 → 〈字段〉 ; 〈字段表〉 | 〈字段〉 ;
//     〈字段〉 → 〈标识符表〉 : 〈类型引用〉
//
//   语义动作：不生成四元式。填充 TYPEL/AINFL/RINFL 表，
//   类型名写入 SYNBL（cat='t'）。
// ============================================================

bool Parser::parseTypeDeclaration() {
    enterRule("类型说明");

    if (!matchKeyword("type")) {
        error("缺少关键字 'type'");
        exitRule("类型说明", false);
        return false;
    }

    if (!parseTypeDefinitionList()) {
        exitRule("类型说明", false);
        return false;
    }

    exitRule("类型说明", true);
    return true;
}

bool Parser::parseTypeDefinitionList() {
    enterRule("类型定义表");

    // 至少一条类型定义
    if (!parseTypeDefinition()) {
        exitRule("类型定义表", false);
        return false;
    }

    if (!matchDelimiter(";")) {
        error("类型定义后缺少 ';'");
        exitRule("类型定义表", false);
        return false;
    }

    /* SEMANTIC: 将类型名注册到符号表 */

    // 循环处理后续定义：FIRST(〈类型定义〉) = { ID }
    while (checkId()) {
        if (!parseTypeDefinition()) {
            exitRule("类型定义表", false);
            return false;
        }

        if (!matchDelimiter(";")) {
            error("类型定义后缺少 ';'");
            exitRule("类型定义表", false);
            return false;
        }

        /* SEMANTIC: 将类型名注册到符号表 */
    }

    exitRule("类型定义表", true);
    return true;
}

bool Parser::parseTypeDefinition() {
    enterRule("类型定义");

    // 读取类型名
    if (!matchId()) {
        error("缺少类型名");
        exitRule("类型定义", false);
        return false;
    }

    // 获取刚匹配的类型名在 synbl 中的索引
    int typeNameSynblIndex = -1;
    if (pos_ >= 1 && tokens_[pos_ - 1].type == "ID") {
        int idx = -1;
        try { idx = stoi(tokens_[pos_ - 1].value); } catch (...) {}
        if (idx >= 0 && idx < static_cast<int>(ctx.synbl.size()))
            typeNameSynblIndex = idx;
    }

    if (!matchDelimiter("=")) {
        error("类型定义中缺少 '='");
        exitRule("类型定义", false);
        return false;
    }

    // 解析类型构造器，返回新建的 TYPEL 索引
    int typelIndex = -1;
    if (!parseTypeConstructor(typelIndex)) {
        exitRule("类型定义", false);
        return false;
    }

    // 将类型名写入符号表（cat='t'，addr 指向 TYPEL 条目）
    if (typeNameSynblIndex >= 0 && typelIndex >= 0) {
        ctx.synbl[typeNameSynblIndex].cat = "t";
        ctx.synbl[typeNameSynblIndex].typ = typelIndex;
        ctx.synbl[typeNameSynblIndex].addr = "";  // 类型定义无运行时地址
    }

    exitRule("类型定义", true);
    return true;
}

bool Parser::parseTypeConstructor(int& typelIndex) {
    enterRule("类型构造器");

    typelIndex = -1;

    if (checkKeyword("array")) {
        // array [ INT .. INT ] of 〈类型引用〉
        if (!matchKeyword("array")) {
            error("缺少关键字 'array'");
            exitRule("类型构造器", false);
            return false;
        }

        if (!matchDelimiter("[")) {
            error("array 后缺少 '['");
            exitRule("类型构造器", false);
            return false;
        }

        // 下界
        int low = 0;
        if (current().type == "CONSL1") {
            low = ctx.consl1[stoi(current().value)];
        } else {
            error("数组下界必须是整数常量");
            exitRule("类型构造器", false);
            return false;
        }
        logMatch(advance());

        if (!matchDelimiter("..")) {
            error("数组界缺少 '..'");
            exitRule("类型构造器", false);
            return false;
        }

        // 上界
        int up = 0;
        if (current().type == "CONSL1") {
            up = ctx.consl1[stoi(current().value)];
        } else {
            error("数组上界必须是整数常量");
            exitRule("类型构造器", false);
            return false;
        }
        logMatch(advance());

        if (!matchDelimiter("]")) {
            error("数组界缺少 ']'");
            exitRule("类型构造器", false);
            return false;
        }

        if (!matchKeyword("of")) {
            error("array 后缺少 'of'");
            exitRule("类型构造器", false);
            return false;
        }

        // 元素类型
        if (!parseType()) {
            exitRule("类型构造器", false);
            return false;
        }

        int elemTypIndex = lastParsedTypeIndex_;

        // 计算元素长度
        int clen = 1;  // 默认 1 个值单元
        if (elemTypIndex >= 0 && elemTypIndex < static_cast<int>(ctx.typel.size())) {
            // 简化：所有基本类型占 1 个值单元
            clen = 1;
        }

        // 填 AINFL
        AinflItem ai;
        ai.low = low;
        ai.up = up;
        ai.ctp = elemTypIndex;
        ai.clen = clen;
        ctx.ainfl.push_back(ai);
        int ainflIndex = static_cast<int>(ctx.ainfl.size()) - 1;

        // 填 TYPEL（数组类型）
        TypelItem ti;
        ti.tval = "a";
        ti.tpoint = ainflIndex;
        ctx.typel.push_back(ti);
        typelIndex = static_cast<int>(ctx.typel.size()) - 1;

        logInfo("数组类型: [" + to_string(low) + ".." + to_string(up) + "] of type[" +
                to_string(elemTypIndex) + "], clen=" + to_string(clen));

    } else if (checkKeyword("record")) {
        // record 〈字段表〉 end
        if (!matchKeyword("record")) {
            error("缺少关键字 'record'");
            exitRule("类型构造器", false);
            return false;
        }

        // 记录解析前的 RINFL 起始位置
        int rinflStart = static_cast<int>(ctx.rinfl.size());
        int currentOff = 0;

        if (!parseFieldList(currentOff)) {
            exitRule("类型构造器", false);
            return false;
        }

        if (!matchKeyword("end")) {
            error("record 缺少 'end'");
            exitRule("类型构造器", false);
            return false;
        }

        // 填 TYPEL（结构类型）
        TypelItem ti;
        ti.tval = "d";
        ti.tpoint = rinflStart;  // 指向 RINFL 第一条字段
        ctx.typel.push_back(ti);
        typelIndex = static_cast<int>(ctx.typel.size()) - 1;

        logInfo("记录类型: " + to_string(static_cast<int>(ctx.rinfl.size()) - rinflStart) +
                " 个字段, RINFL 起始=" + to_string(rinflStart));
    } else {
        error("缺少 array 或 record");
        exitRule("类型构造器", false);
        return false;
    }

    exitRule("类型构造器", true);
    return true;
}

bool Parser::parseFieldList(int& currentOff) {
    enterRule("字段表");

    // 至少一个字段
    if (!parseField(currentOff)) {
        exitRule("字段表", false);
        return false;
    }

    if (!matchDelimiter(";")) {
        error("字段后缺少 ';'");
        exitRule("字段表", false);
        return false;
    }

    // 后续字段
    while (checkId()) {
        if (!parseField(currentOff)) {
            exitRule("字段表", false);
            return false;
        }

        if (!matchDelimiter(";")) {
            error("字段后缺少 ';'");
            exitRule("字段表", false);
            return false;
        }
    }

    exitRule("字段表", true);
    return true;
}

bool Parser::parseField(int& currentOff) {
    enterRule("字段");

    // 收集字段标识符
    vector<int> fieldIds;
    if (!matchId()) {
        error("字段缺少标识符");
        exitRule("字段", false);
        return false;
    }
    if (pos_ >= 1 && tokens_[pos_ - 1].type == "ID") {
        int idx = -1;
        try { idx = stoi(tokens_[pos_ - 1].value); } catch (...) {}
        if (idx >= 0 && idx < static_cast<int>(ctx.synbl.size()))
            fieldIds.push_back(idx);
    }

    while (matchDelimiter(",")) {
        if (!matchId()) {
            error("',' 后缺少标识符");
            exitRule("字段", false);
            return false;
        }
        if (pos_ >= 1 && tokens_[pos_ - 1].type == "ID") {
            int idx = -1;
            try { idx = stoi(tokens_[pos_ - 1].value); } catch (...) {}
            if (idx >= 0 && idx < static_cast<int>(ctx.synbl.size()))
                fieldIds.push_back(idx);
        }
    }

    if (!matchDelimiter(":")) {
        error("字段缺少 ':'");
        exitRule("字段", false);
        return false;
    }

    if (!parseType()) {
        exitRule("字段", false);
        return false;
    }

    int fieldTypIndex = lastParsedTypeIndex_;

    // 为每个字段标识符填 RINFL
    for (int idIdx : fieldIds) {
        RinflItem ri;
        ri.id = ctx.synbl[idIdx].name;
        ri.off = currentOff;
        ri.tp = fieldTypIndex;
        ctx.rinfl.push_back(ri);
        currentOff++;  // 每个字段占 1 个值单元
    }

    exitRule("字段", true);
    return true;
}

// ============================================================
// §7  复合语句
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

    /* SEMANTIC: 生成 if 条件四元式（假出口待回填） */
    int ifIndex = emitQuad("if", lastExpressionPlace_, "", "?");

    // then 分支
    if (!parseStatement()) {
        exitRule("if语句", false);
        return false;
    }

    /* SEMANTIC: 生成 else 跳转并回填出口 */
    int elIndex = -1;

    // 可选的 else 分支
    if (matchKeyword("else")) {
        /* SEMANTIC: then 末尾发出 el，并把 if 假出口回填到 else 起点 */
        elIndex = emitQuad("el", "", "", "?");
        backpatchQuadResult(ifIndex, static_cast<int>(quadruples_.size()));

        if (!parseStatement()) {
            exitRule("if语句", false);
            return false;
        }

        int ieIndex = emitQuad("ie", "", "", "");
        backpatchQuadResult(elIndex, ieIndex);
    } else {
        int ieIndex = emitQuad("ie", "", "", "");
        backpatchQuadResult(ifIndex, ieIndex);
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

    /* SEMANTIC: while 起始标记 */
    int whIndex = emitQuad("wh", "", "", "");

    if (!parseExpression({"do"})) {
        exitRule("while语句", false);
        return false;
    }

    if (!matchKeyword("do")) {
        error("缺少关键字 'do'");
        exitRule("while语句", false);
        return false;
    }

    /* SEMANTIC: do 记录条件结果，假出口待回填到 we */
    int doIndex = emitQuad("do", lastExpressionPlace_, "", "?");

    if (!parseStatement()) {
        exitRule("while语句", false);
        return false;
    }

    /* SEMANTIC: while 结束标记，并回跳到 wh */
    int weIndex = emitQuad("we", "", "", to_string(whIndex));
    backpatchQuadResult(doIndex, weIndex);

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

    // 处理数组下标 / 字段访问后缀（arr[1]、pt.x 等）
    // 合并到 lastStatementIdentifier_ 中，不走函数调用路径
    while (checkDelimiter("[") || checkDelimiter(".")) {
        if (checkDelimiter("[")) {
            matchDelimiter("[");
            lastStatementIdentifier_ += "[";
            // 解析下标表达式 token 串（简单扫描到匹配的 ]）
            int bracketDepth = 1;
            while (bracketDepth > 0 && !isAtEnd()) {
                Token ct = current();
                if (ct.type == "DELIMITER") {
                    int idx = -1;
                    try { size_t p = 0; idx = stoi(ct.value, &p); if (p != ct.value.size()) idx = -1; } catch (...) {}
                    if (idx >= 0 && idx < static_cast<int>(ctx.delimiterTable.size())) {
                        const string& d = ctx.delimiterTable[idx];
                        if (d == "[") bracketDepth++;
                        if (d == "]") bracketDepth--;
                    }
                }
                lastStatementIdentifier_ += tokenToString(advance());
            }
        } else if (checkDelimiter(".")) {
            matchDelimiter(".");
            lastStatementIdentifier_ += ".";
            if (checkId()) {
                lastStatementIdentifier_ += tokenToString(advance());
            }
        }
    }

    // 语义验证：检查赋值目标中的数组下标越界 & 字段存在性
    {
        const string& combined = lastStatementIdentifier_;
        size_t baseEnd = combined.find_first_of("[.");
        string currentBase = (baseEnd == string::npos)
                             ? combined
                             : combined.substr(0, baseEnd);
        size_t pos = baseEnd;
        while (pos != string::npos && pos < combined.size()) {
            if (combined[pos] == '[') {
                size_t close = combined.find(']', pos);
                if (close != string::npos) {
                    string indexStr = combined.substr(pos + 1, close - pos - 1);
                    validateSubscriptBound(currentBase, indexStr);
                    pos = close + 1;
                } else break;
            } else if (combined[pos] == '.') {
                size_t nextDot = combined.find('.', pos + 1);
                size_t nextBrk = combined.find('[', pos + 1);
                size_t fieldEnd = string::npos;
                if (nextDot != string::npos && nextBrk != string::npos)
                    fieldEnd = (nextDot < nextBrk) ? nextDot : nextBrk;
                else if (nextDot != string::npos)
                    fieldEnd = nextDot;
                else if (nextBrk != string::npos)
                    fieldEnd = nextBrk;
                string fieldName = (fieldEnd == string::npos)
                                   ? combined.substr(pos + 1)
                                   : combined.substr(pos + 1, fieldEnd - pos - 1);
                validateFieldExists(currentBase, fieldName);
                currentBase = (fieldEnd == string::npos)
                              ? combined
                              : combined.substr(0, fieldEnd);
                pos = fieldEnd;
            } else break;
        }
    }

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

        /* SEMANTIC: 生成赋值四元式。
           函数体内对函数名的赋值改写为对 _result$func 的赋值 */
        {
            string dest = lastStatementIdentifier_;
            if (currentRoutineSymbolIndex_ >= 0 &&
                currentRoutineSymbolIndex_ < static_cast<int>(ctx.synbl.size())) {
                const SynblItem& si = ctx.synbl[currentRoutineSymbolIndex_];
                if (dest == si.name && si.cat == "f") {
                    dest = newScopedName("_result");  // 当前作用域下的 _result, 即 _result$func
                }
            }
            emitQuad(":=", lastExpressionPlace_, "", dest);
        }
    } else {
        // 过程调用（含无参调用）
        // 收集实参
        pendingActualArgs_.clear();
        if (!parseCallSuffix()) {
            exitRule("赋值或调用语句", false);
            return false;
        }

        // 生成 call 调用序列
        string routineCallName = lastStatementIdentifier_;
        // 入口标签
        string entryLabel = routineCallName + "_entry";
        for (const string& seg : scopePath_) entryLabel += "$" + seg;

        // 实参→形参
        for (size_t i = 0; i < pendingActualArgs_.size(); ++i) {
            string paramName = "_p" + to_string(i);
            for (const string& seg : scopePath_) paramName += "$" + seg;
            paramName += "$" + routineCallName;
            emitQuad(":=", pendingActualArgs_[i], "", paramName);
        }
        // call：返回后执行下一条四元式，无需显式返回标签
        emitQuad("call", entryLabel, "", "");
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
    int bracketDepth = 0;   // 数组下标的括号深度

    while (scanPos < tokens_.size()) {
        const Token& t = tokens_[scanPos];

        if (parenDepth == 0 && bracketDepth == 0 && tokenMatchesStop(t, stopTokens)) {
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
                } else if (d == "[") {
                    bracketDepth++;
                } else if (d == "]") {
                    if (bracketDepth > 0) {
                        bracketDepth--;
                    } else {
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
    // 预处理：合并 ID [ expr ] 为单个语义值（如 arr[1]），支持嵌套 arr[i][j]
    vector<SemanticValue> exprValues;
    exprValues.reserve(scanPos - beginPos);

    for (size_t i = beginPos; i < scanPos; ) {
        const Token& t = tokens_[i];

        // 检查 ID 后是否跟着 [ 或 .，如果是则合并整个 ID[...] 或 ID.id 为一个值
        if (t.type == "ID" && i + 1 < scanPos) {
            const Token& next = tokens_[i + 1];
            bool isSuffix = false;
            if (next.type == "DELIMITER") {
                int idx = -1;
                try { size_t p = 0; idx = stoi(next.value, &p); if (p != next.value.size()) idx = -1; } catch (...) {}
                if (idx >= 0 && idx < static_cast<int>(ctx.delimiterTable.size())) {
                    const string& d = ctx.delimiterTable[idx];
                    if (d == "[" || d == ".") isSuffix = true;
                }
            }

            if (isSuffix) {
                // 合并 ID + [ + 内容 + ] 为单个语义值
                string combinedText;
                {
                    int synblIdx = -1;
                    try { size_t p = 0; synblIdx = stoi(t.value, &p); if (p != t.value.size()) synblIdx = -1; } catch (...) {}
                    if (synblIdx >= 0 && synblIdx < static_cast<int>(ctx.synbl.size()))
                        combinedText = ctx.synbl[synblIdx].name;
                    else
                        combinedText = "?";
                }

                // 跳过 ID
                i++;

                // 处理所有连续的 [ expr ] 和 . id 后缀
                while (i < scanPos) {
                    const Token& bracket = tokens_[i];
                    if (bracket.type == "DELIMITER") {
                        int idx = -1;
                        try { size_t p = 0; idx = stoi(bracket.value, &p); if (p != bracket.value.size()) idx = -1; } catch (...) {}

                        if (idx >= 0 && idx < static_cast<int>(ctx.delimiterTable.size())) {
                            const string& d = ctx.delimiterTable[idx];

                            if (d == "[") {
                                // 收集 [...] 内部的所有 token
                                combinedText += "[";
                                i++; // 跳过 [
                                int bracketDepth2 = 1;
                                while (i < scanPos && bracketDepth2 > 0) {
                                    const Token& inner = tokens_[i];
                                    if (inner.type == "DELIMITER") {
                                        int iidx = -1;
                                        try { size_t p2 = 0; iidx = stoi(inner.value, &p2); if (p2 != inner.value.size()) iidx = -1; } catch (...) {}
                                        if (iidx >= 0 && iidx < static_cast<int>(ctx.delimiterTable.size())) {
                                            const string& dd = ctx.delimiterTable[iidx];
                                            if (dd == "[") bracketDepth2++;
                                            if (dd == "]") bracketDepth2--;
                                        }
                                    }
                                    combinedText += tokenToString(inner);
                                    i++;
                                }
                                // 不额外加 ]，因为已经在循环内通过 tokenToString 追加
                                continue;
                            }

                            if (d == ".") {
                                // 字段访问：.id
                                combinedText += ".";
                                i++; // 跳过 .
                                if (i < scanPos) {
                                    combinedText += tokenToString(tokens_[i]);
                                    i++; // 跳过字段名
                                }
                                continue;
                            }
                        }
                    }
                    break; // 不是 [ 或 .，停止
                }

                // 语义验证：检查数组下标越界 & 字段存在性
                {
                    // 解析 combinedText，依次验证 [...].id 后缀
                    // 先找到基础变量名（到第一个 [ 或 . 为止）
                    size_t baseEnd = combinedText.find_first_of("[.");
                    string currentBase = (baseEnd == string::npos)
                                         ? combinedText
                                         : combinedText.substr(0, baseEnd);
                    size_t pos = baseEnd;
                    while (pos != string::npos && pos < combinedText.size()) {
                        if (combinedText[pos] == '[') {
                            size_t close = combinedText.find(']', pos);
                            if (close != string::npos) {
                                string indexStr = combinedText.substr(pos + 1, close - pos - 1);
                                validateSubscriptBound(currentBase, indexStr);
                                pos = close + 1;
                            } else {
                                break;
                            }
                        } else if (combinedText[pos] == '.') {
                            size_t nextDot = combinedText.find('.', pos + 1);
                            size_t nextBrk = combinedText.find('[', pos + 1);
                            size_t fieldEnd = string::npos;
                            if (nextDot != string::npos && nextBrk != string::npos)
                                fieldEnd = (nextDot < nextBrk) ? nextDot : nextBrk;
                            else if (nextDot != string::npos)
                                fieldEnd = nextDot;
                            else if (nextBrk != string::npos)
                                fieldEnd = nextBrk;
                            string fieldName = (fieldEnd == string::npos)
                                               ? combinedText.substr(pos + 1)
                                               : combinedText.substr(pos + 1, fieldEnd - pos - 1);
                            validateFieldExists(currentBase, fieldName);
                            // 更新 currentBase 为完整路径，供下一次 . 验证
                            currentBase = (fieldEnd == string::npos)
                                          ? combinedText
                                          : combinedText.substr(0, fieldEnd);
                            pos = fieldEnd;
                        } else {
                            break;
                        }
                    }
                }

                SemanticValue val;
                val.symbol = "id";
                val.text = combinedText;
                val.line = t.line;
                val.typ = t.type == "ID" ? ([]() -> int {
                    // 尝试获取 ID 的类型
                    return -1;
                })() : -1;
                exprValues.push_back(val);
                continue;
            }
        }

        // 普通 token
        SemanticValue val;
        if (!tokenToExpressionValue(t, val)) {
            error("表达式中出现非法记号: " + tokenToString(t));
            exitRule("表达式", false);
            return false;
        }
        exprValues.push_back(val);
        i++;
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
            // 1/3/5/7/9/18/21/23~27: 语义透传；28 为 (Expr) 透传中间项
            case 1: case 3: case 5: case 7: case 9:
            case 18: case 21: case 23: case 24: case 25:
            case 26: case 27:
                passThrough(0);
                break;
            case 28: // Factor -> ( Expr )，应透传 rhs[1] 而不是 rhs[0] 的 "("
                passThrough(1);
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
                    // 函数调用在表达式中: id(args)
                    string callName = rhs[0].text;
                    int callResultTyp = rhs[0].typ;
                    if (callResultTyp < 0) callResultTyp = ensureBuiltinType("i");
                    string t = newTemp(callResultTyp);

                    // 入口标签
                    string entryLabel = callName + "_entry";
                    for (const string& seg : scopePath_) entryLabel += "$" + seg;

                    // 传参
                    for (size_t i = 0; i < rhs[1].args.size(); ++i) {
                        string paramName = "_p" + to_string(i);
                        for (const string& seg : scopePath_) paramName += "$" + seg;
                        paramName += "$" + callName;
                        emitQuad(":=", rhs[1].args[i], "", paramName);
                    }
                    // call：返回后执行下一条四元式
                    emitQuad("call", entryLabel, "", "");
                    // 取返回值
                    emitQuad(":=", calleeResultName(callName), "", t);

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
                string zero = (unaryTyp == realTyp) ? "0.0" : "0";
                emitQuad("-", zero, rhs[1].text, t);
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
// §12 类型引用
//     〈类型引用〉 → integer | real | char | boolean | string | ID
// ============================================================

bool Parser::parseType() {
    enterRule("类型引用");

    if (matchKeyword("integer")) {
        lastParsedTypeCode_ = "i";
        lastParsedTypeIndex_ = ensureBuiltinType(lastParsedTypeCode_);
        exitRule("类型引用", true);
        return true;
    }
    if (matchKeyword("real")) {
        lastParsedTypeCode_ = "r";
        lastParsedTypeIndex_ = ensureBuiltinType(lastParsedTypeCode_);
        exitRule("类型引用", true);
        return true;
    }
    if (matchKeyword("char")) {
        lastParsedTypeCode_ = "c";
        lastParsedTypeIndex_ = ensureBuiltinType(lastParsedTypeCode_);
        exitRule("类型引用", true);
        return true;
    }
    if (matchKeyword("boolean")) {
        lastParsedTypeCode_ = "b";
        lastParsedTypeIndex_ = ensureBuiltinType(lastParsedTypeCode_);
        exitRule("类型引用", true);
        return true;
    }
    if (matchKeyword("string")) {
        lastParsedTypeCode_ = "s";
        lastParsedTypeIndex_ = ensureBuiltinType(lastParsedTypeCode_);
        exitRule("类型引用", true);
        return true;
    }

    // 用户自定义类型名（ID）
    if (checkId()) {
        Token t = current();
        int idx = stoi(t.value);
        if (idx >= 0 && idx < static_cast<int>(ctx.synbl.size())) {
            const SynblItem& si = ctx.synbl[idx];
            if (si.cat == "t") {
                // 自定义类型：typ 字段指向 TYPEL 条目
                lastParsedTypeCode_ = "";  // 非基本类型
                lastParsedTypeIndex_ = si.typ;
                logMatch(advance());
                exitRule("类型引用", true);
                return true;
            }
            // cat != 't'，不是类型名
        }
        error("标识符不是已定义的类型名");
        exitRule("类型引用", false);
        return false;
    }

    error("缺少类型（integer / real / char / boolean / string）或自定义类型名");
    exitRule("类型引用", false);
    return false;
}

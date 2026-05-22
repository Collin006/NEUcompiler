#include <iostream>
#include <vector>
#include <string>
#include <map>
#include <set>
#include <algorithm>
#include <sstream>
#include <cassert>

#include "youhua.h"

using namespace std;

// ==================== 优化工具函数 ====================
namespace Utils
{
    bool isNumber(const std::string &s)
    {
        if (s.empty())
            return false;
        if (s[0] == '-' && s.length() > 1 && isdigit(s[1]))
            return true;
        return isdigit(s[0]);
    }

    bool isIntegerIndex(const std::string &s)
    {
        if (s.empty())
            return false;
        size_t start = 0;
        if (s[0] == '-')
        {
            if (s.size() == 1)
                return false;
            start = 1;
        }
        for (size_t i = start; i < s.size(); i++)
        {
            if (!isdigit(static_cast<unsigned char>(s[i])))
                return false;
        }
        return true;
    }

    bool isVar(const std::string &s)
    {
        if (s.empty())
            return false;
        if (isNumber(s))
            return false;
        return isalpha(s[0]) || s[0] == '_';
    }

    std::string compute(const std::string &op, const std::string &v1, const std::string &v2)
    {
        if (!isNumber(v1))
            return "";
        double a = stod(v1);
        double b = 0;
        if (op != "!")
        {
            if (!isNumber(v2))
                return "";
            b = stod(v2);
        }

        double r = 0;
        if (op == "+")
            r = a + b;
        else if (op == "-")
            r = a - b;
        else if (op == "*")
            r = a * b;
        else if (op == "/")
        {
            if (b == 0)
                return "";
            r = a / b;
        }
        else if (op == ">")
            r = a > b ? 1 : 0;
        else if (op == "<")
            r = a < b ? 1 : 0;
        else if (op == "=")
            r = a == b ? 1 : 0;
        else if (op == "<=")
            r = a <= b ? 1 : 0;
        else if (op == ">=")
            r = a >= b ? 1 : 0;
        else if (op == "&&")
            r = (a != 0 && b != 0) ? 1 : 0;
        else if (op == "||")
            r = (a != 0 || b != 0) ? 1 : 0;
        else if (op == "!")
            r = (a == 0) ? 1 : 0;
        else
            return "";

        return (r == (int)r) ? to_string((int)r) : to_string(r);
    }

    std::set<std::string> getUse(const FourTuple &t)
    {
        std::set<std::string> s;
        if (isVar(t.first_value))
            s.insert(t.first_value);
        if (isVar(t.second_value))
            s.insert(t.second_value);
        return s;
    }
}

// ==================== 1. 常量折叠 ====================
std::vector<FourTuple> constantFolding(const std::vector<FourTuple> &input)
{
    std::vector<FourTuple> output;
    int count = 0;

    for (const auto &t : input)
    {
        std::string op = t.operator_str;

        if (op == "+" || op == "-" || op == "*" || op == "/" ||
            op == ">" || op == "<" || op == "=" ||
            op == "<=" || op == ">=" || op == "&&" || op == "||" || op == "!")
        {
            std::string r = Utils::compute(op, t.first_value, t.second_value);
            if (!r.empty())
            {
                std::cout << "常量折叠: (" << op << ", " << t.first_value
                          << ", " << t.second_value << ", " << t.dist
                          << ") -> " << t.dist << " := " << r << std::endl;
                FourTuple new_t;
                new_t.operator_str = ":=";
                new_t.first_value = r;
                new_t.second_value = "";
                new_t.dist = t.dist;
                output.push_back(new_t);
                count++;
                continue;
            }
        }

        output.push_back(t);
    }

    std::cout << "常量折叠: " << count << " 条" << std::endl;
    return output;
}

// ==================== 2. 代数简化 ====================
std::vector<FourTuple> algebraicSimplify(const std::vector<FourTuple> &input)
{
    std::vector<FourTuple> output;
    int count = 0;

    for (auto t : input)
    {
        std::string op = t.operator_str;
        bool simp = false;

        if (op == "+" && t.second_value == "0")
        {
            std::cout << "代数简化: x+0 -> x" << std::endl;
            t.operator_str = ":=";
            t.first_value = t.first_value;
            t.second_value = "";
            simp = true;
        }
        else if (op == "+" && t.first_value == "0")
        {
            std::cout << "代数简化: 0+x -> x" << std::endl;
            t.operator_str = ":=";
            t.first_value = t.second_value;
            t.second_value = "";
            simp = true;
        }
        else if (op == "-" && t.second_value == "0")
        {
            std::cout << "代数简化: x-0 -> x" << std::endl;
            t.operator_str = ":=";
            t.first_value = t.first_value;
            t.second_value = "";
            simp = true;
        }
        else if (op == "*" && t.second_value == "1")
        {
            std::cout << "代数简化: x*1 -> x" << std::endl;
            t.operator_str = ":=";
            t.first_value = t.first_value;
            t.second_value = "";
            simp = true;
        }
        else if (op == "*" && t.first_value == "1")
        {
            std::cout << "代数简化: 1*x -> x" << std::endl;
            t.operator_str = ":=";
            t.first_value = t.second_value;
            t.second_value = "";
            simp = true;
        }
        else if (op == "*" && (t.first_value == "0" || t.second_value == "0"))
        {
            std::cout << "代数简化: x*0 -> 0" << std::endl;
            t.operator_str = ":=";
            t.first_value = "0";
            t.second_value = "";
            simp = true;
        }
        else if (op == "/" && t.second_value == "1")
        {
            std::cout << "代数简化: x/1 -> x" << std::endl;
            t.operator_str = ":=";
            t.first_value = t.first_value;
            t.second_value = "";
            simp = true;
        }

        if (simp)
            count++;
        output.push_back(t);
    }

    std::cout << "代数简化: " << count << " 条" << std::endl;
    return output;
}

// ==================== 3. 公共子表达式消除 ====================
std::vector<FourTuple> commonSubexpressionElimination(const std::vector<FourTuple> &input)
{
    std::map<std::string, std::string> available;
    std::vector<FourTuple> output;
    int count = 0;

    for (const auto &t : input)
    {
        std::string op = t.operator_str;

        if (op == "+" || op == "-" || op == "*" || op == "/" ||
            op == ">" || op == "<" || op == "=" || op == "<=" || op == ">=")
        {
            std::string key = op + "_" + t.first_value + "_" + t.second_value;

            if (available.find(key) != available.end())
            {
                std::cout << "公共子表达式消除: " << t.dist
                          << " 重用 " << available[key] << std::endl;
                FourTuple new_t;
                new_t.operator_str = ":=";
                new_t.first_value = available[key];
                new_t.second_value = "";
                new_t.dist = t.dist;
                output.push_back(new_t);
                count++;
                continue;
            }

            available[key] = t.dist;
        }

        // 赋值语句可能使表达式失效
        if (op == ":=" && Utils::isVar(t.dist))
        {
            std::vector<std::string> to_remove;
            for (const auto &p : available)
            {
                if (p.first.find(t.dist) != std::string::npos)
                {
                    to_remove.push_back(p.first);
                }
            }
            for (const auto &k : to_remove)
            {
                available.erase(k);
            }
        }

        output.push_back(t);
    }

    std::cout << "公共子表达式消除: " << count << " 个" << std::endl;
    return output;
}

// ==================== 4. 基本块划分 ====================
std::vector<BasicBlock> buildBasicBlocks(const std::vector<FourTuple> &tuples)
{
    std::set<int> leaders;
    if (tuples.empty())
        return {};

    leaders.insert(0);

    std::map<std::string, int> label2idx;
    for (int i = 0; i < static_cast<int>(tuples.size()); i++)
    {
        if (tuples[i].operator_str == "lb" && !tuples[i].first_value.empty())
        {
            label2idx[tuples[i].first_value] = i;
            leaders.insert(i);
        }
    }

    auto addTargetLeader = [&](const std::string &targetText)
    {
        if (targetText.empty())
            return;

        if (Utils::isIntegerIndex(targetText))
        {
            int target = std::stoi(targetText);
            if (target >= 0 && target < static_cast<int>(tuples.size()))
                leaders.insert(target);
        }
        else if (label2idx.count(targetText))
        {
            leaders.insert(label2idx[targetText]);
        }
    };

    for (int i = 0; i < static_cast<int>(tuples.size()); i++)
    {
        std::string op = tuples[i].operator_str;

        if (op == "goto" || op == "if" || op == "do" || op == "we" ||
            op == "el" || op == "ie" || op == "ret")
        {
            if (i + 1 < static_cast<int>(tuples.size()))
                leaders.insert(i + 1);
        }

        if (op == "goto" || op == "if" || op == "do" || op == "we" || op == "el")
        {
            addTargetLeader(tuples[i].dist);
        }
    }

    std::vector<int> leader_list(leaders.begin(), leaders.end());
    std::sort(leader_list.begin(), leader_list.end());

    std::vector<BasicBlock> blocks;
    std::map<int, int> idx2block;

    for (int i = 0; i < static_cast<int>(leader_list.size()); i++)
    {
        BasicBlock bb;
        bb.id = i;
        int start = leader_list[i];
        int end = (i + 1 < static_cast<int>(leader_list.size()))
                      ? leader_list[i + 1] - 1
                      : static_cast<int>(tuples.size()) - 1;
        bb.start_index = start;
        bb.end_index = end;

        for (int j = start; j <= end; j++)
        {
            bb.tuples.push_back(tuples[j]);
            idx2block[j] = i;
        }
        blocks.push_back(bb);
    }

    auto connectBlocks = [&](int from, int targetIndex)
    {
        if (from < 0 || from >= static_cast<int>(blocks.size()))
            return;
        if (!idx2block.count(targetIndex))
            return;

        int to = idx2block[targetIndex];
        blocks[from].next.insert(to);
        blocks[to].prev.insert(from);
    };

    auto connectTarget = [&](int from, const std::string &targetText)
    {
        if (targetText.empty())
            return;

        if (Utils::isIntegerIndex(targetText))
        {
            int target = std::stoi(targetText);
            if (target >= 0 && target < static_cast<int>(tuples.size()))
                connectBlocks(from, target);
        }
        else if (label2idx.count(targetText))
        {
            connectBlocks(from, label2idx[targetText]);
        }
    };

    for (int i = 0; i < static_cast<int>(blocks.size()); i++)
    {
        if (blocks[i].tuples.empty())
            continue;

        const FourTuple &last = blocks[i].tuples.back();
        std::string last_op = last.operator_str;

        if (last_op == "goto" || last_op == "we" || last_op == "el")
        {
            connectTarget(i, last.dist);
        }
        else if (last_op == "if" || last_op == "do")
        {
            connectTarget(i, last.dist);
            if (i + 1 < static_cast<int>(blocks.size()))
            {
                blocks[i].next.insert(i + 1);
                blocks[i + 1].prev.insert(i);
            }
        }
        else if (last_op == "ret")
        {
            continue;
        }
        else
        {
            if (i + 1 < static_cast<int>(blocks.size()))
            {
                blocks[i].next.insert(i + 1);
                blocks[i + 1].prev.insert(i);
            }
        }
    }

    return blocks;
}

// ==================== 5. 全局死代码消除（增强版） ====================
static std::vector<FourTuple>
globalDeadCodeElimination(const std::vector<FourTuple> &input,
                          const std::map<int, std::set<std::string>> &funcExitUses,
                          const std::map<int, std::set<std::string>> &callParamUses)
{
    int n = input.size();
    std::vector<std::set<std::string>> live(n + 1);
    std::set<std::string> cur;

    for (int i = n - 1; i >= 0; i--)
    {
        live[i] = cur;

        if (input[i].operator_str == "call")
        {
            auto it = callParamUses.find(i);
            if (it != callParamUses.end())
                cur.insert(it->second.begin(), it->second.end());
        }

        if (input[i].operator_str == "ret")
        {
            auto it = funcExitUses.find(i);
            if (it != funcExitUses.end())
                cur.insert(it->second.begin(), it->second.end());
        }

        if (Utils::isVar(input[i].dist))
            cur.erase(input[i].dist);

        std::set<std::string> use = Utils::getUse(input[i]);
        cur.insert(use.begin(), use.end());
    }

    std::vector<FourTuple> output;
    int count = 0;
    for (int i = 0; i < n; i++)
    {
        if (input[i].operator_str == ":=" &&
            Utils::isVar(input[i].dist) &&
            live[i].find(input[i].dist) == live[i].end())
        {
            std::cout << "删除死代码: " << input[i].dist
                      << " := " << input[i].first_value << std::endl;
            count++;
            continue;
        }
        output.push_back(input[i]);
    }
    std::cout << "死代码消除: " << count << " 条" << std::endl;
    return output;
}

// ==================== 辅助：收集函数出口活跃变量 ====================
static std::map<int, std::set<std::string>>
buildFuncExitUses(const std::vector<FourTuple> &tuples)
{
    std::map<int, std::set<std::string>> ret2uses;
    struct FuncInfo {
        int start;
        std::set<std::string> defs;
    };
    std::vector<FuncInfo> funcs;
    int n = tuples.size();
    for (int i = 0; i < n; i++)
    {
        if (tuples[i].operator_str == "lb")
        {
            FuncInfo f;
            f.start = i;
            funcs.push_back(f);
        }
        else if (!funcs.empty() && tuples[i].operator_str != "lb")
        {
            if (tuples[i].operator_str == ":=" && Utils::isVar(tuples[i].dist))
                funcs.back().defs.insert(tuples[i].dist);
        }
    }

    for (int i = 0; i < n; i++)
    {
        if (tuples[i].operator_str == "ret")
        {
            int funcIdx = -1;
            for (int k = funcs.size() - 1; k >= 0; k--)
            {
                if (funcs[k].start < i)
                {
                    funcIdx = k;
                    break;
                }
            }
            if (funcIdx != -1)
                ret2uses[i] = funcs[funcIdx].defs;
        }
    }
    return ret2uses;
}

// ==================== 辅助：收集 call 使用的参数变量 ====================
static std::map<int, std::set<std::string>>
buildCallParamUses(const std::vector<FourTuple> &tuples)
{
    std::map<int, std::set<std::string>> call2uses;
    for (int i = 0; i < (int)tuples.size(); i++)
    {
        if (tuples[i].operator_str == "call")
        {
            std::string funcName = tuples[i].first_value;
            std::string funcBase;
            size_t pos = funcName.rfind("_entry");
            if (pos != std::string::npos)
                funcBase = funcName.substr(0, pos);
            else
                funcBase = funcName;

            std::set<std::string> usedParams;
            for (int j = i - 1; j >= 0; j--)
            {
                if (tuples[j].operator_str == "call" ||
                    tuples[j].operator_str == "lb" ||
                    tuples[j].operator_str == "ret")
                    break;
                if (tuples[j].operator_str == ":=")
                {
                    const std::string &dest = tuples[j].dist;
                    if (dest.find("_p") == 0 && dest.find("$" + funcBase) != std::string::npos)
                        usedParams.insert(dest);
                }
            }
            if (!usedParams.empty())
                call2uses[i] = usedParams;
        }
    }
    return call2uses;
}

// ==================== 跳转目标重映射 ====================
static std::vector<FourTuple>
remapJumpTargets(const std::vector<FourTuple> &input,
                 const std::vector<int> &origIdx)
{
    std::map<int, int> old2new;
    for (int i = 0; i < (int)input.size(); i++)
    {
        if (origIdx[i] >= 0)
            old2new[origIdx[i]] = i;
    }

    std::vector<FourTuple> output = input;
    for (auto &t : output)
    {
        std::string op = t.operator_str;
        if (op == "if" || op == "do" || op == "we" || op == "el" || op == "goto")
        {
            if (!t.dist.empty() && Utils::isIntegerIndex(t.dist))
            {
                int oldTarget = std::stoi(t.dist);
                auto it = old2new.find(oldTarget);
                if (it != old2new.end())
                    t.dist = std::to_string(it->second);
                else
                    std::cerr << "警告：跳转目标 " << oldTarget << " 在优化后不存在！" << std::endl;
            }
        }
    }
    return output;
}

// ==================== 基本块局部优化（不含死代码消除） ====================
static std::pair<std::vector<FourTuple>, std::vector<int>>
optimizeBasicBlockWithOrig(const BasicBlock &block)
{
    std::vector<FourTuple> result = block.tuples;
    std::vector<int> orig(result.size());
    for (int i = 0; i < (int)result.size(); i++)
        orig[i] = block.start_index + i;

    auto applyOpt = [&](std::vector<FourTuple> (*opt)(const std::vector<FourTuple>&)) {
        std::vector<FourTuple> newRes = opt(result);
        std::vector<int> newOrig;
        if (newRes.size() == result.size())
            newOrig = orig;
        else
            newOrig.assign(newRes.size(), -1);
        result = newRes;
        orig = newOrig;
    };

    applyOpt(constantFolding);
    applyOpt(algebraicSimplify);
    applyOpt(commonSubexpressionElimination);
    applyOpt(constantFolding);

    return {result, orig};
}

// ==================== 优化管理器 ====================
std::vector<FourTuple> optimize(const std::vector<FourTuple> &input)
{
    std::cout << "\n========== 开始优化 ==========" << std::endl;
    std::cout << "原始四元式: " << input.size() << " 条" << std::endl;

    std::vector<BasicBlock> blocks = buildBasicBlocks(input);
    std::cout << "基本块数量: " << blocks.size() << " 个" << std::endl;

    std::vector<FourTuple> afterLocal;
    std::vector<int> origIdx;
    for (const auto &block : blocks)
    {
        std::cout << "\n----- 局部优化基本块 B" << block.id << " -----" << std::endl;
        auto [optBlock, blockOrig] = optimizeBasicBlockWithOrig(block);
        afterLocal.insert(afterLocal.end(), optBlock.begin(), optBlock.end());
        origIdx.insert(origIdx.end(), blockOrig.begin(), blockOrig.end());
    }

    auto funcExitUses = buildFuncExitUses(afterLocal);
    auto callParamUses = buildCallParamUses(afterLocal);

    std::cout << "\n----- 全局死代码消除 -----" << std::endl;
    std::vector<FourTuple> afterDead = globalDeadCodeElimination(afterLocal, funcExitUses, callParamUses);

    std::vector<int> newOrigIdx;
    int deadIdx = 0;
    for (int i = 0; i < (int)afterLocal.size() && deadIdx < (int)afterDead.size(); i++)
    {
        if (deadIdx < (int)afterDead.size() &&
            afterLocal[i].operator_str == afterDead[deadIdx].operator_str &&
            afterLocal[i].first_value == afterDead[deadIdx].first_value &&
            afterLocal[i].second_value == afterDead[deadIdx].second_value &&
            afterLocal[i].dist == afterDead[deadIdx].dist)
        {
            newOrigIdx.push_back(origIdx[i]);
            deadIdx++;
        }
    }
    if (deadIdx != (int)afterDead.size())
        std::cerr << "错误：死代码消除后指令匹配失败！" << std::endl;

    std::vector<FourTuple> final = remapJumpTargets(afterDead, newOrigIdx);

    std::cout << "优化后四元式: " << final.size() << " 条" << std::endl;
    std::cout << "========== 优化完成 ==========" << std::endl;
    return final;
}

// ==================== 辅助函数：打印 ====================
void printTuples(const std::vector<FourTuple> &tuples)
{
    for (size_t i = 0; i < tuples.size(); i++)
    {
        std::cout << i << ": (" << tuples[i].operator_str
                  << ", " << tuples[i].first_value
                  << ", " << tuples[i].second_value
                  << ", " << tuples[i].dist << ")" << std::endl;
    }
}

void printBlocks(const std::vector<BasicBlock> &blocks)
{
    std::cout << "\n===== 基本块结构 =====" << std::endl;
    for (const auto &bb : blocks)
    {
        std::cout << "B" << bb.id << " 前驱:{";
        for (int p : bb.prev)
            std::cout << "B" << p << " ";
        std::cout << "} 后继:{";
        for (int n : bb.next)
            std::cout << "B" << n << " ";
        std::cout << "}" << std::endl;
        for (const auto &t : bb.tuples)
        {
            std::cout << "  (" << t.operator_str << ", "
                      << t.first_value << ", " << t.second_value
                      << ", " << t.dist << ")" << std::endl;
        }
    }
}
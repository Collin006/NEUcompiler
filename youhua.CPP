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
    bool isNumber(const std::string &s) { /* 保持不变 */ }
    bool isIntegerIndex(const std::string &s) { /* 保持不变 */ }
    bool isVar(const std::string &s) { /* 保持不变 */ }
    std::string compute(const std::string &op, const std::string &v1, const std::string &v2) { /* 保持不变 */ }
    std::set<std::string> getUse(const FourTuple &t) { /* 保持不变 */ }
}

// ==================== 1. 常量折叠 ====================
// 不变
std::vector<FourTuple> constantFolding(const std::vector<FourTuple> &input) { /* 保持不变 */ }

// ==================== 2. 代数简化 ====================
// 不变
std::vector<FourTuple> algebraicSimplify(const std::vector<FourTuple> &input) { /* 保持不变 */ }

// ==================== 3. 公共子表达式消除 ====================
// 不变
std::vector<FourTuple> commonSubexpressionElimination(const std::vector<FourTuple> &input) { /* 保持不变 */ }

// ==================== 4. 基本块划分 ====================
// 不变，沿用你原来的版本
std::vector<BasicBlock> buildBasicBlocks(const std::vector<FourTuple> &tuples) { /* 保持不变 */ }

// ==================== 5. 死代码消除（增强版，全局，处理函数出口） ====================
// 新增参数：
//   funcExitUses : 记录每个 ret 指令在函数出口时必须活跃的变量集合
//   callParamUses: 记录每个 call 指令所使用的参数变量集合
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

        // 处理 call：call 会隐式使用实参变量
        if (input[i].operator_str == "call")
        {
            auto it = callParamUses.find(i);
            if (it != callParamUses.end())
                cur.insert(it->second.begin(), it->second.end());
        }

        // 处理 ret：ret 出口处需要活跃的变量（返回值、引用参数等）
        if (input[i].operator_str == "ret")
        {
            auto it = funcExitUses.find(i);
            if (it != funcExitUses.end())
                cur.insert(it->second.begin(), it->second.end());
        }

        // 处理定义
        if (Utils::isVar(input[i].dist))
            cur.erase(input[i].dist);

        // 处理使用
        std::set<std::string> use = Utils::getUse(input[i]);
        cur.insert(use.begin(), use.end());
    }

    // 删除无用赋值
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
// 从一组四元式中识别出所有函数（lb…ret区间），并为每个 ret 生成它必须保护的变量集合
static std::map<int, std::set<std::string>>
buildFuncExitUses(const std::vector<FourTuple> &tuples)
{
    std::map<int, std::set<std::string>> ret2uses;
    // 第一步：划分函数边界，收集每个函数体内所有的赋值目标
    struct FuncInfo {
        int start; // lb 所在的索引
        std::set<std::string> defs; // 函数内所有被赋值的变量
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
            // 收集定义
            if (tuples[i].operator_str == ":=" && Utils::isVar(tuples[i].dist))
                funcs.back().defs.insert(tuples[i].dist);
        }
    }

    // 第二步：为每个 ret 找到它所属的函数，并赋予该函数的 defs
    for (int i = 0; i < n; i++)
    {
        if (tuples[i].operator_str == "ret")
        {
            // 找到包含此 ret 的函数（lb 在它之前，且是最近的 lb）
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
// 扫描序列，对每个 call，向前查找最近的若干条实参传递指令
static std::map<int, std::set<std::string>>
buildCallParamUses(const std::vector<FourTuple> &tuples)
{
    std::map<int, std::set<std::string>> call2uses;
    for (int i = 0; i < (int)tuples.size(); i++)
    {
        if (tuples[i].operator_str == "call")
        {
            std::string funcName = tuples[i].first_value; // 例如 max_entry
            // 从 call 向前收集以 "$funcBase" 结尾的参数变量
            // 约定：实参临时变量为 _p0$funcBase, _p1$funcBase …
            std::string funcBase;
            // 去掉 _entry 后缀（如果存在）
            size_t pos = funcName.rfind("_entry");
            if (pos != std::string::npos)
                funcBase = funcName.substr(0, pos);
            else
                funcBase = funcName;

            std::set<std::string> usedParams;
            // 向前搜索，直到遇到其他 call、lb 或函数开头
            for (int j = i - 1; j >= 0; j--)
            {
                if (tuples[j].operator_str == "call" ||
                    tuples[j].operator_str == "lb" ||
                    tuples[j].operator_str == "ret")
                    break;
                if (tuples[j].operator_str == ":=")
                {
                    const std::string &dest = tuples[j].dist;
                    // 形如 _p0$xxx, _p1$xxx
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
                 const std::vector<int> &origIdx) // origIdx[i] 表示 input[i] 在原始序列中的索引，-1 表示新生成
{
    // 构建 old -> new 映射
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
// 返回优化后的四元式序列，以及每条指令对应的原始索引（-1 代表新生成）
static std::pair<std::vector<FourTuple>, std::vector<int>>
optimizeBasicBlockWithOrig(const BasicBlock &block)
{
    std::vector<FourTuple> result = block.tuples;
    std::vector<int> orig(result.size());
    for (int i = 0; i < (int)result.size(); i++)
        orig[i] = block.start_index + i;   // 初始对应原始索引

    // 依次进行局部优化，同时维护 orig 映射
    auto applyOpt = [&](std::vector<FourTuple> (*opt)(const std::vector<FourTuple>&)) {
        std::vector<FourTuple> newRes = opt(result);
        std::vector<int> newOrig;
        // 简单策略：如果指令数相同，则保留原映射；否则保守地全部设为 -1
        // 更精确的实现需要各优化函数也返回 orig，但这里我们保守处理。
        if (newRes.size() == result.size())
        {
            newOrig = orig;
        }
        else
        {
            newOrig.assign(newRes.size(), -1);
        }
        result = newRes;
        orig = newOrig;
    };

    applyOpt(constantFolding);
    applyOpt(algebraicSimplify);
    applyOpt(commonSubexpressionElimination);
    applyOpt(constantFolding);  // 再次折叠
    // 注意：死代码消除放到全局处理

    return {result, orig};
}

// ==================== 优化管理器（重构） ====================
std::vector<FourTuple> optimize(const std::vector<FourTuple> &input)
{
    std::cout << "\n========== 开始优化 ==========" << std::endl;
    std::cout << "原始四元式: " << input.size() << " 条" << std::endl;

    // 1. 基本块划分（基于原始序列）
    std::vector<BasicBlock> blocks = buildBasicBlocks(input);
    std::cout << "基本块数量: " << blocks.size() << " 个" << std::endl;

    // 2. 对每个基本块做局部优化，收集结果及原始索引
    std::vector<FourTuple> afterLocal;
    std::vector<int> origIdx;
    for (const auto &block : blocks)
    {
        std::cout << "\n----- 局部优化基本块 B" << block.id << " -----" << std::endl;
        auto [optBlock, blockOrig] = optimizeBasicBlockWithOrig(block);
        afterLocal.insert(afterLocal.end(), optBlock.begin(), optBlock.end());
        origIdx.insert(origIdx.end(), blockOrig.begin(), blockOrig.end());
    }

    // 3. 准备函数出口活跃集 和 call 参数使用集（基于 afterLocal）
    auto funcExitUses = buildFuncExitUses(afterLocal);
    auto callParamUses = buildCallParamUses(afterLocal);

    // 4. 全局死代码消除
    std::cout << "\n----- 全局死代码消除 -----" << std::endl;
    std::vector<FourTuple> afterDead = globalDeadCodeElimination(afterLocal, funcExitUses, callParamUses);

    // 同步 origIdx：死代码消除删除了某些指令，我们需要对应裁剪 origIdx
    std::vector<int> newOrigIdx;
    // 我们采用简单方法：因为死代码消除后的指令顺序与 afterLocal 的相对顺序相同（只是删除了部分），
    // 我们可以顺序匹配来重建 origIdx。
    int deadIdx = 0;
    for (int i = 0; i < (int)afterLocal.size() && deadIdx < (int)afterDead.size(); i++)
    {
        // 通过比较四元式内容是否一致来判断是否被保留（注意：死代码消除可能改变某些指令？不会，只删除）
        if (deadIdx < (int)afterDead.size() &&
            afterLocal[i].operator_str == afterDead[deadIdx].operator_str &&
            afterLocal[i].first_value == afterDead[deadIdx].first_value &&
            afterLocal[i].second_value == afterDead[deadIdx].second_value &&
            afterLocal[i].dist == afterDead[deadIdx].dist)
        {
            newOrigIdx.push_back(origIdx[i]);
            deadIdx++;
        }
        // 否则该指令被删除，忽略
    }
    // 长度应一致
    if (deadIdx != (int)afterDead.size())
        std::cerr << "错误：死代码消除后指令匹配失败！" << std::endl;

    // 5. 跳转目标重映射
    std::vector<FourTuple> final = remapJumpTargets(afterDead, newOrigIdx);

    std::cout << "优化后四元式: " << final.size() << " 条" << std::endl;
    std::cout << "========== 优化完成 ==========" << std::endl;
    return final;
}

// ==================== 辅助函数：打印 ====================
// ... 保持不变
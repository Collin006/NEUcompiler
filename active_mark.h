#pragma once

#include <string>
#include <vector>
#include <set>
#include <unordered_map>
#include "youhua.h"

struct ActiveBasicBlock
{
    int id;
    int start_index;
    int end_index;
    std::set<int> prev;
    std::set<int> next;
    std::set<std::string> use;
    std::set<std::string> def;
    std::set<std::string> in_set;
    std::set<std::string> out_set;
};

std::vector<ActiveBasicBlock> ToActiveBlocks(const std::vector<BasicBlock> &blocks);

void ComputeUseDef(std::vector<ActiveBasicBlock> &blocks, const std::vector<FourTuple> &tuples);

void ComputeInOut(std::vector<ActiveBasicBlock> &blocks);

std::vector<std::set<std::string>> ComputePerInstructionLiveness(
    const std::vector<ActiveBasicBlock> &blocks,
    const std::vector<FourTuple> &tuples);

std::pair<
    std::vector<MarkedFourTuple>,
    std::vector<std::unordered_map<std::string, int>>>
ActiveMark(
    const std::vector<FourTuple> &tuples,
    const std::vector<BasicBlock> &blocks);

std::string formatMarkedValue(const MarkedValue &value);
std::string dumpMarkedQuadruples(const std::vector<MarkedFourTuple> &marked);

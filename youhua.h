//
// Created by 玉豪郑 on 2026/5/18.
//

#ifndef NEUCOMPILER_YOUHUA_H
#define NEUCOMPILER_YOUHUA_H
#include <vector>
#include <set>
#include "four_tuple.h"
struct BasicBlock
{
    int id;
    int start_index;
    int end_index;
    std::vector<FourTuple> tuples;
    std::set<int> prev;
    std::set<int> next;
};
std::vector<BasicBlock> buildBasicBlocks(const std::vector<FourTuple> &tuples);
std::vector<FourTuple> optimize(const std::vector<FourTuple> &input);
std::vector<FourTuple> optimizeByBasicBlocks(const std::vector<BasicBlock> &blocks);

#endif //NEUCOMPILER_YOUHUA_H

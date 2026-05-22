#include "active_mark.h"
#include <sstream>

static bool IsConstant(const std::string &s)
{
    if (s.empty())
        return true;
    return std::isdigit(s[0]);
}

static bool IsVariable(const std::string &s)
{
    return !s.empty() && !IsConstant(s);
}

static bool IsNonDefOp(const std::string &op)
{
    return op == "lb" || op == "wh" || op == "we" || op == "goto"
        || op == "el" || op == "ie" || op == "call" || op == "ret";
}

static bool IsCondJumpOp(const std::string &op)
{
    return op == "if" || op == "do";
}

static void AddUse(std::set<std::string> &use, const std::set<std::string> &def, const std::string &value)
{
    if (IsVariable(value) && def.find(value) == def.end())
    {
        use.insert(value);
    }
}

std::vector<ActiveBasicBlock> ToActiveBlocks(const std::vector<BasicBlock> &blocks)
{
    std::vector<ActiveBasicBlock> active_blocks;
    active_blocks.reserve(blocks.size());

    for (const auto &block : blocks)
    {
        ActiveBasicBlock ab;
        ab.id = block.id;
        ab.start_index = block.start_index;
        ab.end_index = block.end_index;
        ab.prev = block.prev;
        ab.next = block.next;
        active_blocks.push_back(ab);
    }

    return active_blocks;
}

void ComputeUseDef(std::vector<ActiveBasicBlock> &blocks, const std::vector<FourTuple> &tuples)
{
    for (auto &block : blocks)
    {
        block.use.clear();
        block.def.clear();

        for (int i = block.start_index; i <= block.end_index; i++)
        {
            const auto &ft = tuples[i];

            if (IsNonDefOp(ft.operator_str))
                continue;

            if (IsCondJumpOp(ft.operator_str))
            {
                AddUse(block.use, block.def, ft.first_value);
                continue;
            }

            AddUse(block.use, block.def, ft.first_value);
            AddUse(block.use, block.def, ft.second_value);

            if (IsVariable(ft.dist))
            {
                block.def.insert(ft.dist);
            }
        }
    }
}

void ComputeInOut(std::vector<ActiveBasicBlock> &blocks)
{
    bool changed = true;
    while (changed)
    {
        changed = false;

        for (int i = blocks.size() - 1; i >= 0; i--)
        {
            auto &block = blocks[i];

            std::set<std::string> new_OUT;
            for (int next_id : block.next)
            {
                for (const auto &var : blocks[next_id].in_set)
                {
                    new_OUT.insert(var);
                }
            }

            std::set<std::string> new_IN = block.use;
            for (const auto &var : new_OUT)
            {
                if (block.def.find(var) == block.def.end())
                {
                    new_IN.insert(var);
                }
            }

            if (new_IN != block.in_set || new_OUT != block.out_set)
            {
                block.in_set = new_IN;
                block.out_set = new_OUT;
                changed = true;
            }
        }
    }
}

std::vector<std::set<std::string>> ComputePerInstructionLiveness(
    const std::vector<ActiveBasicBlock> &blocks,
    const std::vector<FourTuple> &tuples)
{
    int n = tuples.size();
    std::vector<std::set<std::string>> live_before(n);

    for (const auto &block : blocks)
    {
        std::set<std::string> live_after = block.out_set;

        for (int i = block.end_index; i >= block.start_index; i--)
        {
            const auto &ft = tuples[i];

            std::set<std::string> use_i, def_i;

            if (!IsNonDefOp(ft.operator_str))
            {
                if (IsCondJumpOp(ft.operator_str))
                {
                    if (IsVariable(ft.first_value))
                        use_i.insert(ft.first_value);
                }
                else
                {
                    if (IsVariable(ft.first_value))
                        use_i.insert(ft.first_value);
                    if (IsVariable(ft.second_value))
                        use_i.insert(ft.second_value);
                    if (IsVariable(ft.dist))
                        def_i.insert(ft.dist);
                }
            }

            live_before[i] = use_i;
            for (const auto &var : live_after)
            {
                if (def_i.find(var) == def_i.end())
                {
                    live_before[i].insert(var);
                }
            }

            live_after = live_before[i];
        }
    }

    return live_before;
}

std::pair<
    std::vector<MarkedFourTuple>,
    std::vector<std::unordered_map<std::string, int>>>
ActiveMark(
    const std::vector<FourTuple> &tuples,
    const std::vector<BasicBlock> &blocks)
{
    std::vector<ActiveBasicBlock> active_blocks = ToActiveBlocks(blocks);

    ComputeUseDef(active_blocks, tuples);
    ComputeInOut(active_blocks);

    std::vector<std::set<std::string>> live_before =
        ComputePerInstructionLiveness(active_blocks, tuples);

    int n = tuples.size();
    std::vector<MarkedFourTuple> marked_qt(n);
    std::vector<std::unordered_map<std::string, int>> active_record(n);

    for (int i = 0; i < n; i++)
    {
        const auto &ft = tuples[i];
        auto &mft = marked_qt[i];

        mft.operator_str = ft.operator_str;

        mft.first_value.value = ft.first_value;
        mft.first_value.active = IsConstant(ft.first_value)
                                     ? is_constant
                                     : (live_before[i].count(ft.first_value) ? 1 : 0);

        mft.second_value.value = ft.second_value;
        mft.second_value.active = IsConstant(ft.second_value)
                                      ? is_constant
                                      : (live_before[i].count(ft.second_value) ? 1 : 0);

        mft.dist.value = ft.dist;
        mft.dist.active = IsConstant(ft.dist)
                              ? is_constant
                              : (live_before[i].count(ft.dist) ? 1 : 0);

        for (const auto &var : live_before[i])
        {
            active_record[i][var] = 1;
        }
    }

    return {marked_qt, active_record};
}

std::string formatMarkedValue(const MarkedValue &value)
{
    std::string text = value.value.empty() ? "_" : value.value;
    if (value.active == is_constant)
    {
        return text + "/C";
    }
    return text + (value.active ? "/1" : "/0");
}

std::string dumpMarkedQuadruples(const std::vector<MarkedFourTuple> &marked)
{
    std::ostringstream out;
    for (size_t i = 0; i < marked.size(); ++i)
    {
        const auto &mft = marked[i];
        out << i << ": (" << mft.operator_str << ", "
            << formatMarkedValue(mft.first_value) << ", "
            << formatMarkedValue(mft.second_value) << ", "
            << formatMarkedValue(mft.dist) << ")\n";
    }
    return out.str();
}

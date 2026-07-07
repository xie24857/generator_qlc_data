#ifndef QUOTASHUFFLEGENERATOR_H
#define QUOTASHUFFLEGENERATOR_H

#include "algo/BaseGenerator.h"

#include <cstdint>
#include <map>
#include <random>
#include <vector>

// ==========================================
//  配额洗牌生成器（cell-type 无关：按状态分布填充 pool 后洗牌）
// ==========================================
class QuotaShuffleGenerator : public BaseGenerator {
private:
    std::vector<int> pool;
    size_t current_idx;
    std::mt19937 rng;

public:
    QuotaShuffleGenerator(const std::map<int, size_t>& state_counts,
                          const std::vector<int>& encoding);

    int next() override;
};

#endif // QUOTASHUFFLEGENERATOR_H

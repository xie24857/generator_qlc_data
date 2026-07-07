#include "algo/QuotaShuffleGenerator.h"

#include <algorithm>
#include <random>

QuotaShuffleGenerator::QuotaShuffleGenerator(
    const std::map<int, size_t>& state_counts,
    const std::vector<int>& encoding)
{
    std::random_device rd;
    rng = std::mt19937(rd());
    current_idx = 0;

    // 按照各状态的数量填充 pool
    for (const auto& p : state_counts) {
        int encoded_value = encoding[p.first];
        for (size_t i = 0; i < p.second; i++) {
            pool.push_back(encoded_value);
        }
    }

    // 洗牌打乱
    std::shuffle(pool.begin(), pool.end(), rng);
}

int QuotaShuffleGenerator::next() {
    if (current_idx >= pool.size()) {
        std::shuffle(pool.begin(), pool.end(), rng);
        current_idx = 0;
    }
    return pool[current_idx++];
}

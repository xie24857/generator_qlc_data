#include "algo/DualPoolGenerator.h"
#include "algo/QuotaShuffleGenerator.h"

#include <memory>
#include <vector>

DualPoolGenerator::DualPoolGenerator(
    const std::map<int, size_t>& state_counts,
    const std::vector<int>& encoding,
    Mode mode,
    Map map)
    : mode(mode), cells_per_wl(0)
{
    // 按 key 奇偶拆分
    std::map<int, size_t> even_keys, odd_keys;
    for (const auto& p : state_counts) {
        if (p.first % 2 == 0) even_keys.insert(p);
        else                  odd_keys.insert(p);
        cells_per_wl += p.second;
    }

    // key 分配规则：
    //   parity default：偶状态→偶数位，奇状态→奇数位
    //   parity swap：   偶状态→奇数位，奇状态→偶数位
    //   half   default：奇状态→前半，偶状态→后半
    //   half   swap：   偶状态→前半，奇状态→后半
    const std::map<int, size_t>* first;
    const std::map<int, size_t>* second;
    if (mode == Mode::PARITY) {
        if (map == Map::DEFAULT) { first = &even_keys; second = &odd_keys; }
        else                     { first = &odd_keys;  second = &even_keys; }
    } else {
        if (map == Map::DEFAULT) { first = &odd_keys;  second = &even_keys; }
        else                     { first = &even_keys; second = &odd_keys; }
    }

    pool_first.reset(new QuotaShuffleGenerator(*first, encoding));
    pool_second.reset(new QuotaShuffleGenerator(*second, encoding));
}

int DualPoolGenerator::next() {
    int val;
    if (mode == Mode::PARITY) {
        val = (cell_pos % 2 == 0)
            ? pool_first->next()
            : pool_second->next();
    } else {
        val = (static_cast<size_t>(cell_pos) < static_cast<size_t>(cells_per_wl) / 2)
            ? pool_first->next()
            : pool_second->next();
    }
    cell_pos = (cell_pos + 1) % cells_per_wl;
    return val;
}

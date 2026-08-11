#ifndef DUALPOOLGENERATOR_H
#define DUALPOOLGENERATOR_H

#include "algo/BaseGenerator.h"

#include <cstddef>
#include <map>
#include <memory>
#include <vector>

// ==========================================
//  双池生成器：按位置奇偶/前后自动选择子池
//  parity 模式：奇数位置用一个子池，偶数位置用另一个子池
//  half 模式：前半部分用一个子池，后半部分用另一个子池
//  各状态等量：每状态 cell 数 = cells_per_wl / num_states
// ==========================================
class DualPoolGenerator : public BaseGenerator {
public:
    enum class Mode { PARITY, HALF };
    enum class Map  { DEFAULT, SWAP };

    DualPoolGenerator(const std::map<int, size_t>& state_counts,
                      const std::vector<int>& encoding,
                      Mode mode,
                      Map map);

    int next() override;

private:
    std::unique_ptr<BaseGenerator> pool_first;
    std::unique_ptr<BaseGenerator> pool_second;
    size_t cells_per_wl;
    Mode mode;
    size_t cell_pos = 0;
};

#endif // DUALPOOLGENERATOR_H

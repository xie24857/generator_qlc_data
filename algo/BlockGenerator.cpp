#include "algo/BlockGenerator.h"
#include "algo/QuotaShuffleGenerator.h"
#include "algo/DualPoolGenerator.h"
#include "algo/PageProcessor.h"
#include "logger/Logger.h"

#include <algorithm>
#include <cstdio>
#include <stdexcept>
#include <string>

// ==========================================
//  辅助：计算每个 WordLine 中各状态的数量
// ==========================================
std::map<int, size_t> BlockGenerator::buildStateCounts(
    const DeviceConfig::TypeStrategy& strategy,
    size_t num_states)
{
    std::map<int, size_t> counts;

    if (strategy.position_mode != "none") {
        // 位置约束模式：各状态等量
        if (num_states == 0 || cells_per_wl % num_states != 0)
            throw std::runtime_error("cells_per_wl 不能被状态数整除: " +
                std::to_string(cells_per_wl) + " / " + std::to_string(num_states));
        size_t per_state = cells_per_wl / num_states;
        for (int i = 0; i < static_cast<int>(num_states); ++i)
            counts[i] = per_state;
    } else if (strategy.mode == "count") {
        // count 模式直接使用
        counts.insert(strategy.state_values.begin(), strategy.state_values.end());
    } else {
        // ratio 模式：按权重分配
        size_t total_weight = 0;
        for (const auto& p : strategy.state_values)
            total_weight += p.second;

        size_t assigned = 0;
        std::vector<std::pair<int, size_t>> remainder_list;

        for (const auto& p : strategy.state_values) {
            size_t c = (p.second * cells_per_wl) / total_weight;
            counts[p.first] = c;
            assigned += c;
            // 记录余数用于后续分配
            size_t rem = (p.second * cells_per_wl) % total_weight;
            remainder_list.push_back(std::make_pair(p.first, rem));
        }

        // 按余数从大到小分配剩余的
        std::sort(remainder_list.begin(), remainder_list.end(),
            [](const std::pair<int, size_t>& a, const std::pair<int, size_t>& b) {
                return a.second > b.second;
            });

        size_t remaining = cells_per_wl - assigned;
        for (size_t i = 0; i < remaining && i < remainder_list.size(); i++) {
            counts[remainder_list[i].first]++;
        }
    }

    return counts;
}

// ==========================================
//  BlockGenerator
// ==========================================
BlockGenerator::BlockGenerator(const DeviceConfig& device_cfg, size_t cells_per_wl_cfg)
    : device(device_cfg), cells_per_wl(cells_per_wl_cfg)
{
    // 1. 为每个配置了策略的 cell type 准备 per-WL 计数与生成器
    for (const auto& strategy_entry : device.strategies) {
        const std::string& cell_type = strategy_entry.first;
        const auto& strategy = strategy_entry.second;

        const auto& encoding = device.encodings.at(cell_type);

        CellTypeGenerator ctg;
        switch (encoding.size()) {
            case 16: ctg.bitsPerCell = 4; break;
            case  8: ctg.bitsPerCell = 3; break;
            case  4: ctg.bitsPerCell = 2; break;
            case  2: ctg.bitsPerCell = 1; break;
            default:
                throw std::runtime_error("未知的 encoding 大小: " + std::to_string(encoding.size()));
        }

        size_t num_states = encoding.size();
        ctg.wl_state_counts = buildStateCounts(strategy, num_states);
        ctg.position_mode = strategy.position_mode;
        ctg.parity_map = strategy.parity_map;

        if (strategy.position_mode == "none") {
            ctg.cell_pool.reset(new QuotaShuffleGenerator(ctg.wl_state_counts, encoding));
        } else {
            DualPoolGenerator::Mode mode = (strategy.position_mode == "parity")
                ? DualPoolGenerator::Mode::PARITY
                : DualPoolGenerator::Mode::HALF;
            DualPoolGenerator::Map map = (strategy.parity_map == "swap")
                ? DualPoolGenerator::Map::SWAP
                : DualPoolGenerator::Map::DEFAULT;
            ctg.cell_pool.reset(new DualPoolGenerator(ctg.wl_state_counts, encoding, mode, map));
        }

        generators.emplace(cell_type, std::move(ctg));
    }

    // 2. 建立 WL -> CellTypeGenerator 查表
    wl_type.assign(device.wordline_count, nullptr);
    for (const auto& wl_range : device.wl_ranges) {
        CellTypeGenerator* pt = &generators.at(wl_range.first);
        for (const auto& position : wl_range.second) {
            for (size_t wl = position.first; wl <= position.second; ++wl)
                wl_type[wl] = pt;
        }
    }
}

BlockGenerator::~BlockGenerator() = default;

void BlockGenerator::printInfo() const {
    for (const auto& strategy_entry : device.strategies) {
        const std::string& cell_type = strategy_entry.first;
        const auto& strategy = strategy_entry.second;
        const auto& ctg = generators.at(cell_type);

        log_info("[%s] bits/cell=%d  mode=%s  position_mode=%s  parity_map=%s",
                 cell_type.c_str(), ctg.bitsPerCell, strategy.mode.c_str(),
                 ctg.position_mode.c_str(), ctg.parity_map.c_str());

        if (ctg.position_mode == "none") {
            // 直接显示各状态数量
            const auto& counts = ctg.wl_state_counts;
            for (const auto& p : counts) {
                char buf[64];
                snprintf(buf, sizeof(buf), "状态 %2d占有cells数量:%6zu", p.first, p.second);
                log_info("[%s] %s", cell_type.c_str(), buf);
            }
        } else {
            // 等量模式：显示子池分配
            int num_states = static_cast<int>(ctg.wl_state_counts.size());
            size_t per_state = cells_per_wl / num_states;
            bool is_parity = (ctg.position_mode == "parity");
            bool is_swap   = (ctg.parity_map == "swap");
            const char* first_name  = is_parity ? "偶数位" : "前半";
            const char* second_name = is_parity ? "奇数位" : "后半";

            char buf[128];
            for (int pool_idx = 0; pool_idx < 2; ++pool_idx) {
                bool is_first = (pool_idx == 0);
                // 该池是否为偶数状态
                bool pool_even;
                if (is_parity) pool_even = is_first != is_swap;
                else           pool_even = is_first == is_swap;

                const char* pool_name = is_first ? first_name : second_name;
                int n = snprintf(buf, sizeof(buf), "[%s] 池%d(%s) 状态:",
                                 cell_type.c_str(), pool_idx + 1, pool_name);
                for (int i = 0; i < num_states; ++i)
                    if ((i % 2 == 0) == pool_even)
                        n += snprintf(buf + n, sizeof(buf) - n, " %d", i);
                n += snprintf(buf + n, sizeof(buf) - n, "  每状态 %zu cells", per_state);
                log_info("%s", buf);
            }
        }
    }
}

void BlockGenerator::generate(uint8_t* block_data) const {
    PageProcessor processor(block_data, device.page_size);

    log_info("Initializing Data Generator...");
    log_info("Generating block data, please wait...");
    for (size_t wl = 0; wl < device.wordline_count; ++wl) {
        CellTypeGenerator* ctg = wl_type[wl];
        processor.reset(ctg->bitsPerCell);
        for (size_t c = 0; c < cells_per_wl; ++c)
            processor.process(ctg->cell_pool->next());
        processor.flush();
    }
    log_info("Progress: 100%% done.");
}

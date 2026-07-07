#include "algo/BlockGenerator.h"
#include "algo/QuotaShuffleGenerator.h"
#include "algo/PageProcessor.h"
#include "logger/Logger.h"

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <stdexcept>
#include <string>

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

        ctg.wl_state_counts = buildStateCounts(strategy);
        ctg.cell_pool.reset(new QuotaShuffleGenerator(ctg.wl_state_counts, encoding));
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

// ==========================================
//  辅助：计算每个 WordLine 中各状态的数量
// ==========================================
std::map<int, size_t> BlockGenerator::buildStateCounts(
    const DeviceConfig::TypeStrategy& strategy)
{
    std::map<int, size_t> counts;

    if (strategy.mode == "count") {
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

void BlockGenerator::printInfo() const {
    for (const auto& strategy_entry : device.strategies) {
        const std::string& cell_type = strategy_entry.first;
        const auto& strategy = strategy_entry.second;
        const auto& ctg = generators.at(cell_type);

        log_info("[%s] bits/cell=%d  mode=%s", cell_type.c_str(),
                 ctg.bitsPerCell, strategy.mode.c_str());

        const auto& counts = ctg.wl_state_counts;
        for (size_t i = 0; i < counts.size(); ++i) {
            char buf[64];
            snprintf(buf, sizeof(buf), "状态 %2zu占有cells数量:%6zu", i,
                     counts.at(static_cast<int>(i)));
            log_info("[%s] %s", cell_type.c_str(), buf);
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

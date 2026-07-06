#include "ini/Ini.h"
#include "config/Config.h"
#include "logger/Logger.h"

#include <iostream>
#include <fstream>
#include <vector>
#include <memory>
#include <cstdint>
#include <random>
#include <algorithm>
#include <iomanip>
#include <unordered_map>

// ==========================================
// 根据配置计算每个 WordLine 中各状态的数量
// ==========================================
static std::unordered_map<int, size_t> buildStateCounts(const DeviceConfig::TypeStrategy& strategy, size_t cells_per_wl)
{
    std::unordered_map<int, size_t> counts;

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

// ==========================================
// 平均生成器（cell-type 无关：按状态分布填充 pool 后洗牌）
// ==========================================
class StrictAverageGenerator {
private:
    std::vector<int> pool;
    size_t current_idx;
    std::mt19937 rng;

public:
    StrictAverageGenerator(const std::unordered_map<int, size_t>& state_counts,
                           const std::vector<int>& encoding) {
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

    int next_value() {
        if (current_idx >= pool.size()) {
            std::shuffle(pool.begin(), pool.end(), rng);
            current_idx = 0;
        }
        return pool[current_idx++];
    }
};

// ==========================================
// Page 处理模块（按 bitsPerCell 参数化，支持 QLC/TLC/MLC/SLC）
// 每个 WL 调用一次 reset -> process*cells_per_wl -> flush
// ==========================================
class PageProcessor {
private:
    uint8_t* block_ptr;
    size_t block_offset;
    size_t page_size;

    int bits_per_cell;
    std::vector<std::vector<uint8_t>> pages;  // bits_per_cell 个页缓冲
    std::vector<uint8_t> cur_bytes;           // 每个 page 当前正在组装的字节
    int bit_count;
    size_t byte_pos;

public:
    PageProcessor(uint8_t* target_block, size_t page_size)
        : block_ptr(target_block), block_offset(0), page_size(page_size),
          bits_per_cell(0), bit_count(0), byte_pos(0) {}

    // 开始一个新 WL：设定该 WL 的 bitsPerCell，重置缓冲
    void reset(int bitsPerCell) {
        bits_per_cell = bitsPerCell;
        pages.assign(bits_per_cell, std::vector<uint8_t>(page_size));
        cur_bytes.assign(bits_per_cell, 0);
        bit_count = 0;
        byte_pos = 0;
    }

    void process(int encoded_val) {
        // bit b -> pages[b]（b=0 是最低位页 LP，与原 QLC 行为一致）
        for (int b = 0; b < bits_per_cell; ++b) {
            uint8_t bit = static_cast<uint8_t>((encoded_val >> b) & 1);
            cur_bytes[b] = static_cast<uint8_t>((cur_bytes[b] << 1) | bit);
        }

        bit_count++;
        if (bit_count == 8) {
            for (int b = 0; b < bits_per_cell; ++b)
                pages[b][byte_pos] = cur_bytes[b];
            byte_pos++;
            bit_count = 0;
            std::fill(cur_bytes.begin(), cur_bytes.end(), 0);
        }
    }

    // 结束当前 WL：把 bits_per_cell 个完整页刷入 block
    void flush() {
        for (int b = 0; b < bits_per_cell; ++b) {
            std::copy(pages[b].begin(), pages[b].end(), block_ptr + block_offset);
            block_offset += page_size;
        }
    }
};

// ==========================================
// main
// ==========================================
int main(int argc, char* argv[])
{
    // 1. 加载配置
    std::string config_file = (argc > 1) ? argv[1] : "config.ini";
    try {
        Ini::instance().loadFile(config_file);
        g_config.load();
    } catch (const std::runtime_error& e) {
        log_fatal("配置加载失败: %s", e.what());
        return 1;
    }

    size_t block_size = g_config.device.page_count_in_block * g_config.device.page_size;
    size_t cells_per_wl = g_config.device.page_size * 8;
    size_t total_cells = g_config.device.wordline_count * cells_per_wl;

    // 2. 打印配置信息
    log_info("Config File:    %s", config_file.c_str());
    log_info("Block Size:     %zu Bytes", block_size);
    log_info("Page Size:      %zu Bytes", g_config.device.page_size);
    log_info("Page Count:     %zu", g_config.device.page_count_in_block);
    log_info("Wordline Count: %zu", g_config.device.wordline_count);
    log_info("Cells per WL:   %zu", cells_per_wl);
    log_info("Total Cells:    %zu", total_cells);

    // 3. 为每个配置了策略的 cell type 准备 per-WL 计数与生成器
    struct CellTypeGenerator {
        int bitsPerCell;
        std::unordered_map<int, size_t> wl_state_counts;  // 每个 WL 各状态的 cell 数量
        std::unique_ptr<StrictAverageGenerator> cell_pool;
    };
    std::unordered_map<std::string, CellTypeGenerator> generators;

    for (const auto& strategy_entry : g_config.device.strategies) {
        const std::string& cell_type = strategy_entry.first;   // cell type 名称，如 "qlc"/"tlc"
        const auto& strategy = strategy_entry.second;          // 对应的 TypeStrategy（mode + state_values）

        const auto& encoding = g_config.device.encodings.at(cell_type);

        CellTypeGenerator celltypegenerator;
        switch (encoding.size()) {
            case 16: celltypegenerator.bitsPerCell = 4; break;
            case  8: celltypegenerator.bitsPerCell = 3; break;
            case  4: celltypegenerator.bitsPerCell = 2; break;
            case  2: celltypegenerator.bitsPerCell = 1; break;
            default:
                throw std::runtime_error("未知的 encoding 大小: " + std::to_string(encoding.size()));
        }

        celltypegenerator.wl_state_counts = buildStateCounts(strategy, cells_per_wl);
        celltypegenerator.cell_pool.reset(new StrictAverageGenerator(celltypegenerator.wl_state_counts, encoding));
        generators.emplace(cell_type, std::move(celltypegenerator));

        // 打印该 cell type 的策略
        log_info("[%s] bits/cell=%d  mode=%s", cell_type.c_str(),
                 generators.at(cell_type).bitsPerCell, strategy.mode.c_str());
        const auto& counts = generators.at(cell_type).wl_state_counts;
        for (size_t i = 0; i < counts.size(); ++i) {
            char buf[64];
            snprintf(buf, sizeof(buf), "状态 %2zu占有cells数量:%6zu", i, counts.at(static_cast<int>(i)));
            log_info("[%s] %s", cell_type.c_str(), buf);
        }
    }

    // 4. 建立 WL -> CellTypeGenerator 查表
    std::vector<CellTypeGenerator*> wl_type(g_config.device.wordline_count);
    for (const auto& kv : g_config.device.wl_ranges) {
        CellTypeGenerator* pt = &generators.at(kv.first);
        for (const auto& r : kv.second) {
            for (size_t wl = r.first; wl <= r.second; ++wl)
                wl_type[wl] = pt;
        }
    }

    // 5. 申请 Block 内存并按 WL 生成
    std::vector<uint8_t> block_data(block_size);
    PageProcessor processor(block_data.data(), g_config.device.page_size);

    log_info("Initializing Data Generator...");
    log_info("Generating block data, please wait...");
    for (size_t wl = 0; wl < g_config.device.wordline_count; ++wl) {
        CellTypeGenerator* celltypegenerator = wl_type[wl];
        processor.reset(celltypegenerator->bitsPerCell);
        for (size_t c = 0; c < cells_per_wl; ++c)
            processor.process(celltypegenerator->cell_pool->next_value());
        processor.flush();
    }
    log_info("Progress: 100%% done.");

    // 6. 写入文件
    std::string out_filename = g_config.filename;
    log_info("Writing to disk: %s ...", out_filename.c_str());

    std::ofstream outfile(out_filename, std::ios::binary);
    if (!outfile) {
        log_fatal("无法打开输出文件: %s", out_filename.c_str());
        return 1;
    }

    outfile.write(reinterpret_cast<const char*>(block_data.data()), block_size);
    outfile.close();

    log_info("Block file written successfully!");
    return 0;
}

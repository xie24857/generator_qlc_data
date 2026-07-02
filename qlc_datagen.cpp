#include "ini/Ini.h"
#include "config/Config.h"
#include "logger/Logger.h"

#include <iostream>
#include <fstream>
#include <vector>
#include <cstdint>
#include <random>
#include <algorithm>
#include <iomanip>
#include <map>

// ==========================================
// 根据配置计算每个 WordLine 中各状态的数量
// ==========================================
static std::map<int, size_t> computePerWlCounts(const StrategyConfig& strategy, size_t cells_per_wl)
{
    std::map<int, size_t> counts;

    if (strategy.mode == "count") {
        // count 模式直接使用
        counts = strategy.state_values;
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
// 平均生成器
// ==========================================
class StrictAverageGenerator {
private:
    std::vector<int> pool;
    size_t current_idx;
    std::mt19937 rng;

public:
    StrictAverageGenerator(const std::map<int, size_t>& state_counts,
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
// Page 处理模块
// ==========================================
class PageProcessor {
private:
    uint8_t* block_ptr;
    size_t block_offset;
    size_t page_size;

    std::vector<uint8_t> page_lp, page_mp, page_up, page_xp;
    size_t page_pos;

    uint8_t cur_lp, cur_mp, cur_up, cur_xp;
    int bit_count;

public:
    PageProcessor(uint8_t* target_block, size_t page_size)
        : block_ptr(target_block), block_offset(0), page_size(page_size),
          page_lp(page_size), page_mp(page_size), page_up(page_size), page_xp(page_size),
          page_pos(0), cur_lp(0), cur_mp(0), cur_up(0), cur_xp(0), bit_count(0) {}

    void process(int encoded_val) {
        uint8_t xp = (encoded_val >> 3) & 1;
        uint8_t up = (encoded_val >> 2) & 1;
        uint8_t mp = (encoded_val >> 1) & 1;
        uint8_t lp = encoded_val & 1;

        cur_xp = (cur_xp << 1) | xp;
        cur_up = (cur_up << 1) | up;
        cur_mp = (cur_mp << 1) | mp;
        cur_lp = (cur_lp << 1) | lp;

        bit_count++;

        if (bit_count == 8) {
            page_xp[page_pos] = cur_xp;
            page_up[page_pos] = cur_up;
            page_mp[page_pos] = cur_mp;
            page_lp[page_pos] = cur_lp;
            page_pos++;
            bit_count = 0;

            if (page_pos == page_size) {
                std::copy(page_lp.begin(), page_lp.end(), block_ptr + block_offset); block_offset += page_size;
                std::copy(page_mp.begin(), page_mp.end(), block_ptr + block_offset); block_offset += page_size;
                std::copy(page_up.begin(), page_up.end(), block_ptr + block_offset); block_offset += page_size;
                std::copy(page_xp.begin(), page_xp.end(), block_ptr + block_offset); block_offset += page_size;

                page_pos = 0;
            }
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

    size_t block_size = g_config.flash.page_count_in_block * g_config.flash.page_size;
    size_t cells_per_wl = g_config.flash.page_size * 8;
    size_t total_cells = (block_size * 8) / 4;

    // 2. 打印配置信息
    log_info("Config File:    %s", config_file.c_str());
    log_info("Block Size:     %zu Bytes", block_size);
    log_info("Page Size:      %zu Bytes", g_config.flash.page_size);
    log_info("Page Count:     %zu", g_config.flash.page_count_in_block);
    log_info("Cells per WL:   %zu", cells_per_wl);
    log_info("Total Cells:    %zu", total_cells);
    log_info("Strategy Mode:  %s", g_config.strategy.mode.c_str());

    std::string active_states;
    for (const auto& p : g_config.strategy.state_values)
        active_states += std::to_string(p.first) + "(" + std::to_string(p.second) + ") ";
    log_info("Active States:  %s", active_states.c_str());

    // 3. 计算每个 WL 中各状态的数量
    std::map<int, size_t> per_wl_counts = computePerWlCounts(g_config.strategy, cells_per_wl);

    log_info("Per-WL state counts:");
    for (const auto& p : per_wl_counts)
        log_info("  State %2d: %zu", p.first, p.second);

    // 4. 初始化生成器
    log_info("Initializing QLC Data Generator...");
    StrictAverageGenerator generator(per_wl_counts, g_config.flash.encoding);

    // 5. 申请 Block 内存并生成数据
    std::vector<uint8_t> block_data(block_size);
    PageProcessor processor(block_data.data(), g_config.flash.page_size);

    log_info("Generating block data, please wait...");
    for (size_t i = 0; i < total_cells; ++i) {
        int encoded_val = generator.next_value();
        processor.process(encoded_val);
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

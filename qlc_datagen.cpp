#include <iostream>
#include <fstream>
#include <vector>
#include <cstdint>
#include <random>
#include <algorithm>
#include <iomanip>
#include <map>
#include <functional>

#include "IniConfig.h"

// ==========================================
// 配置加载
// ==========================================
struct FlashConfig {
    size_t page_count_in_block;
    size_t page_size;
    std::vector<int> encoding;  // 16 个值
};

struct StrategyConfig {
    std::string mode;                   // "ratio" 或 "count"
    std::map<int, size_t> state_values; // state -> 权重或数量（仅非零项）
};

struct AppConfig {
    FlashConfig flash;
    StrategyConfig strategy;
    std::string filename;
};

// ==========================================
// 自动生成文件名
// ==========================================
static std::string generateFilename(const StrategyConfig& strategy)
{
    std::string name = strategy.mode;
    for (const auto& p : strategy.state_values) {
        name += "_" + std::to_string(p.first) + "-" + std::to_string(p.second);
    }

    // Linux 文件名上限 255 字节，预留 .bin 和 hash 的空间
    const size_t MAX_BASE_LEN = 240;
    if (name.size() > MAX_BASE_LEN) {
        std::size_t h = std::hash<std::string>()(name);
        std::string hash_str = std::to_string(h);
        name = name.substr(0, MAX_BASE_LEN - hash_str.size() - 1) + "_" + hash_str;
    }

    return name + ".bin";
}

// ==========================================
// 加载配置
// ==========================================
static AppConfig loadConfig(const std::string& config_file)
{
    auto ini = IniConfig::fromFile(config_file);
    AppConfig cfg;

    // [flash]
    auto page_count = ini.getInt("flash", "page_count_in_block");
    if (!page_count.first)
        throw std::runtime_error("配置缺少 flash.page_count_in_block");
    cfg.flash.page_count_in_block = static_cast<size_t>(page_count.second);
    if (cfg.flash.page_count_in_block % 4 != 0)
        throw std::runtime_error("page_count_in_block 必须是 4 的倍数");

    auto page_size = ini.getInt("flash", "page_size");
    if (!page_size.first)
        throw std::runtime_error("配置缺少 flash.page_size");
    cfg.flash.page_size = static_cast<size_t>(page_size.second);

    auto enc = ini.getIntList("flash", "encoding");
    if (!enc.first || enc.second.size() != 16)
        throw std::runtime_error("encoding 必须恰好 16 个值");

    // 校验是 0-15 的排列（每个值出现恰好一次）
    std::vector<bool> seen(16, false);
    for (long v : enc.second) {
        if (v < 0 || v > 15)
            throw std::runtime_error("encoding 值必须在 0-15 范围内: " + std::to_string(v));
        if (seen[v])
            throw std::runtime_error("encoding 中有重复值: " + std::to_string(v));
        seen[v] = true;
    }

    for (long v : enc.second)
        cfg.flash.encoding.push_back(static_cast<int>(v));

    // 校验格雷码：相邻状态的编码只差 1 bit
    for (int i = 0; i < 15; i++) {
        int diff = cfg.flash.encoding[i] ^ cfg.flash.encoding[i + 1];
        if (diff == 0 || (diff & (diff - 1)) != 0)
            throw std::runtime_error("encoding 不是有效格雷码: 状态 " +
                std::to_string(i) + " 和 " + std::to_string(i + 1) +
                " 的编码差异不是 1 bit");
    }

    // [strategy]
    auto mode = ini.getString("strategy", "mode");
    if (!mode.first)
        throw std::runtime_error("配置缺少 strategy.mode");
    cfg.strategy.mode = mode.second;
    if (cfg.strategy.mode != "ratio" && cfg.strategy.mode != "count")
        throw std::runtime_error("strategy.mode 必须是 ratio 或 count");

    // 读取 0-15 的值
    for (int i = 0; i <= 15; i++) {
        auto val = ini.getInt("strategy", std::to_string(i));
        if (!val.first)
            throw std::runtime_error("配置缺少 strategy." + std::to_string(i));
        if (val.second < 0)
            throw std::runtime_error("strategy." + std::to_string(i) + " 不能为负数");
        if (val.second > 0)
            cfg.strategy.state_values[i] = static_cast<size_t>(val.second);
    }
    if (cfg.strategy.state_values.empty())
        throw std::runtime_error("至少需要一个非零状态");

    // count 模式校验总和
    if (cfg.strategy.mode == "count") {
        size_t total = 0;
        for (const auto& p : cfg.strategy.state_values)
            total += p.second;
        size_t expected = cfg.flash.page_size * 8;
        if (total != expected)
            throw std::runtime_error("count 模式总和 " + std::to_string(total) +
                                     " 不等于 page_size*8=" + std::to_string(expected));
    }

    // [output]
    auto fname = ini.getString("output", "filename");
    if (fname.first && !fname.second.empty())
        cfg.filename = fname.second;
    else
        cfg.filename = generateFilename(cfg.strategy);

    return cfg;
}

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
// 平均生成器（保留原有逻辑，改为接受 per-WL 的状态数量）
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
// Page 处理模块（保留原有逻辑，page_size 改为参数传入）
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
    AppConfig cfg;
    try {
        cfg = loadConfig(config_file);
    } catch (const std::runtime_error& e) {
        std::cerr << "配置加载失败: " << e.what() << std::endl;
        return 1;
    }

    size_t block_size = cfg.flash.page_count_in_block * cfg.flash.page_size;
    size_t cells_per_wl = cfg.flash.page_size * 8;
    size_t total_cells = (block_size * 8) / 4;

    // 2. 打印配置信息
    std::cout << "Config File:    " << config_file << std::endl;
    std::cout << "Block Size:     " << block_size << " Bytes" << std::endl;
    std::cout << "Page Size:      " << cfg.flash.page_size << " Bytes" << std::endl;
    std::cout << "Page Count:     " << cfg.flash.page_count_in_block << std::endl;
    std::cout << "Cells per WL:   " << cells_per_wl << std::endl;
    std::cout << "Total Cells:    " << total_cells << std::endl;
    std::cout << "Strategy Mode:  " << cfg.strategy.mode << std::endl;
    std::cout << "Active States:  ";
    for (const auto& p : cfg.strategy.state_values)
        std::cout << p.first << "(" << p.second << ") ";
    std::cout << std::endl;

    // 3. 计算每个 WL 中各状态的数量
    std::map<int, size_t> per_wl_counts = computePerWlCounts(cfg.strategy, cells_per_wl);

    std::cout << "\nPer-WL state counts:" << std::endl;
    for (const auto& p : per_wl_counts)
        std::cout << "  State " << std::setw(2) << p.first << ": " << p.second << std::endl;

    // 4. 初始化生成器
    std::cout << "\nInitializing QLC Data Generator..." << std::endl;
    StrictAverageGenerator generator(per_wl_counts, cfg.flash.encoding);

    // 5. 申请 Block 内存并生成数据
    std::vector<uint8_t> block_data(block_size);
    PageProcessor processor(block_data.data(), cfg.flash.page_size);

    std::cout << "Generating block data, please wait..." << std::endl;
    for (size_t i = 0; i < total_cells; ++i) {
        int encoded_val = generator.next_value();
        processor.process(encoded_val);
    }
    std::cout << "Progress: 100% done." << std::endl;

    // 6. 写入文件
    std::string out_filename = cfg.filename;
    std::cout << "Writing to disk: " << out_filename << " ..." << std::endl;

    std::ofstream outfile(out_filename, std::ios::binary);
    if (!outfile) {
        std::cerr << "Error: Cannot open file for writing!" << std::endl;
        return 1;
    }

    outfile.write(reinterpret_cast<const char*>(block_data.data()), block_size);
    outfile.close();

    std::cout << "Block file written successfully!" << std::endl;
    return 0;
}

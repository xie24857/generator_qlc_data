#include <iostream>
#include <fstream>
#include <vector>
#include <cstdint>
#include <random>
#include <algorithm>
#include <iomanip>

// 全局 QLC 状态(0-15)到 4bits[xp up mp lp] 的格雷编码
const int QLC_ENCODING[16] = {15, 7, 5, 13, 12, 8, 9, 11, 3, 1, 0, 4, 6, 2, 10, 14};

// 闪存物理结构常量定义
const size_t BLOCK_SIZE = 162273280;  // 整个 Block 的大小 (字节)
const size_t PAGE_SIZE  = 19136;      // 单个 Page 的大小 (字节，包含 Data + OOB)
// 需要生成的总状态次数: Block总字节数 * 8 比特 / 每个状态占用4比特 (XP,UP,MP,LP)
const size_t TOTAL_GENERATE_COUNT = (BLOCK_SIZE * 8) / 4; 

// ==========================================
// 绝对严格平均生成器 (生成指定的闪存状态)
// ==========================================
class StrictAverageGenerator {
private:
    std::vector<int> pool;
    size_t current_idx;
    std::mt19937 rng;

public:
    StrictAverageGenerator(const std::vector<int>& allowed_states, size_t wl_states_count = PAGE_SIZE * 8) {
        std::random_device rd;
        rng = std::mt19937(rd());
        current_idx = 0;

        size_t state_count = allowed_states.size();
        size_t current_pool_size = (wl_states_count / state_count) * state_count;

        // 1. 填入能够严格平均的基础部分
        for (int state : allowed_states) {
            int encoded_value = QLC_ENCODING[state];
            // 修正2：将 actual_pool_size 改为 current_pool_size
            for (size_t i = 0; i < current_pool_size / state_count; ++i) {
                pool.push_back(encoded_value);
            }
        }
        
        // 2. 尾部补齐以严格对齐 WL 的状态数 (会有极微弱的状态不均，但在物理允许范围内)
        for(size_t i = 0, j = current_pool_size; j < wl_states_count; ++i, ++j) {
            pool.push_back(QLC_ENCODING[allowed_states[i]]); 
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
// Page 处理模块 (模拟同一个 WordLine 上的 4 个 Page)
// ==========================================
class PageProcessor {
private:
    uint8_t* block_ptr;            // 指向最终的 Block 大数组
    size_t block_offset = 0;       // 当前在 Block 中的写入偏移量

    // 4 个物理 Page 的数据缓冲区
    std::vector<uint8_t> page_lp, page_mp, page_up, page_xp;
    size_t page_pos = 0;

    // 字节拼接时的临时比特缓存
    uint8_t cur_lp = 0, cur_mp = 0, cur_up = 0, cur_xp = 0;
    int bit_count = 0;

public:
    PageProcessor(uint8_t* target_block) : block_ptr(target_block), 
        page_lp(PAGE_SIZE), page_mp(PAGE_SIZE), page_up(PAGE_SIZE), page_xp(PAGE_SIZE) {}

    // 处理每次生成的 4-bit 编码值
    void process(int encoded_val) {
        // 拆解出单个 bit，对应相应的 Page
        // encoded_val 的比特位： bit3=XP, bit2=UP, bit1=MP, bit0=LP
        uint8_t xp = (encoded_val >> 3) & 1;
        uint8_t up = (encoded_val >> 2) & 1;
        uint8_t mp = (encoded_val >> 1) & 1;
        uint8_t lp = encoded_val & 1;

        // 将位合入当前字节 (按照从高位到低位 MSB->LSB 的顺序存放)
        cur_xp = (cur_xp << 1) | xp;
        cur_up = (cur_up << 1) | up;
        cur_mp = (cur_mp << 1) | mp;
        cur_lp = (cur_lp << 1) | lp;
        
        bit_count++;

        // 如果凑满了一个字节 (8 bits)
        if (bit_count == 8) {
            page_xp[page_pos] = cur_xp;
            page_up[page_pos] = cur_up;
            page_mp[page_pos] = cur_mp;
            page_lp[page_pos] = cur_lp;
            page_pos++;
            bit_count = 0;

            // 如果当前 WordLine 上的 4 个 Page 都已经写满 19136 字节
            // 则按照 NAND 物理下发顺序 (通常是 LP -> MP -> UP -> XP) 刷入 Block 中
            if (page_pos == PAGE_SIZE) {
                std::copy(page_lp.begin(), page_lp.end(), block_ptr + block_offset); block_offset += PAGE_SIZE;
                std::copy(page_mp.begin(), page_mp.end(), block_ptr + block_offset); block_offset += PAGE_SIZE;
                std::copy(page_up.begin(), page_up.end(), block_ptr + block_offset); block_offset += PAGE_SIZE;
                std::copy(page_xp.begin(), page_xp.end(), block_ptr + block_offset); block_offset += PAGE_SIZE;
                
                page_pos = 0; // 重置页内偏移
            }
        }
    }
};

int main() {
    // 1. 指定允许写入的闪存状态集合 (Erase 状态 0)
    std::vector<int> allowed_states = {0, 1,2,3,4,6,7,8,9,10,11,12,13,14,15};
    
    std::cout << "Block Size: " << BLOCK_SIZE << " Bytes" << std::endl;
    std::cout << "Page Size:  " << PAGE_SIZE  << " Bytes" << std::endl;
    std::cout << "Target Generates: " << TOTAL_GENERATE_COUNT << " times" << std::endl;
    std::cout << "Allowed WL States: ";
    for (int s : allowed_states) std::cout << s << " ";
    std::cout << "\n\nInitializing QLC Data Generator..." << std::endl;

    // 初始化平均生成器
    StrictAverageGenerator generator(allowed_states);

    // 2. 申请 Block 级别的内存
    std::vector<uint8_t> block_data(BLOCK_SIZE);
    PageProcessor processor(block_data.data());

    // 3. 生成 Block 数据
    std::cout << "Generating block data, please wait..." << std::endl;
    for (size_t i = 0; i < TOTAL_GENERATE_COUNT; ++i) {
        int encoded_val = generator.next_value();
        processor.process(encoded_val);
    }
    std::cout << "Progress: 100% done." << std::endl;

    // 4. 将 Block 数据刷入 Linux 磁盘文件
    std::string out_filename = "";
    for (size_t i = 0; i < allowed_states.size(); ++i) {
        out_filename += std::to_string(allowed_states[i]); // 数字转字符串
        if (i < allowed_states.size() - 1) {
            out_filename += "_"; // 如果不是最后一个数字，加下划线分隔
        }
    }
    out_filename += ".bin"; // 添加后缀

    std::cout << "Writing to disk: " << out_filename << " ..." << std::endl;
    
    std::ofstream outfile(out_filename, std::ios::binary);
    if (!outfile) {
        std::cerr << "Error: Cannot open file for writing!" << std::endl;
        return 1;
    }
    
    outfile.write(reinterpret_cast<const char*>(block_data.data()), BLOCK_SIZE);
    outfile.close();

    std::cout << "Block file written successfully!" << std::endl;
    return 0;
}

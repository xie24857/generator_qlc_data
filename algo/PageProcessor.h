#ifndef PAGEPROCESSOR_H
#define PAGEPROCESSOR_H

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>

// ==========================================
//  Page 处理模块（按 bitsPerCell 参数化，支持 QLC/TLC/MLC/SLC）
//  每个 WL 调用一次 reset -> process*cells_per_wl -> flush
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
    PageProcessor(uint8_t* target_block, size_t page_size);

    // 开始一个新 WL：设定该 WL 的 bitsPerCell，重置缓冲
    void reset(int bitsPerCell);

    void process(int encoded_val);

    // 结束当前 WL：把 bits_per_cell 个完整页刷入 block
    void flush();
};

#endif // PAGEPROCESSOR_H

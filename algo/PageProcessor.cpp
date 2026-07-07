#include "algo/PageProcessor.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>

PageProcessor::PageProcessor(uint8_t* target_block, size_t page_size)
    : block_ptr(target_block), block_offset(0), page_size(page_size),
      bits_per_cell(0), bit_count(0), byte_pos(0) {}

void PageProcessor::reset(int bitsPerCell) {
    bits_per_cell = bitsPerCell;
    pages.assign(bits_per_cell, std::vector<uint8_t>(page_size));
    cur_bytes.assign(bits_per_cell, 0);
    bit_count = 0;
    byte_pos = 0;
}

void PageProcessor::process(int encoded_val) {
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

void PageProcessor::flush() {
    for (int b = 0; b < bits_per_cell; ++b) {
        std::copy(pages[b].begin(), pages[b].end(), block_ptr + block_offset);
        block_offset += page_size;
    }
}

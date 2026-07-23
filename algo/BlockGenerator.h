#ifndef BLOCKGENERATOR_H
#define BLOCKGENERATOR_H

#include "algo/BaseGenerator.h"
#include "config/Config.h"

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

// ==========================================
//  Block 生成器：封装"准备 per-type 生成器 + WL 循环"
//  main 只需调 printInfo() 和 generate()
// ==========================================
class BlockGenerator {
public:
    // 构造时完成所有 per-type 生成器的准备
    BlockGenerator(const DeviceConfig& device, size_t cells_per_wl);
    ~BlockGenerator();  // 必须在 .cpp 中定义，使 unique_ptr<BaseGenerator> 析构可见完整类型

    // 打印每个 cell type 的策略信息
    void printInfo() const;

    // 执行生成：把结果写入 block_data（须预先分配 block_size 字节）
    void generate(uint8_t* block_data) const;

private:
    struct CellTypeGenerator {
        int bitsPerCell;
        std::map<int, size_t> wl_state_counts;  // 每个 WL 各状态的 cell 数量
        std::string              position_mode;  // "none" / "parity" / "half"
        std::string              parity_map;     // "default" / "swap"
        std::unique_ptr<BaseGenerator> cell_pool;
    };

    // 计算每个 WordLine 中各状态的数量（position_mode 非 none 时等量）
    std::map<int, size_t> buildStateCounts(
        const DeviceConfig::TypeStrategy& strategy, size_t num_states);

    const DeviceConfig& device;
    size_t cells_per_wl;
    std::unordered_map<std::string, CellTypeGenerator> generators;
    std::vector<CellTypeGenerator*> wl_type;  // WL -> CellTypeGenerator 查表
};

#endif // BLOCKGENERATOR_H

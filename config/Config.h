#ifndef CONFIG_H
#define CONFIG_H

#include "threadpool/ThreadPool.h"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

struct DeviceConfig {
    // 单种 cell type 的策略（QLC/TLC/MLC/SLC 共用结构）
    struct TypeStrategy {
        std::string              mode;           // "ratio" 或 "count"
        std::map<int, size_t>    state_values;   // state -> 权重或数量（仅非零项）
        std::string              position_mode;  // "none"(默认) / "parity" / "half"
        std::string              parity_map;     // "default" / "swap"（position_mode 非 none 时有效）
    };

    std::string                        cell_type;        // QLC / TLC / MLC / SLC
    size_t                             wordline_count;   // WordLine 总数
    size_t                             page_count_in_block;
    size_t                             page_size;
    std::map<std::string, std::vector<std::pair<size_t, size_t>>> wl_ranges;  // qlc/tlc/mlc/slc -> WL 区间列表
    std::map<std::string, std::vector<int>> encodings;   // qlc/tlc/mlc/slc -> Gray code 编码
    std::map<std::string, TypeStrategy> strategies;      // qlc/tlc/mlc/slc -> 策略
};

struct ThreadingConfig {
    int         threadCount;
    ThreadPool* threadPool = nullptr;
};

struct Config {
    DeviceConfig    device;
    ThreadingConfig threading;
    std::string    filename;   // 输出文件名

    void load();               // 从 Ini::instance() 加载全部配置
};

extern Config g_config;

#endif // CONFIG_H

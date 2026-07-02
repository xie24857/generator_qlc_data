#ifndef CONFIG_H
#define CONFIG_H

#include <cstdint>
#include <map>
#include <string>
#include <vector>

struct FlashConfig {
    size_t              page_count_in_block;
    size_t              page_size;
    std::vector<int>    encoding;   // 16 个值，Gray code 编码
};

struct StrategyConfig {
    std::string              mode;          // "ratio" 或 "count"
    std::map<int, size_t>    state_values;  // state -> 权重或数量（仅非零项）
};

struct LoggerConfig {
    std::string level;       // TRACE / DEBUG / INFO / WARN / ERROR / FATAL
    bool        quiet;
    bool        thread_safe;
    bool        color;
};

struct Config {
    FlashConfig    flash;
    StrategyConfig strategy;
    std::string    filename;   // 输出文件名
    LoggerConfig   logger;

    void load();               // 从 Ini::instance() 加载全部配置
};

extern Config g_config;

#endif // CONFIG_H

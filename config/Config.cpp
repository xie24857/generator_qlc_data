#include "config/Config.h"
#include "ini/Ini.h"
#include "logger/Logger.h"

#include <cctype>
#include <cstdlib>
#include <functional>
#include <sstream>
#include <stdexcept>
#include <string>

Config g_config;

// ============================================================
//  辅助：生成文件名
// ============================================================
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

// ============================================================
//  loadLogger
// ============================================================
static void loadLogger(LoggerConfig& cfg)
{
    Ini&    ini = Ini::instance();
    Logger& log = Logger::instance();

    // level
    {
        auto r = ini.getString("logger", "level");
        if (!r.first)
            throw std::runtime_error("缺少 [logger] level");
        std::string s = r.second;
        for (char& c : s)
            c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));

        if      (s == "TRACE") log.set_level(Logger::TRACE);
        else if (s == "DEBUG") log.set_level(Logger::DEBUG);
        else if (s == "INFO")  log.set_level(Logger::INFO);
        else if (s == "WARN")  log.set_level(Logger::WARN);
        else if (s == "ERROR") log.set_level(Logger::ERROR);
        else if (s == "FATAL") log.set_level(Logger::FATAL);
        else throw std::runtime_error("[logger] level 非法值: " + r.second);

        cfg.level = s;
    }

    // quiet
    {
        auto r = ini.getBool("logger", "quiet");
        if (!r.first) throw std::runtime_error("缺少 [logger] quiet");
        log.set_quiet(r.second);
        cfg.quiet = r.second;
    }

    // thread_safe
    {
        auto r = ini.getBool("logger", "thread_safe");
        if (!r.first) throw std::runtime_error("缺少 [logger] thread_safe");
        log.set_thread_safe(r.second);
        cfg.thread_safe = r.second;
    }

    // color
    {
        auto r = ini.getBool("logger", "color");
        if (!r.first) throw std::runtime_error("缺少 [logger] color");
        log.set_color(r.second);
        cfg.color = r.second;
    }
}

// ============================================================
//  loadFlash
// ============================================================
static void loadFlash(FlashConfig& cfg)
{
    Ini& ini = Ini::instance();

    // page_count_in_block
    {
        auto r = ini.getInt("flash", "page_count_in_block");
        if (!r.first)
            throw std::runtime_error("缺少 [flash] page_count_in_block");
        cfg.page_count_in_block = static_cast<size_t>(r.second);
        if (cfg.page_count_in_block % 4 != 0)
            throw std::runtime_error("page_count_in_block 必须是 4 的倍数");
    }

    // page_size
    {
        auto r = ini.getInt("flash", "page_size");
        if (!r.first)
            throw std::runtime_error("缺少 [flash] page_size");
        cfg.page_size = static_cast<size_t>(r.second);
    }

    // encoding
    {
        auto str = ini.getString("flash", "encoding");
        if (!str.first)
            throw std::runtime_error("缺少 [flash] encoding");

        // 本地解析逗号分隔的整数列表
        std::vector<long> enc_values;
        std::istringstream ss(str.second);
        std::string token;
        while (std::getline(ss, token, ',')) {
            char* end;
            long n = std::strtol(token.c_str(), &end, 0);
            if (end == token.c_str())
                throw std::runtime_error("encoding 含非法值: " + token);
            enc_values.push_back(n);
        }

        if (enc_values.size() != 16)
            throw std::runtime_error("encoding 必须恰好 16 个值");

        // 校验是 0-15 的排列（每个值出现恰好一次）
        std::vector<bool> seen(16, false);
        for (long v : enc_values) {
            if (v < 0 || v > 15)
                throw std::runtime_error("encoding 值必须在 0-15 范围内: " + std::to_string(v));
            if (seen[v])
                throw std::runtime_error("encoding 中有重复值: " + std::to_string(v));
            seen[v] = true;
        }

        for (long v : enc_values)
            cfg.encoding.push_back(static_cast<int>(v));

        // 校验格雷码：相邻状态的编码只差 1 bit
        for (int i = 0; i < 15; i++) {
            int diff = cfg.encoding[i] ^ cfg.encoding[i + 1];
            if (diff == 0 || (diff & (diff - 1)) != 0)
                throw std::runtime_error("encoding 不是有效格雷码: 状态 " +
                    std::to_string(i) + " 和 " + std::to_string(i + 1) +
                    " 的编码差异不是 1 bit");
        }
    }
}

// ============================================================
//  loadStrategy
// ============================================================
static void loadStrategy(StrategyConfig& cfg, const FlashConfig& flash)
{
    Ini& ini = Ini::instance();

    // mode
    {
        auto r = ini.getString("strategy", "mode");
        if (!r.first)
            throw std::runtime_error("缺少 [strategy] mode");
        cfg.mode = r.second;
        if (cfg.mode != "ratio" && cfg.mode != "count")
            throw std::runtime_error("strategy.mode 必须是 ratio 或 count");
    }

    // 读取 0-15 的值
    for (int i = 0; i <= 15; i++) {
        auto val = ini.getInt("strategy", std::to_string(i));
        if (!val.first)
            throw std::runtime_error("缺少 [strategy] " + std::to_string(i));
        if (val.second < 0)
            throw std::runtime_error("strategy." + std::to_string(i) + " 不能为负数");
        if (val.second > 0)
            cfg.state_values[i] = static_cast<size_t>(val.second);
    }
    if (cfg.state_values.empty())
        throw std::runtime_error("至少需要一个非零状态");

    // count 模式校验总和
    if (cfg.mode == "count") {
        size_t total = 0;
        for (const auto& p : cfg.state_values)
            total += p.second;
        size_t expected = flash.page_size * 8;
        if (total != expected)
            throw std::runtime_error("count 模式总和 " + std::to_string(total) +
                                     " 不等于 page_size*8=" + std::to_string(expected));
    }
}

// ============================================================
//  loadOutput
// ============================================================
static void loadOutput(std::string& filename, const StrategyConfig& strategy)
{
    Ini& ini = Ini::instance();

    auto fname = ini.getString("output", "filename");
    if (fname.first && !fname.second.empty())
        filename = fname.second;
    else
        filename = generateFilename(strategy);
}

// ============================================================
//  Config::load
// ============================================================

void Config::load()
{
    loadLogger(logger);
    loadFlash(flash);
    loadStrategy(strategy, flash);
    loadOutput(filename, strategy);
}

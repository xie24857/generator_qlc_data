#include "config/Config.h"
#include "ini/Ini.h"
#include "logger/Logger.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <ctime>
#include <functional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>

Config g_config;

// ============================================================
//  辅助：解析 "[a,b],[c,d]" 格式的区间列表
// ============================================================
static std::vector<std::pair<size_t, size_t>> parseRegion(const std::string& s)
{
    std::vector<std::pair<size_t, size_t>> ranges;
    size_t i = 0;
    while (i < s.size()) {
        while (i < s.size() && s[i] != '[') i++;
        if (i >= s.size()) break;
        i++;

        char* end;
        long start = std::strtol(s.c_str() + i, &end, 10);
        if (end == s.c_str() + i)
            throw std::runtime_error("region 解析失败，期望数字: " + s);
        i = end - s.c_str();

        while (i < s.size() && (s[i] == ',' || s[i] == ' ')) i++;

        long range_end = std::strtol(s.c_str() + i, &end, 10);
        if (end == s.c_str() + i)
            throw std::runtime_error("region 解析失败，期望数字: " + s);
        i = end - s.c_str();

        while (i < s.size() && s[i] != ']') i++;
        if (i < s.size()) i++;

        if (start > range_end)
            throw std::runtime_error("region 区间逆序: [" +
                std::to_string(start) + "," + std::to_string(range_end) + "]");
        ranges.push_back({static_cast<size_t>(start), static_cast<size_t>(range_end)});
    }
    return ranges;
}

// 合法 cell type 前缀、状态数、每 cell 比特数
static const struct { const char* prefix; int stateCount; int bitsPerCell; } kCellTypes[] = {
    {"qlc", 16, 4},
    {"tlc",  8, 3},
    {"mlc",  4, 2},
    {"slc",  2, 1},
};

// ============================================================
//  loadLogger
// ============================================================
static void loadLogger()
{
    Ini& ini = Ini::instance();
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
    }

    // quiet
    {
        auto r = ini.getBool("logger", "quiet");
        if (!r.first) throw std::runtime_error("缺少 [logger] quiet");
        log.set_quiet(r.second);
    }

    // thread_safe
    {
        auto r = ini.getBool("logger", "thread_safe");
        if (!r.first) throw std::runtime_error("缺少 [logger] thread_safe");
        log.set_thread_safe(r.second);
    }

    // color
    {
        auto r = ini.getBool("logger", "color");
        if (!r.first) throw std::runtime_error("缺少 [logger] color");
        log.set_color(r.second);
    }
}

// ============================================================
//  loadThreading
// ============================================================
static void loadThreading(ThreadingConfig& th)
{
    auto r = Ini::instance().getInt("threading", "thread_count");
    if (!r.first)
        throw std::runtime_error("缺少 [threading] thread_count");

    int val = static_cast<int>(r.second);
    if (val == 0) {
        th.threadCount = 0;
        return;
    }

    int nproc = static_cast<int>(std::thread::hardware_concurrency());
    if (nproc <= 0) nproc = 0;
    int maxThreads = (nproc > 0) ? nproc * 2 : 64;

    if (val < 1 || val > maxThreads)
        throw std::runtime_error(
            "[threading] thread_count 超出范围 [1, " + std::to_string(maxThreads)
            + "] (0=关闭): " + std::to_string(val));

    th.threadCount = val;
}

// ============================================================
//  loadOutput
// ============================================================
static void loadOutput(std::string& filename, const DeviceConfig& device)
{
    Ini& ini = Ini::instance();

    auto fname = ini.getString("output", "filename");
    if (fname.first && !fname.second.empty()) {
        filename = fname.second;
    } else {
        std::time_t now = std::time(nullptr);
        char buf[32];
        std::strftime(buf, sizeof(buf), "%m%d_%H%M%S", std::localtime(&now));
        filename = std::string(buf) + "_" + device.cell_type + ".bin";
    }
}

// ============================================================
//  loadDevice
// ============================================================
static void loadDevice(DeviceConfig& cfg)
{
    Ini& ini = Ini::instance();

    // cell_type
    {
        auto r = ini.getString("device", "cell_type");
        if (!r.first)
            throw std::runtime_error("缺少 [device] cell_type");
        cfg.cell_type = r.second;
        std::string ct = cfg.cell_type;
        for (char& c : ct)
            c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
        if (ct != "QLC" && ct != "TLC" && ct != "MLC" && ct != "SLC")
            throw std::runtime_error("[device] cell_type 非法值: " + r.second +
                "（支持 QLC/TLC/MLC/SLC）");
        cfg.cell_type = ct;
    }

    // page_count_in_block：须为 cell_type 每 cell 比特数的倍数
    {
        auto r = ini.getInt("device", "page_count_in_block");
        if (!r.first)
            throw std::runtime_error("缺少 [device] page_count_in_block");
        cfg.page_count_in_block = static_cast<size_t>(r.second);

        int bitsPerCell = 0;
        for (const auto& ct : kCellTypes) {
            if (ct.prefix == cfg.cell_type) {
                bitsPerCell = ct.bitsPerCell;
                break;
            }
        }
        if (bitsPerCell > 0 && cfg.page_count_in_block % bitsPerCell != 0)
            throw std::runtime_error("[device] page_count_in_block 必须是 " +
                std::to_string(bitsPerCell) + " 的倍数（" + cfg.cell_type + " 每 cell " +
                std::to_string(bitsPerCell) + " bit）");
    }

    // page_size
    {
        auto r = ini.getInt("device", "page_size");
        if (!r.first)
            throw std::runtime_error("缺少 [device] page_size");
        cfg.page_size = static_cast<size_t>(r.second);
    }

    // wordline_count
    {
        auto r = ini.getInt("device", "wordline_count");
        if (!r.first)
            throw std::runtime_error("缺少 [device] wordline_count");
        cfg.wordline_count = static_cast<size_t>(r.second);
    }

    // 各 cell type 的字段须同时存在或同时不存在
    // 先探测哪些 cell type 已配置，再统一加载
    for (const auto& ct : kCellTypes) {
        bool hasEncoding = ini.getString("device", std::string(ct.prefix) + "_encoding").first;
        bool hasRegion   = ini.getString("device", std::string(ct.prefix) + "_region").first;
        bool hasMode     = ini.getString("device", std::string(ct.prefix) + "_mode").first;

        // 任一字段存在则全部必须存在
        if (hasEncoding || hasRegion || hasMode) {
            if (!hasEncoding)
                throw std::runtime_error("[device] " + std::string(ct.prefix) +
                    " 字段不完整：缺少 " + std::string(ct.prefix) + "_encoding（encoding/region/mode 须同时存在）");
            if (!hasRegion)
                throw std::runtime_error("[device] " + std::string(ct.prefix) +
                    " 字段不完整：缺少 " + std::string(ct.prefix) + "_region（encoding/region/mode 须同时存在）");
            if (!hasMode)
                throw std::runtime_error("[device] " + std::string(ct.prefix) +
                    " 字段不完整：缺少 " + std::string(ct.prefix) + "_mode（encoding/region/mode 须同时存在）");
        } else {
            continue;  // 全部不存在，跳过
        }

        // ---- encoding ----
        {
            std::string key = std::string(ct.prefix) + "_encoding";
            auto str = ini.getString("device", key);

            // 逗号替换为空格，再用 >> 直接读取
            std::string s = str.second;
            for (char& c : s)
                if (c == ',') c = ' ';

            std::vector<int> enc_values;
            std::istringstream ss(s);
            int n;
            while (ss >> n)
                enc_values.push_back(n);

            if (ss.fail() && !ss.eof())
                throw std::runtime_error(key + " 含非法值: " + str.second);

            if (enc_values.size() != static_cast<size_t>(ct.stateCount))
                throw std::runtime_error(key + " 必须恰好 " +
                    std::to_string(ct.stateCount) + " 个值");

            std::vector<bool> seen(ct.stateCount, false);
            for (int v : enc_values) {
                if (v < 0 || v >= ct.stateCount)
                    throw std::runtime_error(key + " 值必须在 0-" +
                        std::to_string(ct.stateCount - 1) + " 范围内: " + std::to_string(v));
                if (seen[v])
                    throw std::runtime_error(key + " 中有重复值: " + std::to_string(v));
                seen[v] = true;
            }

            std::vector<int> encoding = enc_values;

            for (int i = 0; i < ct.stateCount - 1; i++) {
                int diff = encoding[i] ^ encoding[i + 1];
                if (diff == 0 || (diff & (diff - 1)) != 0)
                    throw std::runtime_error(key + " 不是有效格雷码: 状态 " +
                        std::to_string(i) + " 和 " + std::to_string(i + 1) +
                        " 的编码差异不是 1 bit");
            }

            cfg.encodings[ct.prefix] = encoding;
        }

        // ---- region ----
        {
            std::string key = std::string(ct.prefix) + "_region";
            auto str = ini.getString("device", key);
            cfg.wl_ranges[ct.prefix] = parseRegion(str.second);
        }

        // ---- strategy (mode + state_0..N) ----
        {
            std::string modeKey = std::string(ct.prefix) + "_mode";
            auto modeR = ini.getString("device", modeKey);

            DeviceConfig::TypeStrategy cts;
            cts.mode = modeR.second;
            if (cts.mode != "ratio" && cts.mode != "count")
                throw std::runtime_error("[device] " + modeKey + " 非法值: " + modeR.second +
                    "（应为 ratio 或 count）");

            for (int i = 0; i < ct.stateCount; ++i) {
                std::string key = std::string(ct.prefix) + "_" + std::to_string(i);
                auto val = ini.getInt("device", key);
                if (!val.first)
                    throw std::runtime_error("[device] 缺少 " + key);
                if (val.second < 0)
                    throw std::runtime_error("[device] " + key + " 不能为负数");
                if (val.second > 0)
                    cts.state_values[i] = static_cast<size_t>(val.second);
            }
            if (cts.state_values.empty())
                throw std::runtime_error("[device] " + std::string(ct.prefix) + " 至少需要一个非零状态");

            if (cts.mode == "count") {
                size_t total = 0;
                for (const auto& p : cts.state_values)
                    total += p.second;
                size_t expected = cfg.page_size * 8;
                if (total != expected)
                    throw std::runtime_error("[device] " + std::string(ct.prefix) +
                        " count 总和 " + std::to_string(total) +
                        " 不等于 page_size*8=" + std::to_string(expected));
            }

            // position_mode（可选，默认 none）
            {
                std::string key = std::string(ct.prefix) + "_position_mode";
                auto r = ini.getString("device", key);
                cts.position_mode = r.first ? r.second : "none";
                if (cts.position_mode != "none"
                    && cts.position_mode != "parity"
                    && cts.position_mode != "half")
                    throw std::runtime_error("[device] " + key + " 非法值: " +
                        cts.position_mode + "（应为 none/parity/half）");
            }

            // parity_map（可选，默认 default）
            {
                std::string key = std::string(ct.prefix) + "_parity_map";
                auto r = ini.getString("device", key);
                cts.parity_map = r.first ? r.second : "default";
                if (cts.parity_map != "default" && cts.parity_map != "swap")
                    throw std::runtime_error("[device] " + key + " 非法值: " +
                        cts.parity_map + "（应为 default/swap）");
            }

            cfg.strategies[ct.prefix] = std::move(cts);
        }
    }

    // 校验 WL 范围全覆盖无重叠
    {
        std::vector<std::pair<size_t, size_t>> all_ranges;
        for (const auto& kv : cfg.wl_ranges)
            for (const auto& r : kv.second)
                all_ranges.push_back(r);
        std::sort(all_ranges.begin(), all_ranges.end());

        size_t expected_end = cfg.wordline_count;
        size_t cursor = 0;
        for (const auto& r : all_ranges) {
            if (r.first > cursor)
                throw std::runtime_error("WL 范围有空隙: [" + std::to_string(cursor) +
                    "," + std::to_string(r.first - 1) + "] 未被覆盖");
            if (r.first < cursor)
                throw std::runtime_error("WL 范围重叠: [" + std::to_string(r.first) +
                    "," + std::to_string(r.second) + "]");
            cursor = std::max(cursor, r.second + 1);
        }
        if (cursor != expected_end)
            throw std::runtime_error("WL 范围未覆盖到 wordline_count: 最后覆盖到 " +
                std::to_string(cursor - 1) + "，需要覆盖到 " + std::to_string(expected_end - 1));
    }
}

// ============================================================
//  Config::load
// ============================================================

void Config::load()
{
    loadLogger();
    loadDevice(device);
    loadThreading(threading);
    loadOutput(filename, device);
}

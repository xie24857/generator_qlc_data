#include "ini/Ini.h"
#include "config/Config.h"
#include "logger/Logger.h"
#include "algo/BlockGenerator.h"

#include <fstream>
#include <iostream>
#include <vector>

// ==========================================
//  main
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

    size_t block_size = g_config.device.page_count_in_block * g_config.device.page_size;
    size_t cells_per_wl = g_config.device.page_size * 8;
    size_t total_cells = g_config.device.wordline_count * cells_per_wl;

    // 2. 打印配置信息
    log_info("Config File:    %s", config_file.c_str());
    log_info("Block Size:     %zu Bytes", block_size);
    log_info("Page Size:      %zu Bytes", g_config.device.page_size);
    log_info("Page Count:     %zu", g_config.device.page_count_in_block);
    log_info("Wordline Count: %zu", g_config.device.wordline_count);
    log_info("Cells per WL:   %zu", cells_per_wl);
    log_info("Total Cells:    %zu", total_cells);

    // 3. 准备生成器并执行
    BlockGenerator generator(g_config.device, cells_per_wl);
    generator.printInfo();

    std::vector<uint8_t> block_data(block_size);
    generator.generate(block_data.data());

    // 4. 写入文件
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

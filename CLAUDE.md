# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Overview

QLC NAND Flash block data generator. Produces binary files that simulate how a QLC (4-bit-per-cell) NAND flash block is physically laid out, using configurable cell-state distributions and Gray code encoding.

## Build & Run

```bash
mkdir -p build && cd build
cmake ..
make
# 运行（从 build 目录）:
./qlc_datagen ../config.ini
```

If no config file is specified, `config.ini` is used by default. Generated `.bin` files, `qlc_datagen` binary, and `build/` directory are gitignored.

## Architecture

### Directory structure

```
generator/
├── ini/             # INI 配置解析器（单例，从 nand-flash-analyzer 移植）
│   ├── Ini.h
│   └── Ini.cpp
├── logger/          # 分级彩色日志（rxi/log.c 的 C++11 重写，从 nand-flash-analyzer 移植）
│   ├── Logger.h
│   └── Logger.cpp
├── config/          # 领域配置加载
│   ├── Config.h     # 配置结构体定义（FlashConfig, StrategyConfig, LoggerConfig, Config）
│   └── Config.cpp   # 从 Ini::instance() 加载各 section
├── qlc_datagen.cpp  # 主程序：生成器、Page 处理器、main()
├── config.ini        # 配置文件
└── CMakeLists.txt
```

### Data layout model

A QLC cell stores 4 bits belonging to 4 different pages: LP (lowest), MP, UP, XP (highest). The generator produces a block as 4 sequential page regions: all LP bytes first, then all MP, then all UP, then all XP — i.e., `[LP_data][MP_data][UP_data][XP_data]`. Each cell contributes 1 bit to each of the 4 pages, packed into bytes in the order cells are generated.

Because each cell spans 4 pages, the total number of cells in a block is `(block_bytes * 8) / 4`. This is also why `page_count_in_block` must be a multiple of 4: the block is evenly divided into 4 equal-sized page-type regions, each holding `page_count_in_block / 4` pages.

### State encoding

Each QLC cell is in one of 16 threshold-voltage states (0–15). The `encoding` config maps each logical state to a 4-bit value. Adjacent states must differ by exactly 1 bit (Gray code), validated at startup. The 4 bits of the encoded value map to:
- Bit 0 → LP page
- Bit 1 → MP page
- Bit 2 → UP page
- Bit 3 → XP page

### Source files

- **`qlc_datagen.cpp`** — Main program.
  - `computePerWlCounts()` — for `ratio` mode, converts weights to per-wordline cell counts using largest-remainder allocation; for `count` mode, uses the values directly.
  - `StrictAverageGenerator` — shuffles a pool of cells (each assigned its encoded value) per the state distribution; re-shuffles when exhausted.
  - `PageProcessor` — receives encoded values one at a time, packs bits into byte buffers for LP/MP/UP/XP, and flushes complete pages to the output block buffer.
- **`ini/Ini.h` / `ini/Ini.cpp`** — Singleton INI file parser. Supports sections, `=`/`:` separators, `;`/`#` comments (inline only after whitespace), multi-line continuation (leading whitespace), UTF-8 BOM skipping, and case-insensitive keys. Returns `pair<bool, T>` from getters. Access via `Ini::instance()`.
- **`logger/Logger.h` / `logger/Logger.cpp`** — Singleton logger (C++11 rewrite of rxi/log.c). Six levels (TRACE/DEBUG/INFO/WARN/ERROR/FATAL), color output, thread-safe option. Macros: `log_trace`/`log_debug`/`log_info`/`log_warn`/`log_error`/`log_fatal`.
- **`config/Config.h` / `config/Config.cpp`** — Domain config loading. `Config::load()` calls `loadLogger()`, `loadFlash()`, `loadStrategy()`, `loadOutput()` sequentially. Global `g_config` instance.

### Configuration file (`config.ini`)

Four sections:

- **`[flash]`** — `page_count_in_block` (must be multiple of 4), `page_size` (bytes), `encoding` (comma-separated list of exactly 16 values, a permutation of 0–15 forming a valid Gray code).
- **`[strategy]`** — `mode` is either `ratio` (values are weights; cells allocated proportionally with remainder distribution) or `count` (values are exact per-state cell counts per wordline; must sum to `page_size * 8`).
- **`[logger]`** — `level` (TRACE/DEBUG/INFO/WARN/ERROR/FATAL), `quiet` (bool), `thread_safe` (bool), `color` (bool).
- **`[output]`** — `filename` (optional; auto-generated from strategy params if omitted).

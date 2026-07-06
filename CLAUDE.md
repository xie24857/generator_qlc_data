# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Constraints

- **`~/win32/*` 读取自由，无须过问。**
- **修改代码前必须先征得用户同意**，未经允许不得擅自修改任何代码文件。

## Overview

QLC NAND Flash block data generator. Produces binary files that simulate how a QLC (4-bit-per-cell) NAND flash block is physically laid out, using configurable cell-state distributions and Gray code encoding.

## Build & Run

```bash
mkdir -p build && cd build
cmake ..
make
# 运行（从 build 目录）:
./qlc_datagen ../x4config.ini
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
│   ├── Config.h     # 配置结构体定义
│   └── Config.cpp   # 从 Ini::instance() 加载各 section
├── threadpool/      # 线程池（从 nand-flash-analyzer 移植，预留暂未使用）
│   └── ThreadPool.h
├── main.cpp         # 主程序：生成器、Page 处理器、main()
├── x4config.ini     # 配置文件
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

### Invariants (待 Config.cpp 校验)

- **state 数量总和（仅 `count` 模式）**：同一 cell type 下，所有 `{prefix}_N` 值之和必须等于 `page_size × 8`（即该 WL 上的物理 cell 数量）。`ratio` 模式无需满足，加载时自动归一化。
- **WL 范围全覆盖无重叠**：所有 `{prefix}_region` 的并集须恰好覆盖 `[0, wordline_count-1]`，区间之间不得重叠。Config.cpp 加载后须校验。

### Source files

- **`main.cpp`** — Main program.
  - `computePerWlCounts()` — for `ratio` mode, converts weights to per-wordline cell counts using largest-remainder allocation; for `count` mode, uses the values directly.
  - `StrictAverageGenerator` — shuffles a pool of cells (each assigned its encoded value) per the state distribution; re-shuffles when exhausted.
  - `PageProcessor` — receives encoded values one at a time, packs bits into byte buffers for LP/MP/UP/XP, and flushes complete pages to the output block buffer.
- **`ini/Ini.h` / `ini/Ini.cpp`** — Singleton INI file parser. Supports sections, `=`/`:` separators, `;`/`#` comments (inline only after whitespace), multi-line continuation (leading whitespace), UTF-8 BOM skipping, and case-insensitive keys. Returns `pair<bool, T>` from getters. Access via `Ini::instance()`.
- **`logger/Logger.h` / `logger/Logger.cpp`** — Singleton logger (C++11 rewrite of rxi/log.c). Six levels (TRACE/DEBUG/INFO/WARN/ERROR/FATAL), color output, thread-safe option. Macros: `log_trace`/`log_debug`/`log_info`/`log_warn`/`log_error`/`log_fatal`.
- **`config/Config.h` / `config/Config.cpp`** — Domain config loading. `Config::load()` calls `loadLogger()`, `loadDevice()`, `loadStrategy()`, `loadThreading()`, `loadOutput()` sequentially. Global `g_config` instance.
- **`threadpool/ThreadPool.h`** — 线程池（从 nand-flash-analyzer 移植）。`ThreadPool(n)` 启动 n 个 worker，`enqueue(f, args...)` 返回 `future`。当前已接入 CMake（pthread），但生成器暂未使用，留待后续加速。

### Configuration file (`x4config.ini`)

Five sections:

- **`[device]`** — `cell_type` (QLC/TLC/MLC/SLC)，`wordline_count`，`page_count_in_block` (must be multiple of 4)，`page_size` (bytes)，`qlc_encoding` (comma-separated list of exactly 16 values, a permutation of 0–15 forming a valid Gray code)。
- **`[strategy]`** — 单段，包含多种 cell type 的策略，用前缀区分。使用哪种由 `[device] cell_type` 决定（QLC→`qlc_mode`+`qlc_0..qlc_15`，TLC→`tlc_mode`+`tlc_0..tlc_7`，以此类推）。每种 cell type 独立 mode 和状态数。
- **`[threading]`** — `thread_count`（0=单线程，>0=预留多线程，当前未接入生成逻辑）。
- **`[logger]`** — `level` (TRACE/DEBUG/INFO/WARN/ERROR/FATAL), `quiet` (bool), `thread_safe` (bool), `color` (bool).
- **`[output]`** — `filename` (optional; auto-generated from strategy params if omitted).

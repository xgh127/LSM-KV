# LSM-KV · S0 工程骨架

> 一个面向"先写测试、再写实现"（TDD）的 C++ LSM-Tree KV 存储教程工程。
> S0 仅交付**可编译、可跑、可失败但不崩溃**的骨架；具体组件逻辑在 S1+ 逐章用 TDD 填空。

## 1. 设计理念

| 维度 | 决策 | 理由 |
|---|---|---|
| 语言标准 | C++20 | `std::span`、`std::jthread`、`std::filesystem` 全可用，g++ 16 / MSVC 19.29 都完整支持 |
| 最大依赖 | CMake + 自带 `mini_test.hpp` | **零外部依赖**，`cmake -B build && cmake --build build` 必通过，无需 vcpkg |
| 错误处理 | `mini_lsm::Status` 类 | 兼容性优先于 `std::expected`（MSVC 19.29 仍未完整支持） |
| Key/Value | `std::string` 别名 | 二进制安全；后期可换 `std::span<const std::byte>` |
| Tombstone 约定 | `put(k, "") == del(k)` | 全引擎一致，简化语义，对齐 mini-lsm |
| 测试框架 | 自带 gtest 风格 `mini_test.hpp`，可切换系统 gtest | 教学 TDD 红灯阶段不依赖网络下载 |
| 配置 | `LsmOptions` 结构体集中所有"魔法数字" | 不绑定课程 PDF 的硬编码值（如 Bloom 8KB / SST 16KB），保持设计开放 |

## 2. 目录结构

```
LSM-KV/
├── PLAN.md              # 6 阶段完整规划（28 KB）
├── CMakeLists.txt       # 顶层 CMake，零外部依赖
├── include/
│   ├── config.h         # LsmOptions 集中式可配置常量
│   ├── types.h          # Key/Value/Status 公共类型
│   ├── iterator.h       # StorageIterator 抽象基类
│   ├── memtable.h       # MemTable 接口
│   ├── sstable.h        # SSTable + BlockMeta + SSTableIterator
│   ├── sstable_builder.h# SSTableBuilder
│   ├── vlog.h           # WiscKey vLog
│   ├── compaction.h     # CompactionController/Executor 抽象
│   └── lsm_engine.h     # 顶层 LsmEngine 接口
├── src/                 # 所有 .cpp 仅含空实现 + TODO 注释 + 实现建议
│   └── *.cpp
├── tests/
│   ├── mini_test.hpp    # 自带 gtest 风格测试框架
│   ├── test_main.cpp    # ctest 入口
│   └── test_*.cpp       # 七份分组件测试 (TDD 红灯)
└── .gitignore
```

## 3. 三步构建与运行

```bash
# 1. 配置
cmake -B build

# 2. 编译（应当零警告，零错误）
cmake --build build

# 3. 测试（红灯：很多 EXPECT_* 失败，但绝不崩溃或 SegFault）
ctest --test-dir build --output-on-failure
```

预期输出示例：

```
[ RUN      ] MemTable.PutGetRoundTrip
[  FAILED  ] MemTable.PutGetRoundTrip
...
[==========] 12 passed, 35 failed
```

这是 S0 的"健康红灯"状态——`35 failed` 而非 `Segmentation fault`，说明 TDD 红灯基础设施就位。

## 4. 测试矩阵一览（S0 阶段）

| 文件 | 用例数 | S0 期望 | S1 阶段 |
|---|---:|---|---|
| `test_memtable.cpp` | 9 | 多数 RED | 全部 GREEN |
| `test_sstable.cpp` | 5 | 多数 RED | 全部 GREEN |
| `test_sstable_builder.cpp` | 4 | 多数 RED | 全部 GREEN |
| `test_vlog.cpp` | 6 | 4 GREEN + 2 RED | 全部 GREEN |
| `test_iterator.cpp` | 3 | 全部 GREEN（虚派发契约） | 保持 |
| `test_compaction.cpp` | 4 | 全部 GREEN（no-op 契约） | S2 转 GREEN 业务 |
| `test_lsm_engine.cpp` | 10 | 多数 RED | 全部 GREEN |

## 5. 给"填空实现者"的工作模式

每个 `src/*.cpp` 里的函数都遵循统一格式：

```cpp
// TODO(S1): <一句话告诉你怎么写>
// Implementation suggestion:
//   <伪代码 / 简短指引>
Status MemTable::put(...) {
    return Status::OK();   // S0 占位
}
```

### TDD 一个章节的标准节奏

1. 选定一个组件，例如 `MemTable`
2. 打开 `tests/test_memtable.cpp`，读红色测试的契约
3. 打开 `src/memtable.cpp`，按 `TODO(S1)` 注释填空
4. 跑 `ctest --test-dir build --output-on-failure` 直到对应测试全绿
5. 跑 sanitizer 版本：`cmake -B build -DLSM_SANITIZE=ON && cmake --build build`
6. commit 并打 tag `checkpoint/<component>-implemented`

## 6. 主要可配置项（`include/config.h`）

| 选项 | 默认 | 作用 |
|---|---|---|
| `memtable_target_size` | 2 MiB | 触发 freeze 的容量阈值 |
| `num_memtable_limit` | 4 | imm 阈值触发 flush |
| `block_size_bytes` | 4 KiB | SST block 大小 |
| `target_sst_size` | 4 MiB | 单 SST 目标大小 |
| `bloom_filter_enabled` | true | 启用 bloom |
| `bloom_bits_per_key` | 10.0 | bloom 位密度 |
| `compaction_policy` | `kSimpleLeveled` | 压实策略 |
| `enable_wal` | false | 是否启用 WAL（S2 实现） |
| `direct_io` / `use_async_io` | false | S4 优化开关 |

## 7. CMake 选项速查

```bash
cmake -B build \
  -DLSM_BUILD_TESTS=ON \         # 默认 ON
  -DLSM_USE_SYSTEM_GTEST=OFF \    # 默认 OFF（用自带 mini_test）
  -DLSM_WARNINGS_AS_ERRORS=ON \  # 严格模式，验证代码质量
  -DLSM_SANITIZE=ON              # 启 ASan/UBSan
```

## 8. 与两份参考项目的关系

| 参考项目 | 取其精华 | 去其糟粕 |
|---|---|---|
| `mini-lsm` (Rust) | 21 章方法论、CoW snapshot 两阶段锁、Merge/TwoMergeIterator、tombstone 全引擎语义、CRUSH 顺序 | Rust GAT 的 self-ref 复杂度（Vlog 直接用 shared_ptr 解决）、moka 依赖（自带 LRU 替代） |
| `LSM-c++` 课程项目 | 端到端 `correctness_test` / `persistence_test` 风格、`gc(chunk_size)` API | 硬编码的 Bloom 8KB / SST 16KB / Level 0 = 2 文件限制（一律抽象为 `LsmOptions`） |

## 9. 后续阶段

详见 [`PLAN.md`](PLAN.md)。S1 在此骨架上按章节填空即可，骨架本身不需要再改。

## 10. 简历一句话（S0 完成后即可写）

> 构建并开源 `LSM-KV`：现代 C++20 LSM-Tree 教程工程的开篇 S0 骨架；零外部依赖、自带 gtest 风格测试框架；定义 MemTable / SSTable / vLog / Compaction / Iterator / LsmEngine 全套接口契约与 49 个用例（含 35+ 红灯测试）；为 6 阶段（S1 单引擎 → S4 性能优化 → S5 扩展）的 TDD 演进奠定工程基础。
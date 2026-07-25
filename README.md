# LSM-KV

一个基于 LSM-Tree 的键值存储系统（C++20）。

## 快速开始

```bash
cd LSM-KV
cmake -B build -G "MinGW Makefiles" -DCMAKE_CXX_COMPILER=g++
cmake --build build
ctest --test-dir build --output-on-failure
```

## 测试

### 运行全部测试

```bash
ctest --test-dir build --output-on-failure
```

### 按组件运行

```bash
ctest --test-dir build --output-on-failure -R memtable
ctest --test-dir build --output-on-failure -R sstable
ctest --test-dir build --output-on-failure -R sstable_builder
ctest --test-dir build --output-on-failure -R vlog
ctest --test-dir build --output-on-failure -R iterator
ctest --test-dir build --output-on-failure -R compaction
ctest --test-dir build --output-on-failure -R lsm_engine
```

### 直接运行测试二进制（输出更简洁）

```bash
.\build\tests\test_memtable.exe
.\build\tests\test_sstable.exe
.\build\tests\test_sstable_builder.exe
.\build\tests\test_vlog.exe
.\build\tests\test_iterator.exe
.\build\tests\test_compaction.exe
.\build\tests\test_lsm_engine.exe
```

### 从测试输出查找关键信息

```powershell
# 只看失败
.\build\tests\test_memtable.exe 2>&1 | Select-String -Pattern "FAILED"

# 只看通过/失败总计
.\build\tests\test_memtable.exe 2>&1 | Select-String -Pattern "passed|failed"
```

### 所有组件

| 组件 | 接口 | 测试数 | 说明 |
|---|---|---|---|
| MemTable | `put/get/del/scan/flush_to` | 21 | 内存写入缓冲区 |
| SSTableBuilder | `add/finish` | 6 | SSTable 构建器，`finish` 写入真实文件 |
| SSTable | `open/may_contain/find_block_idx/read_block` | 5 | SSTable 读取，`open` 解析 footer+meta |
| VLog | `open/append/read_at/gc` | 7 | 值日志，`append`/`read_at` 真实 CRC 校验 |
| Iterator | `is_valid/key/value/next` | 3 | 迭代器抽象 |
| Compaction | `plan/execute` | 4 | 压缩调度（S2） |
| LsmEngine | `put/get/del/scan/freeze/flush/reset` | 15 | 引擎入口，`flush` 写入真实 SST，`get` 从 SST 读取 |
| **总计** | | **61** | **100% passed, 0 failed** |

### 日常开发流程

```bash
# 改代码 → 编译 → 跑对应组件测试
cmake --build build && .\build\tests\test_组件名.exe

# 确认无回归
ctest --test-dir build --output-on-failure

# 全量清理重建
Remove-Item -Recurse -Force build
cmake -B build -G "MinGW Makefiles" -DCMAKE_CXX_COMPILER=g++
cmake --build build
ctest --test-dir build --output-on-failure
```

### 测试框架

自研 `mini_test.hpp`（`tests/mini_test.hpp`），零外部依赖。提供类 gtest 宏：

```cpp
TEST(Suite, Name) {
    EXPECT_EQ(a, b);
    EXPECT_TRUE(condition);
    ASSERT_TRUE(condition);  // 失败时终止当前用例
    EXPECT_FALSE(condition);
}
```

## 项目结构

```
LSM-KV/
├── include/           # 公共头文件（接口定义）
│   ├── memtable.h
│   ├── sstable.h
│   ├── sstable_builder.h
│   ├── vlog.h
│   ├── iterator.h
│   ├── compaction.h
│   ├── lsm_engine.h
│   ├── config.h
│   └── types.h
├── src/               # 实现
│   ├── memtable.cpp
│   ├── sstable.cpp
│   ├── sstable_builder.cpp
│   ├── vlog.cpp
│   ├── iterator.cpp
│   ├── compaction.cpp
│   └── lsm_engine.cpp
├── tests/             # 测试
│   ├── mini_test.hpp
│   ├── test_main.cpp
│   ├── test_memtable.cpp
│   ├── test_sstable.cpp
│   ├── test_sstable_builder.cpp
│   ├── test_vlog.cpp
│   ├── test_iterator.cpp
│   ├── test_compaction.cpp
│   └── test_lsm_engine.cpp
├── doc/               # 文档
│   ├── PLAN.md        # 完整规划（6 个 Stage）
│   ├── NOTES.md       # 学习笔记
│   ├── s0/            # S0 实现指南
│   │   ├── memtable_impl_guide.md
│   │   ├── sstable_builder_impl_guide.md
│   │   ├── vlog_impl_guide.md
│   │   └── lsm_engine_impl_guide.md
│   └── s1/            # S1 实现指南
│       ├── PLAN.md
│       ├── sstable_impl_guide.md
│       ├── sstable_builder_impl_guide.md
│       ├── vlog_impl_guide.md
│       └── lsm_engine_s1_guide.md
└── CMakeLists.txt
```

## 开发阶段

```bash
# 当前版本
git tag -a v0.2.0-s1-engine -m "Stage 1 完成：SSTable/VLog 持久化 + 引擎 flush/read，61 测试全绿"

# 推送到远端
git push origin main --tags

# 开始新阶段
git checkout -b feature/stage2-compaction
```

## 编译选项

```bash
# 默认 Debug
cmake -B build -G "MinGW Makefiles" -DCMAKE_CXX_COMPILER=g++

# 启用 AddressSanitizer + UBSan
cmake -B build -G "MinGW Makefiles" -DCMAKE_CXX_COMPILER=g++ -DLSM_SANITIZE=ON

# 编译警告即错误
cmake -B build -G "MinGW Makefiles" -DCMAKE_CXX_COMPILER=g++ -DLSM_WARNINGS_AS_ERRORS=ON

# 使用系统 gtest 替代自研 mini_test
cmake -B build -G "MinGW Makefiles" -DCMAKE_CXX_COMPILER=g++ -DLSM_USE_SYSTEM_GTEST=ON
```

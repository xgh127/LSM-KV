# LSM-KV Test Usage Guide

## 目录
- [快速开始](#快速开始)
- [运行全部测试](#运行全部测试)
- [按组件运行测试](#按组件运行测试)
- [运行单个用例](#运行单个用例)
- [编译选项](#编译选项)
- [添加新测试](#添加新测试)
- [测试状态一览](#测试状态一览)
- [故障排除](#故障排除)

---

## 快速开始

```bash
# 1. 首次配置（一天一次）
cd LSM-KV
cmake -B build -G "MinGW Makefiles" -DCMAKE_CXX_COMPILER=g++ -DCMAKE_C_COMPILER=gcc

# 2. 编译 + 跑全部测试
cmake --build build
ctest --test-dir build --output-on-failure
```

输出示例：
```
[==========] Running 49 mini_test cases
[ RUN      ] MemTable.PutGetRoundTrip
[       OK ] MemTable.PutGetRoundTrip
...
[==========] 38 passed, 11 failed
```

---

## 运行全部测试

```bash
ctest --test-dir build --output-on-failure
```

等价于直接运行测试二进制：
```bash
.\build\tests\mini_lsm_tests.exe
```

---

## 按组件运行测试

使用 `-L` （label 过滤）:

```bash
ctest --test-dir build --output-on-failure -L memtable
ctest --test-dir build --output-on-failure -L sstable
ctest --test-dir build --output-on-failure -L vlog
ctest --test-dir build --output-on-failure -L iterator
ctest --test-dir build --output-on-failure -L compaction
ctest --test-dir build --output-on-failure -L engine
```

> 注意：因为目前是一个二进制（`mini_lsm_tests.exe`）注册了所有用例，`-L` 只能按标签筛选**整个二进制**，不能精确到单个组件。如果你要只看某个组件的输出，建议用 grep：
>
> ```bash
> .\build\tests\mini_lsm_tests.exe 2>&1 | Select-String -Pattern "MemTable" -Context 0,1
> .\build\tests\mini_lsm_tests.exe 2>&1 | Select-String -Pattern "(RUN|OK|FAILED).*LsmEngine"
> ```

---

## 运行单个用例

mini_test 框架**没有内置的单个用例筛选**。你可以用 `Select-String` 过滤输出：

```bash
# 只看 MemTable 的 PutGetRoundTrip
.\build\tests\mini_lsm_tests.exe 2>&1 | Select-String -Pattern "PutGetRoundTrip" -Context 0,2

# 只看失败的用例
.\build\tests\mini_lsm_tests.exe 2>&1 | Select-String -Pattern "FAILED"

# 只看通过的用例
.\build\tests\mini_lsm_tests.exe 2>&1 | Select-String -Pattern "OK"
```

---

## 编译选项

### Debug 模式（默认）

```bash
cmake -B build -G "MinGW Makefiles" -DCMAKE_CXX_COMPILER=g++
```
编译时带 `-g`，无优化，适合断点调试。

### Sanitizer（AddressSanitizer + UndefinedBehaviorSanitizer）

```bash
cmake -B build -G "MinGW Makefiles" -DCMAKE_CXX_COMPILER=g++ -DLSM_SANITIZE=ON
cmake --build build
ctest --test-dir build --output-on-failure
```
ASan 捕获内存错误（use-after-free、OOB 等），UBSan 捕获未定义行为。

> ⚠ MinGW 下 ASan 可能不完整。如果用 MSVC，`LSM_SANITIZE` 目前被忽略。

### 编译警告即错误

```bash
cmake -B build -G "MinGW Makefiles" -DCMAKE_CXX_COMPILER=g++ -DLSM_WARNINGS_AS_ERRORS=ON
```

### 切换到 gtest（未来 Stage）

```bash
cmake -B build -G "MinGW Makefiles" -DCMAKE_CXX_COMPILER=g++ -DLSM_USE_SYSTEM_GTEST=ON
```

### 清理重建

```bash
Remove-Item -Recurse -Force build
cmake -B build -G "MinGW Makefiles" -DCMAKE_CXX_COMPILER=g++ -DCMAKE_C_COMPILER=gcc
cmake --build build
ctest --test-dir build --output-on-failure
```

---

## 添加新测试

### Step 1: 创建测试文件

在 `tests/` 下新建 `test_<组件名>.cpp`：

```cpp
#include "mini_test.hpp"
#include "你的头文件.h"

using namespace mini_lsm;

TEST(MyComponent, MyFirstTest) {
    EXPECT_EQ(1 + 1, 2);
}

TEST(MyComponent, MySecondTest) {
    MyClass obj;
    EXPECT_TRUE(obj.do_something().ok());
    ASSERT_EQ(obj.count(), 42);
    // ASSERT 失败会终止当前用例
}
```

### Step 2: 注册到 CMake

编辑 `tests/CMakeLists.txt`，将文件名加到 `LSM_TEST_FILES` 列表。

### Step 3: 运行验证

```bash
cmake --build build
.\build\tests\mini_lsm_tests.exe 2>&1 | Select-String -Pattern "MyComponent"
```

---

## 测试状态一览

### 当前（S0 骨架 + MemTable put/get 已实现）
**38 passed, 11 failed**（共 49 个用例）

| 文件 | 用例名 | 状态 | 说明 |
|---|---|---|---|
| **test_memtable.cpp** (10) |
| | `PutGetRoundTrip` | ✅ | put→get 往返 |
| | `OverwriteLatestValueWins` | ✅ | 覆盖写读回最新值 |
| | `DeleteReturnsNullopt` | ✅ | 删后 get 为 nullopt |
| | `ExplicitEmptyValueIsTombstone` | ✅ | 空 value 当做 tombstone |
| | `GetMissingKey` | ✅ | 不存在的 key 返回 nullopt |
| | `EmptyValueAndMissingKeyAreBothNulloptButDistinctInternally` | ✅ | 空 value 与缺失的区分 |
| | `ApproximateSizeIncreases` | ✅ | 插入后 size 增加 |
| | `FreezeReplacesActive` | ✅ | freeze 状态切换 |
| | `ScanReturnsSortedIter` | ❌ | 等待 scan 实现 |
| | `ScanRespectsLowerBound` | ✅ | 下界过滤 |
| | `ScanRespectsUpperBound` | ❌ | 等待 scan 实现 |
| | `ScanFiltersTombstones` | ❌ | 等待 scan 实现 |
| | `FlushToBuilderAcceptsSortedEntries` | ✅ | flush_to 接口完整性 |
| **test_sstable.cpp** (5) | | ✅ 全部通过 |
| **test_sstable_builder.cpp** (4) | | ⚠️ `AscendingInputIsAcceptedOnceImplemented` 红灯（S0 探针） |
| **test_vlog.cpp** (6) | | ⚠️ `OpenWithoutCreateFailsOnMissingFile` 红灯（测试本身待修正） |
| **test_iterator.cpp** (3) | | ✅ 全部通过 |
| **test_compaction.cpp** (4) | | ✅ 全部通过 |
| **test_lsm_engine.cpp** (12) | | ⚠️ 6 红（等待 engine/scan 实现） |

### 目标（S1 全部实现后）
**49 passed, 0 failed**

---

## 故障排除

### `STATUS_ENTRYPOINT_NOT_FOUND (0xC0000139)`
运行时弹窗或 crash，原因是 MinGW DLL 找不到。解决方案（已在 CMakeLists.txt 中处理）：
```cmake
target_link_options(target PRIVATE -static-libgcc -static-libstdc++ -static)
```
如仍有问题，确认你用 `-G "MinGW Makefiles"` 而不是 MSVC。

### `No tests were found!!!`
```bash
ctest --test-dir build --output-on-failure -R MemTable
# 结果: No tests were found!!!
```
原因：当前是一个二进制（`mini_lsm_tests.exe`）包含所有用例，CTest 只注册了一个 test。改用：
```bash
.\build\tests\mini_lsm_tests.exe 2>&1 | Select-String -Pattern "MemTable"
```

### `Error: MSVC was selected but not found`
使用 cmake 时忘了 `-G`：
```bash
# 正确
cmake -B build -G "MinGW Makefiles" -DCMAKE_CXX_COMPILER=g++
# 错误（默认选 MSVC）
cmake -B build
```
清理后重配：
```bash
Remove-Item -Recurse -Force build
cmake -B build -G "MinGW Makefiles" -DCMAKE_CXX_COMPILER=g++ -DCMAKE_C_COMPILER=gcc
```

### 编译通过但测试全红
检查是否忘了 `cmake --build build`。每次修改 `.cpp` 后都要重新编译。

### 用例 crash（segfault）而不是正常失败
如果是 S0 阶段不应该 crash。如遇到，检查：
1. 迭代器返回的 `key()` / `value()` 是否访问了已销毁对象
2. `shared_from_this()` 只能在 `shared_ptr` 管理的对象上调用

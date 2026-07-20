# LSM-KV · 实现指南（S0 RED → S1 GREEN）

> 这份指南告诉你：
> 1. **现在框架处于什么状态**（红灯 / 绿灯一览）
> 2. **该从哪里下手**（按什么顺序填空最不容易踩坑）
> 3. **具体每个文件怎么写**（每个 `TODO(S1)` 的伪代码 + 注意点）
> 4. **怎么验证每一步**（编辑 → 编译 → 单测红绿循环 + sanitizer 检查）
>
> 适用对象：本项目 S0 骨架刚搭完，准备跨入 S1 阶段的实现者（你本人 / AI agent 协同）。

---

## 0. 阅读须知与心智模型

### 0.1 这份骨架最重要的三件事

| # | 原则 | 实践 |
|---|---|---|
| 1 | **测试驱动，红灯先行** | 写实现之前先打开对应 `tests/test_<component>.cpp`，把契约读懂 |
| 2 | **每个 `TODO(S1)` 注释含 Implementation Suggestion** | 你不必思考"用什么数据结构"——按注释里的伪代码手抄即可，先让它跑对再说 |
| 3 | **零外部依赖、严格不污染工作目录** | 所有文件 I/O 用 `std::filesystem::temp_directory_path()`；所有测试用自带 `mini_test.hpp` |

### 0.2 S0 当前状态盘点

构建后跑 `ctest --test-dir build --output-on-failure` 会打印：

```
[==========] 34 passed, 15 failed
```

**15 个失败就是你的"功课表"** —— 全部是 TDD 红灯，等实现填空变绿。**绝不应当出现 SegFault**；如果失败信息里有 `uncaught std::exception`，那是 `ASSERT_*` 在 `mini_test` 里通过抛异常实现，属正常行为。

### 0.3 该用什么数据结构（一句话回答）

| 组件 | 推荐 | 理由 |
|---|---|---|
| MemTable | `std::map<std::string, std::string, std::less<>>` | 红黑树，C++ 直接可用；`less<>` 是 transparent 比较器，能让 `find(string_view)` 不分配 |
| SSTable 文件 I/O | `std::ofstream` / `std::ifstream` 二进制 | S0 仅教学用；S4 才换 io_uring / IOCP |
| Block 编码 | 手写二进制 buffer（`std::string` 当 byte 容器） | 不引入 protobuf |
| Bloom | 位图 `std::vector<uint8_t>` | ~80 行即可 |
| 锁 | `std::shared_mutex` + `shared_ptr<LsmStorageState>` CoW | 跟 mini-lsm 一致；不要直接锁整棵树 |

### 0.4 心智模型一句话

> **"写进去的数据按时间从新到旧找；找到的第一条就是答案；空 value 表示这条数据已经被删了。"**

把这句话刻进脑子，几乎所有 LSM 行为都能推出来。

---

## 1. 推荐实现路径（最重要！务必按顺序）

```
  S1.1  MemTable            （独立组件，最简单的红灯）
   ↓
  S1.2  SSTableBuilder      （依赖一点点编码）
   ↓
  S1.3  SSTable + SSTableIterator   （依赖 Builder 的输出）
   ↓
  S1.4  VLog                （独立的字节追加日志）
   ↓
  S1.5  LsmEngine           （把上面 4 个组装起来）
   ↓
  S1.6  Compaction          （S2 才认真做，S1 留 NoOp）
```

**为什么是这个顺序**：
- MemTable 独立无依赖，红利回报快
- SSTable 三件套（Builder + Table + Iterator）形成闭环，写完 Builder 后 Table / Iterator 顺势做完
- VLog 与 LSM-Tree 解耦（WiscKey 的精髓），可以并行做
- LsmEngine 是粘合层，依赖前面四个组件的 API 稳定
- Compaction 在 S1 可以保持 NoOp，覆盖性测试在 S2

**每完成一个就跑一次 ctest**。下面逐个组件展开。

---

## 2. S1.1 · MemTable（建议 30-60min）

### 2.1 文件位置
- 头文件：`include/memtable.h`（已定好接口，**不要改**）
- 实现：`src/memtable.cpp`（4 个 `TODO(S1)`）
- 测试：`tests/test_memtable.cpp`（9 条用例）

### 2.2 测试契约速读

| 用例 | 验证 |
|---|---|
| `PutGetRoundTrip` | `put("k1","v1")` 后 `get("k1") == "v1"` |
| `OverwriteLatestValueWins` | 同 key 多次 put 取最后值 |
| `DeleteReturnsNullopt` | `del(k)` 后 get 返回 `nullopt` |
| `ExplicitEmptyValueIsTombstone` | `put(k, "")` 等同于 `del(k)` |
| `EmptyValueAndMissingKeyAreBothNulloptButDistinctInternally` | tombstone 与"从未存在"外表一样，但**重新写入能复活** |
| `ApproximateSizeIncreases` | `approximate_size()` 在 put 后增加 |
| `FreezeReplacesActive` | `freeze()` 后 `is_frozen()` 为 true |
| `ScanReturnsSortedIter` | scan 出来的 key 升序 |
| `ScanRespectsLowerBound` / `ScanRespectsUpperBound` / `ScanFiltersTombstones` | scan 边界、tombstone 过滤 |

### 2.3 实现要点（按 src/memtable.cpp 顺序）

#### `put(key, value)`（src/memtable.cpp:38）
```cpp
Status MemTable::put(KeyView key, ValueView value) {
    if (frozen_) return Status::InvalidArgument("memtable frozen");
    map_.emplace(std::string(key), std::string(value));
    // 注意：emplace 不会覆盖；要覆盖要用 operator[] 或先 erase
    // 用 operator[] 最简洁：
    // map_[std::string(key)] = std::string(value);
    size_.fetch_add(key.size() + value.size());
    return Status::OK();
}
```
**坑**：`std::map::emplace` 在 key 已存在时**不会覆盖**旧值。改写要：
```cpp
map_[std::string(key)] = std::string(value);   // ← 推荐
```

#### `get(key)`（src/memtable.cpp:46）
```cpp
std::optional<Value> MemTable::get(KeyView key) const {
    auto it = map_.find(key);   // std::less<> transparent 比较器允许 string_view 查找
    if (it == map_.end()) return std::nullopt;
    if (it->second.empty()) return std::nullopt;   // tombstone
    return it->second;
}
```
注意 `std::map<Key, Value, std::less<>>` 的 `find(string_view)` 是 O(log n) 且零分配 ——这是 `std::less<>` transparent 特性保证的（C++14 起就支持）。

#### `scan(lo, hi)`（src/memtable.cpp:52）
返回一个 `MemTableIterator`，要持有 `shared_ptr<const MemTable>` 让迭代器生命周期独立于 MemTable 自身。最简实现：

```cpp
std::unique_ptr<StorageIterator> MemTable::scan(KeyView lo, KeyView hi) const {
    return std::make_unique<MemTableIterator>(
        std::shared_ptr<const MemTable>(this, [](auto*){}),   // 不拥有，仅保活引用
        lo, hi);
}
```
然后改 `MemTableIterator`：
```cpp
class MemTableIterator final : public StorageIterator {
    std::shared_ptr<const MemTable>  mt_;
    KeyView                          hi_;
    decltype(map_)::const_iterator   cur_, end_;
public:
    MemTableIterator(std::shared_ptr<const MemTable> mt, KeyView lo, KeyView hi)
        : mt_(std::move(mt)), hi_(hi) {
        cur_ = mt_->map_.lower_bound(lo);
        end_ = mt_->map_.end();
    }
    bool       is_valid() const override { return cur_ != end_ && cur_->first < hi_; }
    KeyView    key()   const override { return cur_->first; }
    ValueView  value() const override { return cur_->second; }
    Status     next()  override { ++cur_; return Status::OK(); }
};
```
但要注意：`MemTableIterator` 必须能访问 `MemTable::map_` 私有成员。**最干净做法**：把 `MemTableIterator` 设为 `friend`，或者把 `MemTable::map_` 改为 `protected`。最简实战：在 `memtable.h` 里 `friend class MemTableIterator;` 一行。

**坑**：scan 应过滤 tombstone。两种实现策略：
1. **next() 内跳过 tombstone**（更紧凑）：每次 next 跳过 value 为空的项
2. **LsmEngine 层过滤**（更纯粹）：memtable 迭代器原样输出，理论上 LsmEngine 上游过滤

我们的 S0 测试 `ScanFiltersTombstones` 在 MemTable 层就要求过滤，所以选策略 1。`next()` 改为：
```cpp
Status next() override {
    ++cur_;
    while (cur_ != end_ && cur_->second.empty()) ++cur_;   // skip tombstone
    return Status::OK();
}
```
**注意 seat_to_first / seek_to_key 也要跳过开头连续的 tombstone**。本项目 S0 只在构造时定位，所以构造函数也要加 while-skip：
```cpp
MemTableIterator(...) {
    cur_ = mt_->map_.lower_bound(lo);
    while (cur_ != end_ && cur_->second.empty()) ++cur_;
}
```

#### `flush_to(builder)`（src/memtable.cpp:60）
```cpp
Status MemTable::flush_to(SSTableBuilder& builder) const {
    for (auto& [k, v] : map_) {
        if (!builder.add(k, v)) {
            return Status::IOError("SSTable builder rejected entry");
        }
    }
    return Status::OK();
}
```

### 2.4 验证

```bash
cmake --build build
ctest --test-dir build --output-on-failure -R MemTable
```
预期：
```
[       OK ] MemTable.PutGetRoundTrip
[       OK ] MemTable.OverwriteLatestValueWins
[       OK ] MemTable.DeleteReturnsNullopt
...
[==========] Running 9 ... 9 passed, 0 failed  ← 全绿
```

### 2.5 sanitizer 二次确认

```bash
cmake -B build -DLSM_SANITIZE=ON -G "MinGW Makefiles"
cmake --build build
ctest --test-dir build --output-on-failure -R MemTable
```
出现任何 `heap-buffer-overflow` / `use-after-free` → 重新检查 `shared_ptr` 保活逻辑、tombstone 跳过边界、迭代器 +=1 是否越界。

---

## 3. S1.2 · SSTableBuilder（建议 1-2h）

### 3.1 必读：sstable.h 头注释里的格式定义

**重点**：S0 测试**不检查格式兼容性**，只要求 `add` / `finish` 不崩。所以你可以先用最简编码（一个块包装所有 entry），格式优化留到后期。推荐编码：

```
Block 内 layout:
  ┌──────────┬─────────┬───────────┬─────────┬───┬──────┐
  │ n_entry  │  K1_len │  K1       │ V1_len  │V1 │ ...  │
  │  u32     │  u16    │  K1 bytes │  u16    │.. │ ...  │
  └──────────┴─────────┴───────────┴─────────┴───┴──────┘
```
全部 little-endian（x86 / ARM 桌面机原生序）。

### 3.2 测试契约

`tests/test_sstable_builder.cpp` 4 条：
- `EmptyBuilderIsEmpty`：新建 `num_entries()==0` `is_empty()==true`
- `AddReturnsFalseInS0`：这条是 S0 探针，实现后变 **红 → 绿**：你实现后 `add` 应返回 `true`，这条测试反而**会失败**。**这是设计预期**——把这条测试改成 `EXPECT_TRUE(b.add("k1","v1"))` 即可，或把它删除。
- `FinishReturnsNotSupportedInS0`：同理，你实现后这条要改 / 删
- `AscendingInputIsAcceptedOnceImplemented`：实现后变绿
- `OutOfOrderKeyRejected`：必须始终 out-of-order 拒绝

**重要**：实现时要去把两条"S0 探针"测试改成"正向的 S1 契约"。
```cpp
// 改前：
TEST(SSTableBuilder, AddReturnsFalseInS0) {
    SSTableBuilder b(4096, true);
    EXPECT_FALSE(b.add("k1", "v1"));    // ← 这条 RED 改成 ↓
}

// 改后：
TEST(SSTableBuilder, AddAcceptsAscending) {
    SSTableBuilder b(4096, true);
    EXPECT_TRUE(b.add("k1", "v1"));
}
```
**TodoWrite 强约束**：每改一条测试都要在 commit message 里写 `test: flip s0 probe to s1 contract` 一类说明。

### 3.3 实现要点

#### `add(key, value)`（src/sstable_builder.cpp:14）
```cpp
bool SSTableBuilder::add(KeyView key, ValueView value) {
    if (!data_.empty() && key <= KeyView(last_key_)) return false;  // 升序校验
    if (first_key_.empty()) first_key_ = std::string(key);
    last_key_ = std::string(key);

    // 简化编码：先写一条 entry header，再写 key/value。} 多 block 拆分留到 S4
    char hdr[4];
    auto klen = static_cast<std::uint16_t>(key.size());
    auto vlen = static_castuint16_t>(value.size());
    std::memcpy(hdr, &klen, 2);
    std::memcpy(hdr + 2, &vlen, 2);
    data_.append(hdr, 4);
    data_.append(key);
    data_.append(value);
    num_entries_++;
    return true;
}
```

> 注意源码作为文档显示给读者，避免中文乱入代码体破坏视觉一致性。

#### `finish(id, path, out)`（src/sstable_builder.cpp:22）
```cpp
Status SSTableBuilder::finish(std::uint64_t id, std::filesystem::path path,
                             std::unique_ptr<SSTable>& out) {
    std::ofstream f(path, std::ios::out | std::ios::binary | std::ios::trunc);
    if (!f.is_open()) return Status::IOError("cannot open sst for write");

    // 写一个 Block（S0 简化版；多块拆分到 S4）
    std::uint32_t block_offset = 0;
    std::uint32_t n_entries = static_cast<uint32_t>(num_entries_);
    f.write(reinterpret_cast<const char*>(&n_entries), 4);
    f.write(data_.data(), static_cast<std::streamsize>(data_.size()));
    f.flush();

    BlockMeta meta;
    meta.offset      = 0;
    meta.num_entries = num_entries_;
    meta.first_key   = first_key_;
    meta.last_key    = last_key_;
    metas_.push_back(meta);

    // 写 meta blob（count + 数组）
    std::uint32_t meta_count = 1;
    f.write(reinterpret_cast<const char*>(&meta_count), 4);
    for (auto& m : metas_) {
        f.write(reinterpret_cast<const char*>(&m.offset), 4);
        f.write(reinterpret_cast<const char*>(&m.num_entries), 4);
        std::uint16_t klen = static_cast<uint16_t>(m.first_key.size());
        f.write(reinterpret_cast<const char*>(&klen), 2);
        f.write(m.first_key.data(), klen);
        klen = static_cast<uint16_t>(m.last_key.size());
        f.write(reinterpret_cast<const char*>(&klen), 2);
        f.write(m.last_key.data(), klen);
    }
    std::uint64_t meta_off = block_offset + data_.size() + 4; // approximate
    f.write(reinterpret_cast<const char*>(&meta_off), 8);
    std::uint64_t bloom_off = 0; // S0: no bloom file body yet
    f.write(reinterpret_cast<const char*>(&bloom_off), 8);
    std::uint64_t magic = SSTable::kMagic;
    f.write(reinterpret_cast<const char*>(&magic), 8);

    f.close();
    return SSTable::open(id, path, out);
}
```

### 3.4 验证

```bash
cmake --build build
ctest --test-dir build --output-on-failure -R SSTableBuilder
```

---

## 4. S1.3 · SSTable + SSTableIterator（建议 1-2h）

### 4.1 测试契约

`tests/test_sstable.cpp` 5 条：
- `MayContainAlwaysTrueByDefault`：你这辈子可以让它一直返回 true，到 S4 bloom 真做时才认真
- `FindBlockIdxOnEmptyReturnsZero`：空 metas 时返回 0
- `OpenOnMissingFileReturnsNotSupported`：S0 用 `NotSupported` 占位是实现后**改成 `Corruption`** 才说得过去
- `SSTableIterator.IteratorStartsInvalid`：未 `seek_to_first` 时 `is_valid() == false`
- `SSTableIterator.SeekByKeyKeepsInvalid`：seek 之后必须有有效 entry

**同样需翻面**的探针测试：
- `OpenOnMissingFileReturnsNotSupported`：你实现 open() 后，文件确实存在时应该 `Status::OK()`；文件不存在时应当 `Status::IOError`。请改测试名为 `OpenOnMissingFileReturnsIOError` 或调整断言。

### 4.2 实现要点

#### `SSTable::open()`（src/sstable.cpp:39）
读 footer（最后 24B：meta_off u64 / bloom_off u64 / magic u64）→ 校验 magic → seek 到 meta_off → 读 meta blob → 反序列化 → 处理可选 bloom。

#### `SSTable::read_block(idx)`（src/sstable.cpp:33）
```cpp
Status SSTable::read_block(std::size_t idx, std::string& out) const {
    if (idx >= block_metas_.size()) return Status::InvalidArgument("bad idx");
    std::ifstream f(path_, std::ios::in | std::ios::binary);
    if (!f.is_open()) return Status::IOError("cannot open sst");
    f.seekg(block_metas_[idx].offset);
    // block 大小：到下一个 block 起点，或到 meta offset。S0 单块简化为 data_.size() + 4。
    // 实战：编码时把每个 block 大小也写下来 —— 这里先用最简单版本（固定 block_size_bytes_）
    out.resize(/* 块字节数 */);
    f.read(out.data(), out.size());
    return Status::OK();
}
```

#### `SSTableIterator::seek_to_first`（src/sstable.cpp:58）
```cpp
void SSTableIterator::seek_to_first() {
    if (table_->num_blocks() == 0) { valid_ = false; return; }
    current_block_idx_ = 0;
    auto s = table_->read_block(0, current_block_bytes_);
    if (!s.ok()) { valid_ = false; return; }
    entry_cursor_ = 0;
    parse_current_entry();
}
```

### 4.3 验证

```bash
ctest --test-dir build --output-on-failure -R SSTable
```

---

## 5. S1.4 · VLog（建议 1h）

### 5.1 测试契约（`tests/test_vlog.cpp`）

| 用例 | 验证 |
|---|---|
| `OpenCreatesFile` | 创建父目录 + 文件 |
| `OpenWithoutCreateFailsOnMissingFile` | 文件不存在且 `create_if_missing=false` 时返回非 OK |
| `AppendReturnsOkEvenInS0` | put 一次返回 OK |
| `ReadAtNotSupportedInS0` | S0 read_at 未实现，返回非 OK —— 你实现后**要翻面**改成 `Expect_TRUE(s.ok())` |
| `GcIsCallableAndNoOp` | GC 是 no-op，返回 OK，reclaimed==0 |
| `SizeBytesReportsZeroOnFreshOpen` | 新建 vlog 长度 0 |

### 5.2 实现要点

#### `append(key, value, out_handle)`（src/vlog.cpp:43）
WiscKey record layout：
```
magic (1B) | key_len (2B) | key | value_len (4B) | value | crc32 (4B)
```
```cpp
Status VLog::append(KeyView key, ValueView value, VLogHandle& out) {
    out.offset = next_offset_;
    std::string buf;
    buf.push_back(static_cast<char>(0xA1));                 // magic
    char h[6];
    std::uint16_t klen = static_cast<uint16_t>(key.size());
    std::uint32_t vlen = static_cast<uint32_t>(value.size());
    std::memcpy(h, &klen, 2);
    std::memcpy(h + 2, &vlen, 4);
    buf.append(h, 6);
    buf.append(key);
    buf.append(value);
    std::uint32_t crc = crc32(buf.data(), buf.size());   // 自带 / zlib
    buf.append(reinterpret_cast<const char*>(&crc), 4);

    out_.write(buf.data(), buf.size());
    out_.flush();
    next_offset_ += buf.size();
    out.length = static_cast<uint32_t>(buf.size());
    return Status::OK();
}
```

#### `read_at(handle, out)`（src/vlog.cpp:50）
打开 ifstream 并чик seek_offset_read_length_ — S0 头部 6B 跳读到 entry 起点 → 核对 crc → 返回 value 部分。

### 5.3 验证

```bash
ctest --test-dir build --output-on-failure -R VLog
```

**特别检查**：append 后 `size_bytes() == buf.size()`（10 key × 平均 30 字节 = 300，对得上）。

---

## 6. S1.5 · LsmEngine（建议 2-3h）

### 6.1 测试契约（`tests/test_lsm_engine.cpp`）

| 用例 | 红灯说明 |
|---|---|
| `PutGetRoundTrip` | put 后 get 返回 nullopt —— 因为 `LsmEngine::get` 没查 memtable |
| `OverwriteValueWins` | 同上 |
| `HundredPutsAllGettable` | 100 次 put 都 get 不回来 |
| `ScanReturnsSortedInRange` | scan 出来是空 list |
| `ScanOmitsTombstones` | del 后 scan 不过滤 |
| `ForceFreezePushesToImmutables` | force_freeze_memtable 是 NoOp，immutable_memtables 为空 |

### 6.2 实现要点

#### `put(key, value)`（src/lsm_engine.cpp:48）
```cpp
Status LsmEngine::put(KeyView key, ValueView value) {
    auto snap = snapshot();
    snap->active_memtable->put(key, value);   // ← 最关键的一行

    if (snap->active_memtable->approximate_size() >= options_.memtable_target_size) {
        return force_freeze_memtable();
    }
    return Status::OK();
}
```
**注意**：`snap` 是 `shared_ptr<const LsmStorageState>`，但 `MemTable` 自身是 `shared_ptr<MemTable>`，可写。这里 `const` 只是保护 state 结构，不延伸到 owned objects。这与 mini-lsm 一致。

但是 `LsmStorageState` 持有 `std::shared_ptr<MemTable> active_memtable`，从 `const LsmStorageState*` 上拿到的仍是普通 `MemTable&`，调用 `put` 没 OK。

**若你严格做 const correctness**：把 `LsmStorageState::active_memtable` 类型保持 `std::shared_ptr<MemTable>`，读者的 `snapshot()` 返回 `shared_ptr<const LsmStorageState>` 只是 deny 别人改 state 的结构（如替换 active_memtable），但 active_memtable 指向的对象本身仍可写。这是**故意的设计**，对应 mini-lsm `Arc<RwLock<Arc<State>>>` 模式里第一层 lock 内的 mutable state。

#### `get(key)`（src/lsm_engine.cpp:54）
```cpp
std::optional<Value> LsmEngine::get(KeyView key) {
    auto snap = snapshot();
    // 1) active memtable
    if (auto v = snap->active_memtable->get(key)) return v;
    // 2) immutables (从新到旧)
    for (auto& imm : snap->immutable_memtables) {
        if (auto v = imm->get(key)) return v;
    }
    // 3) L0 sstables (从新到旧)
    for (auto sst_id : snap->l0_sstables) {
        auto it = snap->sstables.find(sst_id);
        if (it == snap->sstables.end()) continue;
        if (!it->second->may_contain(key)) continue;
        auto iter = std::make_shared<SSTableIterator>(it->second);
        iter->seek_to_key(key);
        while (iter->is_valid()) {
            if (iter->key() == key) {
                auto v = iter->value();
                return v.empty() ? std::nullopt : std::optional<Value>{std::string(v)};
            }
            if (iter->key() > key) break;
            iter->next();
        }
    }
    // 4) levels L1..LK —— S0 暂不实现 compaction 后的 levels，留空即可
    return std::nullopt;
}
```

#### `force_freeze_memtable()`（src/lsm_engine.cpp:69）
```cpp
Status LsmEngine::force_freeze_memtable() {
    std::lock_guard<std::mutex> g(state_mutex_);
    auto fresh = std::make_shared<LsmStorageState>(*state_);   // CoW 拷贝
    fresh->active_memtable->freeze();
    fresh->immutable_memtables.insert(fresh->immutable_memtables.begin(),
                                        fresh->active_memtable);
    fresh->active_memtable = std::make_shared<MemTable>(next_sst_id_.fetch_add(1));
    state_ = std::move(fresh);
    return Status::OK();
}
```

#### `force_flush_next_imm_memtable()`（src/lsm_engine.cpp:76）
```cpp
Status LsmEngine::force_flush_next_imm_memtable(std::uint64_t& new_sst_id) {
    auto snap = snapshot();
    if (snap->immutable_memtables.empty()) {
        new_sst_id = 0;
        return Status::OK();
    }
    // immutable_memtables.back() 是最老的（先入先出）
    auto& imm = snap->immutable_memtables.back();

    SSTableBuilder builder(options_.block_size_bytes, options_.bloom_filter_enabled);
    auto s = imm->flush_to(builder);
    if (!s.ok()) { new_sst_id = 0; return s; }

    auto sst_path = options_.base_dir / (std::to_string(imm->id()) + ".sst");
    std::unique_ptr<SSTable> sst;
    s = builder.finish(/*id=*/imm->id(), sst_path, sst);
    if (!s.ok()) { new_sst_id = 0; return s; }
    new_sst_id = imm->id();

    std::lock_guard<std::mutex> g(state_mutex_);
    auto fresh = std::make_shared<LsmStorageState>(*state_);
    fresh->immutable_memtables.pop_back();
    fresh->l0_sstables.insert(fresh->l0_sstables.begin(), new_sst_id);
    fresh->sstables.emplace(new_sst_id, std::shared_ptr<SSTable>(std::move(sst)));
    state_ = std::move(fresh);
    return Status::OK();
}
```

#### `scan(lo, hi, out)`（src/lsm_engine.cpp:60）
S1 实现：合并所有 memtables + L0 sstables 的迭代器，跑一遍去重 + tombstone filter。可以用最简"memtable 优先" 算法：

```cpp
Status LsmEngine::scan(KeyView lo, KeyView hi,
                       std::vector<std::pair<Key, Value>>& out) {
    out.clear();
    auto snap = snapshot();

    // 用一个 std::map 在内存里合并；S0 简化版（S1+ 用真正的 MergeIterator）
    std::map<Key, Value, std::less<>> merged;
    auto add_kv = [&](KeyView k, ValueView v) {
        merged[std::string(k)] = std::string(v);  // 覆盖即"新版本胜出"
    };
    // 顺序：active memtable -> imms 新 -> 旧 -> L0 sstables 新 -> 旧
    // 每个 MemTable.scan 自带 tombstone filter，但 LsmEngine 这里仍然要处理
    // 出现空 value（即 tombstone）的项 —— 直接覆盖即可
    auto absorb = [&](MemTable& mt) {
        auto it = mt.scan(lo, hi);
        while (it->is_valid()) {
            add_kv(it->key(), it->value());
            it->next();
        }
    };
    absorb(*snap->active_memtable);
    for (auto& imm : snap->immutable_memtables) absorb(*imm);

    // L0 sstables
    for (auto sst_id : snap->l0_sstables) {
        auto it = snap->sstables.find(sst_id);
        if (it == snap->sstables.end()) continue;
        auto iter = std::make_shared<SSTableIterator>(it->second);
        iter->seek_to_key(lo);
        while (iter->is_valid()) {
            if (iter->key() >= hi) break;
            add_kv(iter->key(), iter->value());
            iter->next();
        }
    }

    // 输出 + 过滤 tombstone
    for (auto& [k, v] : merged) {
        if (!v.empty()) out.emplace_back(k, v);
    }
    return Status::OK();
}
```
这是 S1 的"够用版"。S2 会用 TwoMergeIterator + 正 经 MergeIterator 替换。

### 6.3 验证

```bash
cmake --build build
ctest --test-dir build --output-on-failure -R LsmEngine
```
目标：10 条全绿。**重点确认**：`HundredPutsAllGettable` 跑完后 engine 里没有全留 memtable —— 你可以跟踪 `approximate_size` 看。

---

## 7. S1.6 · Compaction（保持 NoOp 即可）

S0 测试已经全绿（NoCompactionController/Executor 返回 `nullopt` / `NotSupported`）。S2 再实现 Simple/Tiered/Leveled。

---

## 8. 上下游测试循环（这是最最重要的）

### 8.1 红绿循环

每完成一个组件，按下面四步走：

```
1. 编辑：用 IDE / Edit 填空 src/<component>.cpp 里对应 TODO(S1)
2. 编译：cmake --build build
   - 编译错：读 gcc/ MSVC 输出 fix 语法
   - 编译警告：尽量修掉；实在不行临时关掉 -Werror
3. 测试：ctest --test-dir build --output-on-failure -R <Component>
   - 红灯：读 EXPECT 输出的 Expected vs Actual，回 step 1
4. sanitizer：cmake -B build-sani -DLSM_SANITIZE=ON -G "MinGW Makefiles"
   cmake --build build-sani
   ctest --test-dir build-sani --output-on-failure -R <Component>
```

### 8.2 跑特定组件

`-R` 是正则过滤。命名约定：
```
ctest --test-dir build --output-on-failure -R MemTable       # 只跑 MemTable 的测试
ctest --test-dir build --output-on-failure -R "MemTable|VLog" # 跑两个组件
ctest --test-dir build --output-on-failure -L unit           # 按 label 跑
```

### 8.3 跑调试模式

`mini_test` 失败时打印预期 vs 实际，**但不会停止运行其它测试**。所以建议每次失败后**先看最早的失败**（影响最深的那个），不要被后面无关的级联失败干扰。

### 8.4 性能微测（S1 末做）

S1 完成后，建议写一个临时 gbench 风格的脚本测一下 baseline：

```cpp
// 在 build/scratch 性 bench_put_get.cpp
// 不是 S0 强制要求，只是给你自己看的
```

`bench/` 目录用在 S4。S1 暂不需要。

---

## 9. sanitizer + warning 双重验收（PR 前必跑）

### 9.1 ASan/UBSan

```bash
cmake -B build-sani -G "MinGW Makefiles" -DLSM_SANITIZE=ON
cmake --build build-sani
ctest --test-dir build-sani --output-on-failure
```
**0 错误**才算 S1 该组件真正完成。

关键检查点：
- ASan：`heap-buffer-overflow`、`stack-buffer-overflow`、`use-after-free`
- UBSan：`implicit conversion`、`signed-integer-overflow`、`null pointer dereference`

### 9.2 Werror 严格模式

```bash
cmake -B build-strict -G "MinGW Makefiles" -DLSM_WARNINGS_AS_ERRORS=ON
cmake --build build-strict
```
应该零警告零错误。如果遇到 `sign-compare` 一类：
- 强制 cast：`static_cast<std::size_t>(signed_value)`
- 或在特殊处加 `#pragma GCC diagnostic ignored "..."`
- 不要忽略 `-Wno-...` 全局关警告

---

## 10. 测试金字塔图谱（每阶段该达成的状态）

| 阶段 | 期望测试分布 |
|---|---|
| **S1.0 起点**（现状） | 49 注册，34 pass / 15 fail（全 TDD 红灯，不 SegFault） |
| S1.1 MemTable 完成 | MemTable 9 条全绿；其它仍红 |
| S1.2 SSTableBuilder 完成 | SSTableBuilder 4-5 条全绿（翻面后） |
| S1.3 SSTable 完成 | SSTable 5 条全绿（翻面后） |
| S1.4 VLog 完成 | VLog 6 条全绿（翻面后） |
| S1.5 LsmEngine 完成 | LsmEngine 10 条全绿 |
| **S1 终点** | 49 全绿、ASan 干净、`-Werror` 干净 |
| **S2 起步** | 引入 week2_*.rs 翻译的 21 条新测试（manifest/wal/compaction） |

---

## 11. 常见陷阱与反模式（按踩坑率排序）

### 11.1 忘掉 tombstone 要过滤

**症状**：scan 输出空 value 条目；get 返回空串
**修复**：所有 MemTable put 路径、所有 scan 路径、所有合并路径都要遵 value.empty() == tombstone 规则
**预防**：写一个 `is_tombstone(v)` 工具函数，全代码搜 `value.empty()` 改成它

### 11.2 `std::map::emplace` 不覆盖

**症状**：`put("k", "old"); put("k", "new");` 后 get 返回 "old"
**修复**：用 `map_[k] = v`，不要 emplace

### 11.3 iterator 失效

**症状**：拿到 memtable scan 的 iterator 后又 put 了一个 key，访问崩溃
**根因**：std::map iterator 在插入/删除时有部分稳定性，但跨"先拿后改"会 UB
**修复**：S0 的 MemTable freeze 机制就是为了避免这个；调用者应当只在 immutable memtable 上 scan。S1 的测试**已经在 force_freeze_memtable 后 scan**，所以你 freeze 实现对了就没事

### 11.4 shared_ptr 循环引用

**症状**：MemTable 析构不掉、内存泄漏
**根因**：MemTableIterator 持 shared_ptr<MemTable>，MemTable 不要反持 iterator
**修复**：MemTable 跟 iterator 单向引用即可

### 11.5 路径污染当前目录

**症状**：跑完测试在 `LSM-KV/` 根目录下出现 `lsm_data/` 一堆 .sst 文件
**根因**：测试代码忘记用 `temp_directory_path()` 而用了相对 `./lsm_data` 默认值
**修复**：所有测试 LsmOptions 必须改 `base_dir = std::filesystem::temp_directory_path() / "mini_lsm_..."`（test_lsm_engine.cpp 已经写了 helper）

### 11.6 `std::less<>`（C++14 transparent comparator）忘记加

**症状**：`map_.find(string_view)` 编译错
**根因**：`std::map<std::string, ...>` 默认用 `std::less<std::string>`，只允许 string 查询
**修复**：在 include/memtable.h 已经声明了 `std::less<>`，不要删

### 11.7 cross-platform 编译警告

`std::string::size()` 返回 `size_t`，但有时你在比较时写 `int`：
```cpp
if (key.size() < some_int)   // sign-compare warning
```
全部用 `std::size_t` 或 `static_cast<std::size_t>`。`-Wconversion` 会帮抓。

### 11.8 编译错时盲修

**症状**：仔细看错误，发现自己改错文件
**修复**：错误行号是绝对路径，看准 `src/` 还是 `tests/` 还是 `include/`
**预防**：**永远不要改 `include/` 的接口先于看测试**。测试就是契约，接口不能动。

---

## 12. AI agent 协同的两种推荐工作流

### 工作流 A：单 agent 一次实现一个组件（适合新手）

```
你 → opencode: "实现 src/memtable.cpp 让 tests/test_memtable.cpp 全绿。
                严格按 include/memtable.h 接口不修改。
                完成后必须跑 ctest 贴输出。"
agent → 改 src/memtable.cpp → 编译 → 测试 → 调试 → 通过
你 → review diff → 满意就 commit
```

### 工作流 B：agent 编译错就停（适合严控质量）

```
你：实现 MemTable，但任何一步编译错或测试红就立刻停下、向我汇报。
agent：[执行]  [编译错]  [停]
你：[看错误]  [给指引]
agent：[执行]  ...
```

A 模式更适合快速推进；B 模式适合学习 + 把质量钉死。**S1 阶段建议 B 模式**——你会真正理解每个组件。

---

## 13. 完成时间预算

| 阶段 | 估计耗时 | 累计 |
|---|---|---|
| S1.1 MemTable | 30-60min | 1h |
| S1.2 SSTableBuilder | 1-2h | 3h |
| S1.3 SSTable + Iterator | 1-2h | 5h |
| S1.4 VLog | 1h | 6h |
| S1.5 LsmEngine | 2-3h | 9h |
| ASan + Werror 清扫 | 1-2h | 11h |
| **合计** | **9-11 小时** | 一个完整周末 |

---

## 14. S1 完工标志（Checklist）

按下面打勾就 S1 毕业：

- [ ] `cmake --build build` 零警告零错误
- [ ] `ctest --test-dir build --output-on-failure` 输出 `0 failed`
- [ ] `cmake --build build-strict -DLSM_WARNINGS_AS_ERRORS=ON` 零警告
- [ ] `ctest --test-dir build-sani --output-on-failure` 零 ASan/UBSan
- [ ] 没有 .sst / .vlog / lsm_data 文件留在工作目录
- [ ] 所有 `src/*.cpp` 里的 `TODO(S1)` 注释都已删除（实现后删 TODO）
- [ ] 跑一次 `git diff --stat src/` 显示 7 个 .cpp 文件都被改过
- [ ] commit message 写 `feat(s1): implement MemTable / SSTable / VLog / LsmEngine` 之类
- [ ] 打 tag：`git tag -a checkpoint/s1-complete -m "Stage S1 complete"`

打上这个 tag 之后，你就可以正式跨入 S2 - 持久化与压实，把 mini-lsm Week 2 的 21 条测试翻译进来。祝编码顺利。

---

## 附录 A：mini_test 框架快查

```cpp
TEST(Suite, Name) { ... }            // 定义一个测试
EXPECT_EQ(a, b);                     // 失败不停
EXPECT_NE / EXPECT_TRUE / EXPECT_FALSE / EXPECT_LT / GT / LE / GE
ASSERT_EQ(a, b);                     // 失败立刻 return 当前测试
ASSERT_TRUE / ASSERT_FALSE
FAIL();                              // 主动 fail 当前测试
```

`mini_test` 限性：
- 不支持 `TEST_F` 夹具；要 fixture 在测试体内手写 setup/teardown
- 不支持 `<<` 流式信息（gtest 的 `EXPECT_TRUE(x) << "msg"` 在这里编译错）
- 不支持 death test / SUBSET / matcher

要切到 gtest：CMake 加 `-DLSM_USE_SYSTEM_GTEST=ON`，再 `find_package(GTest)` 即可。S1 之前**不建议切**——`mini_test` 已经够用。

## 附录 B：命令速查

```bash
# 配置
cmake -B build -G "MinGW Makefiles" -DCMAKE_CXX_COMPILER=g++ -DCMAKE_C_COMPILER=gcc

# 配置（严格 + sanitize）
cmake -B build-strict -G "MinGW Makefiles" \
  -DCMAKE_CXX_COMPILER=g++ \
  -DLSM_WARNINGS_AS_ERRORS=ON -DLSM_SANITIZE=ON

# 编译
cmake --build build

# 跑特定组件
ctest --test-dir build --output-on-failure -R MemTable

# 跑全部
ctest --test-dir build --output-on-failure

# 清理重配
Remove-Item -Recurse -Force build
cmake -B build ...
```
# LSM-KV 
一个基于LSMTree的键值存储系统
## 记录特定版本的方法
打一个带注释的标签（推荐，包含版本信息和日期）
```bash
git tag -a v0.1.0-s0-skeleton -m "Stage 0 完成：工程骨架 + 基础测试"

git tag -a v0.2.0-s1-memtable -m "Stage 1 完成：MemTable 实现，所有单元测试通过"

git tag -a v0.3.0-s2-compaction -m "Stage 2 完成：Compaction 与 SSTable 持久化"
# 推送到远端
git push origin main --tags
# 想回退只需要
git checkout v0.1.0-s0-skeleton   # 回到骨架阶段
```
## 开发阶段
```bash
# 开始开发 Stage 1
git checkout -b feature/stage1-memtable

# ... 写代码，疯狂提交 ...
# 开发完成，测试全绿后，合并回 main
git checkout main
git merge feature/stage1-memtable --no-ff  # --no-ff 保留分支历史

# 打上 Tag（如上所述）
git tag -a v0.2.0-s1-memtable -m "..."

# 删除已经合并的临时分支（可选）
git branch -d feature/stage1-memtable
```
## 实现memtable
先测试
```bash
cmake --build build
ctest --test-dir build --output-on-failure -R memtable
# or
cmake --build build && .\build\tests\test_memtable.exe
```




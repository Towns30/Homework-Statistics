# 作业批改统计程序

这是一个面向教师的本地中文终端工具，用于连续多周保存作业批改结果。程序按学生录入错题编号，自动计算正确题数和等级，并给出每题正确率、等级分布和作业完成率。所有已确认的数据立即写入 SQLite 事务；退出程序或关闭电脑后可重新打开。

程序是单用户离线工具，不包含网络服务、账号、学生姓名或图形界面。数据库里保存的是作业配置、稳定的录入顺序和错题集合；正确题数、等级及统计在读取时计算，避免派生数据互相矛盾。

## 功能

- 新建、列出、重新打开和删除多份作业。
- 在新建、选择作业、新增记录或修改记录时输入 `-1`，可取消当前操作并返回上一界面。
- 所有确认操作统一输入 `y` 或 `n`（大小写均可），无需切换中文输入法。
- 用空格、英文逗号或两者混合输入错题；回车或“无”表示全对。
- 对输入范围、重复题号、非整数、小数和负数给出中文错误并重新询问。
- 达到总学生数后阻止继续新增。
- 每次新增或修改成功后立即输出该学生的正确题数和等第。
- 按“倒数第几个”定位并替换旧记录，不改变原始录入序号。
- 显示逐题正确率、各等级人数/百分比和总体完成率；零份记录时安全显示“暂无数据”。
- SQLite 外键、5 秒 busy timeout、WAL 模式、版本化迁移和事务提交。

## 依赖

- 支持 C++20 的编译器：GCC 10+、Clang 12+ 或 Visual Studio 2022。
- CMake 3.20 或更新版本。
- SQLite3 运行库和开发头文件。
- 可选：Ninja 和 `clang-format`（格式检查需要）。

### Linux

Ubuntu/Debian：

```sh
sudo apt-get update
sudo apt-get install build-essential cmake libsqlite3-dev clang-format ninja-build
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

Fedora 可安装 `gcc-c++ cmake sqlite-devel clang-tools-extra ninja-build`；Arch Linux 可安装 `base-devel cmake sqlite clang ninja`。

### macOS

先安装 Xcode Command Line Tools，再用 Homebrew 安装依赖：

```sh
xcode-select --install
brew install cmake sqlite clang-format ninja
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DCMAKE_PREFIX_PATH="$(brew --prefix sqlite)"
cmake --build build
ctest --test-dir build --output-on-failure
```

### Windows

可使用 Visual Studio 2022 和 vcpkg。在“Developer PowerShell for VS 2022”中：

```powershell
vcpkg install sqlite3:x64-windows
cmake -S . -B build -A x64 -DCMAKE_TOOLCHAIN_FILE=C:/path/to/vcpkg/scripts/buildsystems/vcpkg.cmake
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
```

将 `C:/path/to/vcpkg` 换成实际目录。也可以使用包含 SQLite3 开发包的 MSYS2/MinGW 环境。

## 运行

Linux/macOS：

```sh
./build/homework-grader
./build/homework-grader --db ./test-data/homework.db
```

使用 Visual Studio 多配置生成器时，Windows 可执行文件通常位于：

```powershell
.\build\Debug\homework-grader.exe
.\build\Debug\homework-grader.exe --db .\test-data\homework.db
```

数据库路径优先级为 `--db <路径>`、环境变量 `HOMEWORK_GRADER_DB`、系统默认位置。程序自动创建数据库父目录。

默认位置：

- Windows：`%LOCALAPPDATA%/homework-grader/homework.db`
- macOS：`~/Library/Application Support/homework-grader/homework.db`
- Linux：`$XDG_DATA_HOME/homework-grader/homework.db`
- Linux 未设置 `XDG_DATA_HOME`：`~/.local/share/homework-grader/homework.db`

## 返回上一界面

在新建作业的任一输入项、选择要打开或删除的作业、新增学生记录、选择或重新输入待修改记录时，可以输入 `-1` 取消当前操作并返回所属菜单。作业菜单也可以输入 `-1` 返回作业列表。

已有的返回方式继续有效：选择作业时可以输入 `0`，作业菜单可以选择 `4`，确认提示可以输入 `n`。主菜单没有上一层，因此不接受 `-1`，仍应选择 `4` 退出程序；`y/n` 确认提示也只接受 `y` 或 `n`。

## 完整交互示例

下面演示一份作业从创建、录入、修改、统计到删除的完整流程。菜单会在每次操作后重新显示，所有确认都使用 `y/n`；没有错题时直接按回车即可，因此整个流程都能保持英文输入法。

```text
作业批改统计程序

主菜单
1. 新建作业
2. 打开已有作业
3. 删除已有作业
4. 退出
请选择：1

新建作业
输入 -1 可取消新建并返回主菜单。
作业名称：Week 1
总题目数（至少 4 题，才能设置五个严格递减的等级下限）：10
总学生数：3
请设置五个等级的正确题数下限。
A+ 下限：9
A 下限：8
B 下限：6
C 下限：4
D 下限：2

请确认等级区间（正确题数）：
  A+：9 ～ 10
  A：8 ～ 8
  B：6 ～ 7
  C：4 ～ 5
  D：2 ～ 3
  低于 D：0 ～ 1
保存这份作业吗？（y/n）：y
作业已保存，ID 为 1。

主菜单
请选择：2
已有作业
ID 1 | Week 1 | 10 题 | 0/3 人 | 创建于 2026-08-16T10:00:00.000Z
请输入要打开的作业 ID（输入 0 或 -1 返回主菜单）：1

作业菜单
请选择（输入 -1 也可返回作业列表）：1
请输入错题编号（逗号或空格分隔；全对请直接回车或输入“无”；输入 -1 取消）：2, 5 7
待保存：第 1 位学生
错题编号：2 5 7
错题数量：3
正确题数：7
等级：B
确认保存吗？（y/n）：y
第 1 位学生的记录已保存：正确题数 7，等第 B。

作业菜单
请选择（输入 -1 也可返回作业列表）：1
请输入错题编号（逗号或空格分隔；全对请直接回车或输入“无”；输入 -1 取消）：
待保存：第 2 位学生
错题编号：无（全部正确）
错题数量：0
正确题数：10
等级：A+
确认保存吗？（y/n）：y
第 2 位学生的记录已保存：正确题数 10，等第 A+。

作业菜单
请选择（输入 -1 也可返回作业列表）：2
要修改已经录入学生中的倒数第几个？（输入 -1 取消）：1
修改前：
原始录入序号：2
错题编号：无（全部正确）
正确题数：10
等级：A+
请输入错题编号（逗号或空格分隔；全对请直接回车或输入“无”；输入 -1 取消）：1
修改后：
原始录入序号：2
错题编号：1
正确题数：9
等级：A+
确认替换这份记录吗？（y/n）：y
第 2 位学生的记录已更新：正确题数 9，等第 A+；录入序号保持不变。

作业菜单
请选择（输入 -1 也可返回作业列表）：3
每道题正确率
第 1 题：答对 1 人，答错 1 人，正确率 50.00%
...
作业完成率
已录入学生数：2
总学生数：3
未录入学生数：1
完成率：66.67%

作业菜单
请选择（输入 -1 也可返回作业列表）：4

主菜单
请选择：3

删除已有作业
ID 1 | Week 1 | 10 题 | 2/3 人 | 创建于 2026-08-16T10:00:00.000Z
请输入要删除的作业 ID（输入 0 或 -1 取消删除）：1

即将永久删除：
作业 ID：1
作业名称：Week 1
已录入学生记录：2 份
警告：该作业的所有学生记录和错题数据都会一并删除，且无法撤销。
确认永久删除这份作业吗？（y/n）：y
作业“Week 1”及其全部数据已删除。
```

“倒数第几个”只在当前作业已录入的记录中计算：`1` 是最近录入的一份，`2` 是倒数第二份。修改不会把记录移到末尾，也不会改变其原始录入序号。

删除时，程序会先列出作业及录入进度，再显示目标作业和受影响的记录数。输入 `y` 后，作业、学生记录和错题数据会在同一个 SQLite 事务中永久删除；输入 `0` 取消选择，或在确认时输入 `n`，都不会改变数据。如果以后仍可能用到这份作业，请先按下文方法备份数据库。

## 统计口径

每道题正确率和等级分布只以当前作业已经录入的学生为样本，而不是计划中的总学生数。每题正确率为“答对人数 ÷ 已录入人数”，等级百分比为“该等级人数 ÷ 已录入人数”。作业完成率单独按“已录入人数 ÷ 总学生数”计算。界面统一显示两位小数；尚无记录时前两类百分比显示“暂无数据”，完成率仍为 `0.00%`。

## 安全备份

最稳妥的简单方法是先正常退出程序，再复制 `homework.db` 到备份位置。程序使用 WAL 模式；如果必须在程序运行时进行文件级备份，应同时复制同目录下可能存在的 `homework.db-wal` 和 `homework.db-shm`，不要只复制主文件。更推荐在退出后备份，或使用 SQLite 官方命令行的 `.backup` 命令：

```sh
sqlite3 /path/to/homework.db ".backup '/safe/place/homework-backup.db'"
```

恢复前保留当前数据库副本，确认程序已退出，再替换主数据库。数据库包含教师的实际作业数据，仓库的 `.gitignore` 会阻止常见 SQLite 文件进入 Git；仍请在提交前检查 `git status`。

## 开发验证

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
cmake --build build --target format-check
```

测试在系统临时目录使用独立 SQLite 数据库并自动清理，不读取默认数据库或真实作业数据。

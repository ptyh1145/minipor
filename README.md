# asset-tools

命令行工具，用于读写某款体素沙盒游戏的资源文件（本地研究用途）。

支持的文件格式：

| 格式 | 说明 |
|---|---|
| 资源包（zip） | 按条目读取、替换、重建 |
| 文本资源容器 | 头部混淆 + zlib 压缩的文本资产（CSV / XML / 脚本） |
| 网格文件 `.omod` | 节点树 + 顶点/索引流 |
| 数据表 | 双行表头的 CSV，按列名对齐合并 |

## 工具

```bash
# 读取并解密资源包里的文本资产
python tools/read_assets.py <pack> <entry> -o out.txt

# 把两张表按列名合并（保留目标表结构，填充来源表数据）
python tools/table_merge.py <base.pkg> <old/assets> <output.pkg>

# 端到端：合并数据表 -> 重建资源包 -> 输出新包（含自检）
python tools/build_package.py --base <base.pkg> --assets <old/assets> --out output.pkg

# 网格文件转 OBJ（便于在建模软件里查看）
python tools/model_to_obj.py <input.omod> <out.obj>
```

## 设计原则

* **只做格式转换**：输入文件由使用者自己提供，仓库不含任何游戏资源。
* **构建可验证**：`build_package.py` 每次输出都会自检——条目集合不变、
  可执行文件哈希不变、只有资源包发生改动，否则拒绝出包。
* **表结构优先**：合并数据表时，列结构以目标包为准，来源包只提供数据行；
  引用资源（贴图/音效/模型）和引擎行为类别（类型字段）的列保留目标包取值，
  否则运行时会找不到对应资源。

## 环境

Python 3.10+，仅标准库（zlib / zipfile / struct）。

## 许可

仅供本地学习与互操作研究。使用者需自行确保对输入文件拥有合法使用权。

## 在自己的机器上构建（self-hosted runner）

输入文件留在本机，工作流只读路径，产物也写回本机，不上传任何来源文件。

1. 注册 runner（一次性）：

```powershell
# 仓库 Settings -> Actions -> Runners -> New self-hosted runner 复制命令，形如：
mkdir C:\actions-runner; cd C:\actions-runner
Invoke-WebRequest -Uri https://github.com/actions/runner/releases/download/v2.x/actions-runner-win-x64-2.x.zip -OutFile runner.zip
Expand-Archive runner.zip -DestinationPath .
.\config.cmd --url https://github.com/<owner>/<repo> --token <TOKEN>
.\run.cmd
```

2. 跑构建：Actions -> build -> Run workflow，填三个本机路径：

| 参数 | 例子 |
|---|---|
| `base` | `H:\inputs\base.pkg` |
| `assets` | `H:\inputs\assets` |
| `out_dir` | `H:\output` |

3. 产物：`H:\output\output.pkg`（保留最近 2 个）
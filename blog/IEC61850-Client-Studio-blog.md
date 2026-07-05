# 开源一个桌面级 IEC 61850 客户端工具：IEC 61850 Client Studio

> 基于 Electron + C++ gRPC + libiec61850 的变电站自动化调试客户端，支持 MMS 全栈通信、模型浏览、数据读写、报告控制、GOOSE/SV 订阅。

---

## 为什么做这个工具？

在 IEC 61850 变电站自动化领域，工程调试和运维人员日常需要与智能电子设备（IED）进行通信——浏览在线数据模型、读取/写入数据对象、配置报告控制块（URCB/BRCB）、执行控制操作等。现有的商用工具要么价格昂贵，要么界面陈旧、功能分散。

我决定从零构建一个**现代化的桌面客户端**，目标是：

- **功能完整**：覆盖 MMS 客户端核心能力 + GOOSE/SV 实时报文
- **架构清晰**：前后端分离，可独立演进
- **开箱即用**：无需真实设备也能用 Mock 模式验证流程
- **开源免费**：MIT 协议，任何人都可以使用和贡献

下面是应用启动后的主界面：

![主界面](https://cdn.jsdelivr.net/gh/YlbIce/IEC-61850-Client-Studio@main/docs/blog-images/01-main-ui.png)

顶部是菜单栏和工具栏（连接/模拟/断开/刷新模型/读取/监视），左侧是工程模块导航树，中央是多标签页工作区，右侧为属性面板。整个布局支持拖拽调整面板大小。

---

## 技术架构

### 整体设计

采用 **Electron UI + C++ gRPC 后端** 的前后端分离架构：

```
┌──────────────────────────────────────────────┐
│              Electron 主进程                   │
│    窗口管理 │ 应用菜单 │ IPC/gRPC 桥接        │
├──────────────────────────────────────────────┤
│            preload 安全桥接层                  │
│       (contextIsolation + contextBridge)      │
├──────────────────────────────────────────────┤
│        渲染进程 (桌面工作区 UI)                │
│  模型树 │ 数据读写 │ 数据集 │ 报告 │ 控制 ...   │
├──────────────────────────────────────────────┤
│      gRPC 双向流 (Protobuf) :48650            │
├──────────────────────────────────────────────┤
│           C++ gRPC 后端进程                    │
│     libiec61850 适配层 │ Mock 适配层          │
│               libiec61850 库                  │
└──────────────────────────────────────────────┘
```

### 为什么这样分层？

**Electron 负责所有 UI 相关的事情：**

- 窗口管理（主窗口、独立模块窗口）
- 原生菜单栏（文件/连接/视图/工具/窗口/帮助）
- 多标签页系统 + 可拖拽分割面板
- CSS Grid 四层 Shell 布局（标题栏 → 工具栏 → 工作区 → 状态栏）

**C++ 后端负责所有协议相关的事：**

- 使用 [libiec61850](https://github.com/mz-automation/libiec61850) 开源库实现 MMS 客户端通信
- 通过 gRPC 服务暴露 24 个 RPC 方法给 Electron
- 支持真实 IED 连接 和 Mock 模拟模式

**两者通过 gRPC Protobuf 通信：**

- 定义在 `proto/iec61850studio.proto`，约 343 行
- 包含 30+ 消息类型，覆盖连接、模型、数据、数据集、报告、控制、文件、日志、定值组、GOOSE/SV
- 双向流 `StreamWorkspace` 实现后端状态实时推送

### 安全设计

渲染进程通过 `contextIsolation` 隔离，只能通过 `preload.js` 暴露的受限 API 访问后端：

```javascript
// preload.js — 仅暴露这些安全接口
contextBridge.exposeInMainWorld('iec61850', {
    call(method, payload),      // 调用后端 RPC
    getWorkspace(),             // 获取工作区状态
    onWorkspace(callback),      // 订阅实时更新
    detachModule(moduleId, title)
});
```

---

## 核心功能演示

### 连接 IED 设备

点击工具栏"连接"按钮（或按 F5），输入 IED 的 IP 地址和端口即可建立 MMS 连接：

![连接对话框](https://cdn.jsdelivr.net/gh/YlbIce/IEC-61850-Client-Studio@main/docs/blog-images/02-connect-dialog.png)

输入目标地址后回车确认，应用自动与设备建立 MMS 会话。连接成功后状态栏指示灯变绿。

### 在线模型浏览

连接成功后按 F6 刷新在线模型，左侧导航树展示完整的 **LD → LN → DO → DA** 四级层次结构：

![在线模型目录](https://cdn.jsdelivr.net/gh/YlbIce/IEC-61850-Client-Studio@main/docs/blog-images/06-tab-model.png)

上图展示了从本地 61850 服务器读取到的真实模型：
- **LD**（逻辑设备）：`DemoIEDLD0`
- **LN**（逻辑节点）：`LLN0`
- **DO**（数据对象）：`Mod`, `Beh`
- **DA**（数据属性）：`stVal`, `q`, `t` 等

右侧面板显示选中节点的详细信息（引用、FC、当前值、品质、时标）。支持右键菜单复制引用、跳转关联页面。

### 数据集操作

数据集（DataSet）是 IEC 61850 中将多个数据对象打包用于报告或日志的机制：

![数据集页面](https://cdn.jsdelivr.net/gh/YlbIce/IEC-61850-Client-Studio@main/docs/blog-images/07-tab-datasets.png)

- 自动枚举设备上的所有数据集
- 展示每个数据集的成员列表及引用路径
- 支持**点位巡检表**，标记已测试项（状态持久化到 localStorage）
- "全部标为已测"快捷操作

### 报告控制块配置

报告控制块（Report Control Block, RCB）是 IEC 61850 中事件通知的核心机制：

![报告控制块](https://cdn.jsdelivr.net/gh/YlbIce/IEC-61850-Client-Studio@main/docs/blog-images/08-tab-reports.png)

- 区分 **URCB**（无缓冲报告）和 **BRCB**（缓冲报告）
- 显示每个 RCB 的关联数据集
- 支持读取当前值、设置参数并启用/停用
- 实时接收并展示最新报告数据

### 控制操作

支持 IEC 61850 规定的三种控制模式：

![控制操作](https://cdn.jsdelivr.net/gh/YlbIce/IEC-61850-Client-Studio@main/docs/blog-images/10-tab-control.png)

| 模式 | 说明 |
|------|------|
| Direct | 直接操作，无选择-返回过程 |
| SBO | Select-Before-Operate |
| Enhanced-SBO | 增强 SBO，带附加安全检查 |

右侧"安全边界"面板展示二次确认、SBO 流程、当前状态等信息。

### GOOSE / SV 通信统计

![GOOSE/SV 通信](https://cdn.jsdelivr.net/gh/YlbIce/IEC-61850-Client-Studio@main/docs/blog-images/11-tab-traffic.png)

- MMS 请求/响应计数
- 报告接收统计
- GOOSE 帧、SV 帧订阅入口
- 平均通信延迟监控
- 网卡选择与 AppID/SvID 配置

> 注：GOOSE/SV 实时接收需要 WinPcap/Npcap SDK 支持，MMS 主链路不受影响。

---

## Mock IED 模式

这是我认为最有用的功能之一。点击"模拟"（Ctrl+F5）或工具栏模拟按钮，可以在**没有任何真实 IED 设备**的情况下运行完整的客户端流程：

- Mock 后端提供一套预设的 IEC 61850 数据模型（包含 LD/LN/DO/DA 树）
- 所有 RPC 接口行为一致，只是数据来自模拟
- 适用于：UI 开发调试、CI 流程验证、产品演示

这使得即使在没有物理设备的开发环境中，也能进行全链路测试。

---

## 打包发布

项目使用 `electron-builder` 进行跨平台打包，一条命令生成安装包和便携版：

```bash
npm run dist
```

输出产物：

| 文件 | 类型 | 大小 |
|------|------|------|
| `IEC61850ClientStudio-Setup-0.1.0-x64.exe` | NSIS 安装包 | ~95 MB |
| `IEC61850ClientStudio-Portable-0.1.0-x64.exe` | 便携版 | ~95 MB |

安装包支持自定义安装路径、创建桌面/开始菜单快捷方式；便携版解压即用，适合 U 盘携带。

---

## 项目结构概览

```
Iec61850ClientStudio/
├── src/
│   ├── main/main.js          # Electron 主进程（267行）
│   ├── preload/preload.js    # 安全桥接层（23行）
│   └── renderer/
│       ├── index.html        # 主页面（170行）
│       ├── renderer.js       # 渲染核心逻辑（1802行）
│       └── styles.css        # 样式表（949行）
├── proto/                    # gRPC 接口定义（24个RPC，30+消息类型）
├── backend/src/main.cpp      # C++ gRPC 服务端（~82KB，完整实现）
├── scripts/                  # 构建与启动脚本
└── third_party/libiec61850/  # libiec61850 源码依赖
```

代码量精简但功能完整——前端纯原生 HTML/CSS/JS 无框架依赖，后端单个 C++ 文件承载全部业务逻辑。

---

## 快速开始

### 环境要求

- Windows 10/11 x64
- Node.js >= 18.x
- Visual Studio 2019/2022（含 C++ 桌面工作负载）
- CMake >= 3.18 + vcpkg（管理 Protobuf/gRPC）

```bash
git clone https://github.com/YlbIce/IEC-61850-Client-Studio.git
cd IEC-61850-Client-Studio
npm install
npm run dev          # 编译后端并启动
# 或 npm run dist     # 打包发布版本
```

---

## 未来计划

- [ ] Linux 平台支持
- [ ] SCL/ICD 文件导入离线浏览
- [ ] 报告波形实时绘制
- [ ] 多设备同时连接管理
- [ ] 工程配置持久化（保存/加载工程文件）
- [ ] 插件化协议扩展框架

---

## 开源地址

**GitHub**: [https://github.com/YlbIce/IEC-61850-Client-Studio](https://github.com/YlbIce/IEC-61850-Client-Studio)

欢迎 Star、Fork、Issue 和 Pull Request！

**技术栈一览**: Electron v39 | C++17 | CMake | gRPC | Protobuf | libiec61850

**License**: MIT

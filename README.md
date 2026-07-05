# IEC 61850 Client Studio

[![License](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
[![Electron](https://img.shields.io/badge/Electron-v39.8.10-47848F?logo=electron)](https://www.electronjs.org/)
[![C++](https://img.shields.io/badge/C++-17-00599C?logo=c%2B%2B)](https://en.cppreference.com/)
[![Platform](https://img.shields.io/badge/platform-Windows%20%7C%20Linux-lightgrey)]()
[![gRPC](https://img.shields.io/badge/gRPC-1.x-2DA6B0?logo=grpc)](https://grpc.io/)

**IEC 61850 Client Studio** 是一个基于 Electron 的桌面客户端工具，专为 IEC 61850 变电站自动化系统的工程调试与运维设计。通过现代化的桌面 UI 和强大的 C++ 后端，提供完整的 MMS 客户端能力以及 GOOSE/SV 实时报文订阅功能。

> A desktop IEC 61850 client built with Electron UI and C++ gRPC backend, designed for substation automation engineering and commissioning.

---

## 功能特性

### MMS 客户端
- **连接管理** — 连接/断开 IED 设备、连接模拟 IED、在线模型刷新
- **模型浏览** — 完整的 LD → LN → DO → DA 四级模型树，支持节点过滤与快速跳转
- **数据读写** — 读取/写入数据对象、检查对象属性与类型信息
- **数据集操作** — 读取数据集快照、创建/删除数据集、点位巡检（含测试标记）
- **报告控制** — URCB/BRCB 枚举、参数配置（TrgOps/IntgPd/OptFlds）、关联数据集预览、实时报告
- **控制操作** — direct-operate / SBO / enhanced-SBO 三种控制模式
- **文件服务** — 远程文件目录浏览、文件读取、文件删除
- **日志查询** — IEC 61850 设备日志按时间段过滤检索
- **定值组管理** — SGCB 状态读取、活动定值组切换

### GOOSE / SV 实时订阅
- 支持 GOOSE 报文订阅与解析
- 支持 Sampled Values (SV) 报文订阅
- 通信统计面板（MMS REQ/RSP/RPT 计数）

### Mock IED 模式
- 内置模拟 IED，无需真实设备即可调试 UI 与完整通信链路
- 适用于开发阶段的功能验证与演示

### 桌面工作区
- 多标签页系统（数据模型 / 数据对象 / 数据集 / 报告 / 控制 / 文件 / 定值组 / 通信统计）
- 可拖拽分割面板（左侧导航、右侧属性、底部事件日志）
- 模块独立窗口（可将子页面分离到独立窗口）
- 右键上下文菜单
- 连接状态实时指示灯
- 可重置布局

---

## 技术架构

```
┌────────────────────────────────────────────────────┐
│                  Electron 主进程                    │
│  ┌──────────┐ ┌──────────┐ ┌────────────────────┐ │
│  │ 窗口管理  │ │ 应用菜单  │ │ IPC/gRPC 转发桥接  │ │
│  └──────────┘ └──────────┘ └────────────────────┘ │
├────────────────────────────────────────────────────┤
│              preload 安全桥接层                      │
│         (contextIsolation + contextBridge)          │
├────────────────────────────────────────────────────┤
│              渲染进程 (桌面工作区 UI)                 │
│  ┌──────────────────────────────────────────────┐  │
│  │ 模型树 │ 数据读写 │ 数据集 │ 报告 │ 控制 │... │  │
│  │   拖拽分割布局  │ 右键菜单  │ 独立窗口        │  │
│  └──────────────────────────────────────────────┘  │
├────────────────────────────────────────────────────┤
│         gRPC 双向流 (Protobuf) :48650               │
├────────────────────────────────────────────────────┤
│              C++ gRPC 后端进程                       │
│  ┌──────────────────────────────────────────────┐  │
│  │ libiec61850 适配层  │  Mock IED 适配层       │  │
│  │   (真实 IED 通信)    │  (无设备调试)         │  │
│  └──────────────────────────────────────────────┘  │
│               libiec61850 库                        │
├────────────────────────────────────────────────────┤
│              TCP/IP │ IEC 61850 IED 设备            │
└────────────────────────────────────────────────────┘
```

## 项目结构

```
Iec61850ClientStudio/
├── src/
│   ├── main/main.js              # Electron 主进程
│   ├── preload/preload.js        # 安全桥接层 (contextBridge)
│   └── renderer/
│       ├── index.html            # 主页面
│       ├── renderer.js           # 渲染进程核心逻辑
│       └── styles.css            # 样式表
├── proto/
│   └── iec61850studio.proto      # gRPC/Protobuf 接口定义 (24 个 RPC)
├── backend/
│   ├── src/main.cpp              # C++ gRPC 服务端实现
│   └── generated/                # Protobuf/gRPC 生成代码
├── scripts/
│   ├── build-backend.js          # C++ 后端构建脚本
│   └── run-electron.js           # 应用启动脚本
├── third_party/
│   └── libiec61850/              # libiec61850 开源库
├── CMakeLists.txt                # CMake 构建配置
└── package.json
```

---

## 环境要求

| 组件 | 版本/说明 |
|------|----------|
| Node.js | ≥ 18.x |
| Visual Studio | 2019 或 2022 (含 C++ 桌面开发工作负载) |
| CMake | ≥ 3.18 |
| vcpkg | 管理 Protobuf / gRPC 依赖 |
| Ninja | Windows 构建生成器 (随 VS 安装) |
| WinPcap / Npcap SDK | GOOSE/SV 实时报文接收 (可选) |

---

## 快速开始

### 1. 克隆仓库

```bash
git clone https://github.com/YlbIce/IEC-61850-Client-Studio.git
cd Iec61850ClientStudio
```

### 2. 安装依赖

```bash
npm install
```

### 3. 构建 C++ 后端

```bash
npm run build:backend
```

构建脚本将自动调用 CMake 编译 C++ 后端，产物输出到 `backend/bin/`。

### 4. 启动应用

```bash
npm run dev
```

`npm run dev` 会先构建后端再启动 Electron。

---

## 可用脚本

| 命令 | 说明 |
|------|------|
| `npm run dev` | 构建后端 + 启动应用 |
| `npm run start` | 直接启动 Electron |
| `npm run build:backend` | 构建 C++ 后端 |
| `npm run check` | 语法与代码检查 |

---

## 使用说明

### 连接设备

1. 点击工具栏 **连接** 按钮（或按 `F5`），在弹出的对话框中输入 IED IP 地址
2. 也可通过工具栏 **模拟连接**（`Ctrl+F5`）使用内置 Mock IED
3. 连接成功后状态指示灯变绿，工具栏显示已连接地址

### 模型浏览

- 左侧 **模型树** 面板展示 LD → LN → DO → DA 层级结构
- 支持过滤框快速查找节点
- 右键节点打开上下文菜单，可复制引用、跳转到关联页面

### 模块页面

- 工具栏切换 8 个子模块标签页：数据模型、数据对象、数据集、报告、控制、文件、定值组、通信统计
- 可将标签页分离为 **独立窗口**，方便多屏工作

### 布局调整

- 左侧、右侧、底部面板均支持 **拖拽调整宽度/高度**
- **重置布局** 按钮恢复默认面板尺寸

---

## 开发环境配置

### Windows (推荐)

1. 安装 [Visual Studio 2022](https://visualstudio.microsoft.com/)，勾选"使用 C++ 的桌面开发"工作负载
2. 安装 [vcpkg](https://github.com/microsoft/vcpkg)，并用它安装 Protobuf 和 gRPC:
   ```bash
   vcpkg install protobuf:x64-windows gRPC:x64-windows
   ```
3. 安装 [Node.js](https://nodejs.org/) ≥ 18.x
4. 跟着上面的 [快速开始](#快速开始) 操作即可

### 依赖说明

- **MMS 客户端功能** 不需要 WinPcap/Npcap SDK 即可完全运作
- **GOOSE/SV 实时报文** 依赖于 libiec61850 的 WinPcap/Npcap 二层以太网 HAL，如需启用请在安装对应 SDK 后修改 CMake 构建配置

---

## 技术栈

- **桌面端**: Electron v39
- **前端 UI**: 原生 HTML/CSS/JS (无框架依赖)
- **后端**: C++17, CMake, Ninja
- **通信协议**: gRPC (双向流), Protobuf
- **IEC 61850**: libiec61850 (MMS, GOOSE, SV)
- **安全**: contextIsolation + contextBridge

---

## 开源协议

本项目基于 [MIT License](LICENSE) 开源。

使用的第三方库遵循其各自的许可证：
- [libiec61850](https://github.com/mz-automation/libiec61850) — GPL-3.0
- [Electron](https://github.com/electron/electron) — MIT
- [gRPC](https://github.com/grpc/grpc) — Apache 2.0
- [Protobuf](https://github.com/protocolbuffers/protobuf) — BSD-3-Clause

---

## 贡献

欢迎提交 Issue 和 Pull Request！

1. Fork 本仓库
2. 创建特性分支 (`git checkout -b feature/xxx`)
3. 提交变更 (`git commit -m 'Add xxx feature'`)
4. 推送到分支 (`git push origin feature/xxx`)
5. 创建 Pull Request

---

## 致谢

- [libiec61850](https://github.com/mz-automation/libiec61850) — 提供 IEC 61850 MMS 客户端核心通信能力
- Electron 社区

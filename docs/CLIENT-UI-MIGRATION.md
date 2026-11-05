# Client Studio 0.2.0

界面采用 Service Studio 的桌面布局、菜单、工具栏、页签、配色和三态主题。保留客户端八类业务页面、独立页窗口、属性面板、右键操作及可拖动分隔条。

## 报文与 AI

通过工具栏“报文分析 / AI”或工具菜单 Ctrl+Shift+P 打开独立窗口。支持 PCAP/PCAPNG、MMS/TCP解析、筛选、会话统计、时间回放与数值回放展示；可选捕获 DLL、helper 和 TShark 不存在时，客户端通信不受影响。实时抓取仍需可用的 Npcap 和网卡。

AI 使用 DeepSeek；Key 在客户端自己的用户数据目录加密保存，不读取 Service Studio 的密钥。包含 MMS/TCP 学习课程、逐帧解释、请求响应学习、数据跳变、断连重连线索、独立答案窗口、历史保存以及长回答截断恢复。查看历史和离线课程不发送网络请求。AI 云端诊断需在窗口中配置 Key 并确认数据发送范围。

客户端的回放是报文与解析值的离线回放，不把历史值自动写入真实 IED。Service Studio 专有的本地服务端点值注入接口未接入客户端。

## 工程结构

`src/capture` 及 `backend/capture` 来自 Service Studio 0.3.3；`src/shared/theme*` 是同一主题基础，`src/renderer/studio.css` 实现客户端布局适配。主进程只负责窗口桥接，分析通过独立 helper/DLL，AI 通过主进程受限接口调用。移植目录是客户端自己的源码和运行资源，发布后不依赖相邻的 Service Studio 安装目录。后续修改共有模块时应同步对应测试。

默认客户端 gRPC 管理端口按实例分配，仅绑定本机回环，避免多个应用实例串用后端。原连接、报告、读写等接口不变。

## 验证

- `npm test`：原有 C++ 单元测试。
- `npm run test:ai`：AI、历史、诊断、取消与长回答恢复测试。
- `npm run test:ai-ui`：真实 Electron/捕获模块、模拟云端接口；无实际 API 支出。
- `npm run test:client-ui`：主题、全部页签、独立页、报文模块缺失、PCAP解析；真实 MMS 联调需要相邻 Service Studio 源码作为测试服务端（运行产品不需要）。

## 许可证

本应用及可选报文模块包含 GPL 组件，发行包按 GPL-3.0-or-later 声明，保留根目录 LICENSE 及捕获运行时自带的第三方许可证。不能将这些组件视为原 package.json 中 MIT 声明下的专有可再许可代码。

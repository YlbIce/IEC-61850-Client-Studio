# 0.1.4：真实设备连接兼容排查

## 现场结论（后续确认）

用户已确认此次拒连的实际原因是服务器设备没有部署 IEC 61850 配置文件；部署后恢复。此前 AP-title 差异属于排查发现，不能记作此次故障根因。以下保留参数配置与诊断过程说明。

## 已确认事实

用户反馈 192.168.2.227:102 使用 IEDScout 能连接，旧客户端 connect 返回 MMS 错误 5。用户确认工作目录 `D:/WORKSPACE/61850智能通信服务器/atc61850/osicfg.xml` 属于同一设备。

该配置标明 MMS-EASE Lite。local1 定义服务器 AP-title `1 3 9999 33`、AE-qualifier `33`，T/S/P selectors 为 `0001 / 0001 / 00000001`；remote1 定义对端 AP-title `1 3 9999 23`、AE-qualifier `23`。客户端 Calling/本机使用 remote1，Called/设备使用 local1；不会反向填错。local1cl 的 P-selector 为 `00000002`，本次配置针对 local1，不采用 local1cl。

旧版 libiec61850 默认 Called AP-title 为 `1.1.1.999.1 / 12`，Calling 为 `1.1.1.999 / 12`。已确认参数不同，尚无失败握手证据证明服务端因该字段拒绝连接。MMS-Lite 实际启用的 AR/匹配规则仍由设备程序决定。

[libiec61850 官方错误码文档](https://support.mz-automation.de/doc/libiec61850/c/latest/group__IEC61850__CLIENT__GENERAL.html) 把 5 命名为 IED_ERROR_CONNECTION_REJECTED；本项目所用同步 MmsConnection_connect 实现将异步过程未到达 CONNECTED 的结果统一映射为该错误，因此截图不能单独证明 ACSE 拒绝，也不能区分网络超时。不能据此认定协议栈本身不兼容；[SISCO 官方产品说明](https://www.sisconet.com/products/61850-plus) 说明 MMS Lite 是其 IEC 61850 产品前身。

## 改动

- ConnectionDialog 独立管理地址、超时和 JSON 导入；协议选项由 ConnectionOptions 验证和应用，主窗口不持有字段解析逻辑。
- 标准 / 不发送 AP-title / 自定义 AP-title 三种模式；不自动尝试多个参数、不自动重连。
- T-selector 最长 4 字节，S/P 最长 16 字节，拒绝奇数字符、非十六进制及越界值。AP-title 检查当前协议库整数与 10 字节编码限制，避免静默截断或 OID 编码越界。
- 连接、请求超时可分别设为 1–60 秒，默认 10 秒。每次连接记录参数、关联成功与模型读取阶段，失败记录耗时。关联后模型读取失败时关闭未完成会话，避免界面显示未连接而后台仍占用设备连接槽。
- 配套配置文件包含目标 IP 与协议地址，不包含认证口令。导入只填表，点击连接才访问设备。

## 现场操作

解压 0.1.4，在连接窗口点“导入连接配置…”，选择包内 `profiles/atc61850-local1.json`，核对设备地址后连接。连接其他设备时恢复默认或导入该设备配置，不复用 atc61850 参数。

若仍失败，请从连接开始前抓取 IEDScout 成功与 Lite 失败的 pcap/pcapng；需要完整 TCP 建连、COTP、ACSE AARQ/AARE 与 MMS Initiate，不能仅截取后续 MMS Read。可用 Wireshark 抓取过滤器 `host 192.168.2.227 and tcp port 102`。同时保留新日志，并确认两次尝试的客户端并发连接条件一致。

## 验证边界

使用自有本地 Service Studio 验证标准、不发送 AP-title、自定义设备参数三条连接路径；自定义测试只把目标地址改为 loopback，不访问真实设备。测试验证参数确实写入 libiec61850、无效值被拒绝、其余报告/遥控/监视功能回归。本机服务不能代替真实 MMS-Lite 实现；现场是否恢复须由设备重试确认。

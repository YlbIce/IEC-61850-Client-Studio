"use strict";
const lessons = [
  {
    id: "read",
    title: "看懂一次 MMS 读取",
    source: "https://libiec61850.com/documentation/iec-61850-client-tutorial/",
    text: "先找 Read 请求中的变量引用和 FC，再定位同一连接中对应 invoke ID 的响应。响应值必须与请求变量顺序对应。单独读取 mag.f 不一定包含品质 q 和时间 t。捕获中的响应值不等于客户端最终显示值。",
    question: "单独读取 mag.f，能否直接判断品质良好？",
    choices: ["不能，需要品质属性证据", "能，读取成功就表示品质良好"],
    answer: 0,
    explanation: "成功读到数值与该数值的品质是不同事实，缺少 q 时应保持未知。",
  },
  {
    id: "tcp",
    title: "TCP ACK 与业务成功",
    source: "https://www.rfc-editor.org/rfc/rfc9293.html",
    text: "TCP 提供有序字节流。ACK 确认 TCP 字节，不代表 MMS 写入或遥控执行成功。业务结果需要检查对应应用层响应及必要的后续状态。序号按字节推进，SYN/FIN 各消耗一个序号。工具可能展示相对序号。",
    question: "抓到了 TCP ACK，是否证明遥控成功？",
    choices: ["证明遥控成功", "不能，需要检查应用层结果"],
    answer: 1,
    explanation: "传输确认与业务结果属于不同层次。",
  },
  {
    id: "reassembly",
    title: "分段、重组与 MMS 边界",
    source:
      "https://www.wireshark.org/docs/wsug_html_chunked/ChAdvReassemblySection.html",
    text: "TCP 分段不等于应用消息边界。一条 MMS 消息可能来自多个 TCP 段，一个 TCP 段也可能携带多个应用消息。完整解析可能显示在重组完成的帧上。缺段、截断或从会话中途开始捕获可能限制重组，不能猜测缺失字段。",
    question: "一个 TCP 段是否必然对应一条 MMS 消息？",
    choices: ["是", "不是，两者边界不同"],
    answer: 1,
    explanation: "TCP 是字节流，应用协议还需要自行确定消息边界。",
  },
  {
    id: "report",
    title: "报告与主动读取",
    source: "https://libiec61850.com/documentation/iec-61850-client-tutorial/",
    text: "报告依赖客户端配置和启用报告控制块、数据集及触发条件。InformationReport 不是普通 Read 请求的一问一答。没有观察到报告不一定是服务器故障，也可能缺少前序配置、没有触发或捕获不完整。成员匹配需要数据集与包含位等上下文。",
    question: "没有观察到报告，能否直接认定服务端故障？",
    choices: ["不能，先核对配置、触发和捕获范围", "能"],
    answer: 0,
    explanation: "缺少上下文时应继续调查，不能仅凭未观察到就定责。",
  },
];
module.exports = { lessons };

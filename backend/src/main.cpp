// IEC 61850 Client Studio C++ 后端
//
// 设计目标：
// 1. Electron 只负责 UI、菜单、窗口和布局。
// 2. C++ 后端负责 IEC 61850 客户端能力和业务状态。
// 3. libiec61850 是真实 IED 通信适配层；Mock 适配器只用于无设备时验证界面和流程。
// 4. gRPC/Protobuf 是前后端唯一通信契约。

#include <grpcpp/grpcpp.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cctype>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

extern "C" {
#include "iec61850_client.h"
#include "hal_thread.h"
}

#include "iec61850studio.grpc.pb.h"

namespace {

constexpr char kServerAddress[] = "127.0.0.1:48650";

std::string nowText() {
  const auto now = std::chrono::system_clock::now();
  const std::time_t time = std::chrono::system_clock::to_time_t(now);
  std::tm local{};
#if defined(_WIN32)
  localtime_s(&local, &time);
#else
  localtime_r(&time, &local);
#endif
  std::ostringstream oss;
  oss << std::put_time(&local, "%H:%M:%S");
  return oss.str();
}

std::string errorText(IedClientError error) {
  switch (error) {
    case IED_ERROR_OK:
      return "OK";
    case IED_ERROR_NOT_CONNECTED:
      return "NOT_CONNECTED";
    case IED_ERROR_ALREADY_CONNECTED:
      return "ALREADY_CONNECTED";
    case IED_ERROR_CONNECTION_LOST:
      return "CONNECTION_LOST";
    case IED_ERROR_SERVICE_NOT_SUPPORTED:
      return "SERVICE_NOT_SUPPORTED";
    case IED_ERROR_CONNECTION_REJECTED:
      return "CONNECTION_REJECTED";
    case IED_ERROR_USER_PROVIDED_INVALID_ARGUMENT:
      return "INVALID_ARGUMENT";
    default:
      return "IED_ERROR_" + std::to_string(static_cast<int>(error));
  }
}

std::string fcText(FunctionalConstraint fc) {
  switch (fc) {
    case IEC61850_FC_ST: return "ST";
    case IEC61850_FC_MX: return "MX";
    case IEC61850_FC_SP: return "SP";
    case IEC61850_FC_SV: return "SV";
    case IEC61850_FC_CF: return "CF";
    case IEC61850_FC_DC: return "DC";
    case IEC61850_FC_SG: return "SG";
    case IEC61850_FC_SE: return "SE";
    case IEC61850_FC_SR: return "SR";
    case IEC61850_FC_OR: return "OR";
    case IEC61850_FC_BL: return "BL";
    case IEC61850_FC_EX: return "EX";
    case IEC61850_FC_CO: return "CO";
    case IEC61850_FC_US: return "US";
    case IEC61850_FC_MS: return "MS";
    case IEC61850_FC_RP: return "RP";
    case IEC61850_FC_BR: return "BR";
    case IEC61850_FC_LG: return "LG";
    case IEC61850_FC_GO: return "GO";
    case IEC61850_FC_ALL: return "ALL";
    default: return "";
  }
}

FunctionalConstraint fcFromText(const std::string& text) {
  if (text == "ST") return IEC61850_FC_ST;
  if (text == "MX") return IEC61850_FC_MX;
  if (text == "SP") return IEC61850_FC_SP;
  if (text == "SV") return IEC61850_FC_SV;
  if (text == "CF") return IEC61850_FC_CF;
  if (text == "DC") return IEC61850_FC_DC;
  if (text == "SG") return IEC61850_FC_SG;
  if (text == "SE") return IEC61850_FC_SE;
  if (text == "SR") return IEC61850_FC_SR;
  if (text == "OR") return IEC61850_FC_OR;
  if (text == "BL") return IEC61850_FC_BL;
  if (text == "EX") return IEC61850_FC_EX;
  if (text == "CO") return IEC61850_FC_CO;
  if (text == "US") return IEC61850_FC_US;
  if (text == "MS") return IEC61850_FC_MS;
  if (text == "RP") return IEC61850_FC_RP;
  if (text == "BR") return IEC61850_FC_BR;
  if (text == "LG") return IEC61850_FC_LG;
  if (text == "GO") return IEC61850_FC_GO;
  return IEC61850_FC_MX;
}

std::string mmsValueToText(MmsValue* value) {
  if (!value) return "<null>";

  char buffer[1024] = {};
  MmsValue_printToBuffer(value, buffer, sizeof(buffer));
  return buffer;
}

std::string mmsTypeName(MmsValue* value) {
  if (!value) return "";
  switch (MmsValue_getType(value)) {
    case MMS_BOOLEAN: return "BOOLEAN";
    case MMS_INTEGER: return "INTEGER";
    case MMS_UNSIGNED: return "UNSIGNED";
    case MMS_FLOAT: return "FLOAT";
    case MMS_VISIBLE_STRING: return "VISIBLE_STRING";
    case MMS_STRING: return "STRING";
    case MMS_UTC_TIME: return "UTC_TIME";
    case MMS_ARRAY: return "ARRAY";
    case MMS_STRUCTURE: return "STRUCTURE";
    case MMS_DATA_ACCESS_ERROR: return "DATA_ACCESS_ERROR";
    default: return MmsValue_getTypeString(value);
  }
}

void destroyLinkedList(LinkedList list) {
  if (list) {
    LinkedList_destroy(list);
  }
}

std::vector<std::string> linkedListToVector(LinkedList list) {
  std::vector<std::string> values;
  for (LinkedList item = LinkedList_getNext(list); item; item = LinkedList_getNext(item)) {
    const char* value = static_cast<const char*>(LinkedList_getData(item));
    if (value) values.emplace_back(value);
  }
  return values;
}

std::string datasetReferenceToMmsReference(const std::string& reference) {
  const auto dot = reference.rfind('.');
  if (dot == std::string::npos) return reference;
  return reference.substr(0, dot) + "$" + reference.substr(dot + 1);
}

std::string logReferenceToMmsReference(const std::string& reference) {
  const auto dot = reference.rfind('.');
  if (dot == std::string::npos) return reference;
  return reference.substr(0, dot) + "$" + reference.substr(dot + 1);
}

std::string dotReference(std::string reference) {
  std::replace(reference.begin(), reference.end(), '$', '.');
  return reference;
}

std::string logicalDeviceOf(const std::string& logicalNode) {
  const auto slash = logicalNode.find('/');
  return slash == std::string::npos ? logicalNode : logicalNode.substr(0, slash);
}

std::string logicalNodeNameOf(const std::string& logicalNode) {
  const auto slash = logicalNode.find('/');
  return slash == std::string::npos ? logicalNode : logicalNode.substr(slash + 1);
}

std::string dataSetReferenceForNode(const std::string& logicalNode, const std::string& dataSetName) {
  const std::string value = dotReference(dataSetName);
  if (value.empty()) return "";
  if (value.find('/') != std::string::npos) return value;
  if (logicalNode.empty()) return value;

  const std::string logicalNodeName = logicalNodeNameOf(logicalNode);
  if (value.rfind(logicalNodeName + ".", 0) == 0) {
    return logicalDeviceOf(logicalNode) + "/" + value;
  }

  if (value.rfind(logicalNode + ".", 0) == 0) return value;
  return logicalNode + "." + value;
}

std::string rcbReferenceForNode(const std::string& logicalNode, const std::string& rcbName, bool buffered) {
  const std::string value = dotReference(rcbName);
  if (value.empty()) return "";
  if (value.find(".RP.") != std::string::npos || value.find(".BR.") != std::string::npos) return value;
  if (value.rfind("RP.", 0) == 0 || value.rfind("BR.", 0) == 0) return logicalNode + "." + value;
  return logicalNode + (buffered ? ".BR." : ".RP.") + value;
}

std::string logicalNodeFromReportReference(const std::string& reportReference) {
  const std::string value = dotReference(reportReference);
  for (const std::string marker : {".RP.", ".BR."}) {
    const auto pos = value.find(marker);
    if (pos != std::string::npos) return value.substr(0, pos);
  }
  return "";
}

std::string dataSetReferenceFromRcb(const std::string& logicalNode, const char* dataSetReference) {
  if (!dataSetReference || std::string(dataSetReference).empty()) return "";
  return dataSetReferenceForNode(logicalNode, dataSetReference);
}

std::string reportHandlerReference(const std::string& rcbReference) {
  std::string value = dotReference(rcbReference);
  if (value.size() < 2) return value;
  std::size_t pos = value.size();
  while (pos > 0 && std::isdigit(static_cast<unsigned char>(value[pos - 1]))) {
    --pos;
  }
  if (pos < value.size()) return value.substr(0, pos);
  return value;
}

std::string fcFromMemberReference(const std::string& reference) {
  const auto open = reference.rfind('[');
  const auto close = reference.rfind(']');
  if (open == std::string::npos || close == std::string::npos || close <= open) return "";
  return reference.substr(open + 1, close - open - 1);
}

std::string stripFcSuffix(const std::string& reference) {
  const auto open = reference.rfind('[');
  const auto close = reference.rfind(']');
  if (open == std::string::npos || close != reference.size() - 1 || close <= open) return dotReference(reference);
  return dotReference(reference.substr(0, open));
}

std::string dataObjectReferenceFromMember(const std::string& reference) {
  const std::string withoutFc = stripFcSuffix(reference);
  const auto slash = withoutFc.find('/');
  const auto firstDotAfterLogicalNode = withoutFc.find('.', slash == std::string::npos ? 0 : slash + 1);
  if (firstDotAfterLogicalNode == std::string::npos) return withoutFc;
  const auto secondDot = withoutFc.find('.', firstDotAfterLogicalNode + 1);
  if (secondDot == std::string::npos) return withoutFc;
  return withoutFc.substr(0, secondDot);
}

std::string tailName(const std::string& reference) {
  const std::string normalized = stripFcSuffix(reference);
  const auto pos = normalized.find_last_of("/.");
  return pos == std::string::npos ? normalized : normalized.substr(pos + 1);
}

MmsValue* createMmsValueFromText(const std::string& type, const std::string& text) {
  try {
    if (type == "boolean" || type == "BOOLEAN") {
      return MmsValue_newBoolean(text == "true" || text == "1" || text == "on");
    }
    if (type == "float" || type == "FLOAT" || type == "FLOAT32" || type == "double") {
      return MmsValue_newFloat(std::stof(text));
    }
    if (type == "integer" || type == "INTEGER" || type == "INT32") {
      return MmsValue_newIntegerFromInt32(std::stoi(text));
    }
  } catch (...) {
    return nullptr;
  }
  return MmsValue_newVisibleString(text.c_str());
}

ControlModel controlModelFromText(const std::string& text) {
  if (text == "sbo" || text == "select-before-operate") return CONTROL_MODEL_SBO_NORMAL;
  if (text == "enhanced-sbo") return CONTROL_MODEL_SBO_ENHANCED;
  if (text == "enhanced-direct") return CONTROL_MODEL_DIRECT_ENHANCED;
  return CONTROL_MODEL_DIRECT_NORMAL;
}

std::string timeTextFromMs(uint64_t ms) {
  if (ms == 0) return "";
  const std::time_t time = static_cast<std::time_t>(ms / 1000);
  std::tm local{};
#if defined(_WIN32)
  localtime_s(&local, &time);
#else
  localtime_r(&time, &local);
#endif
  std::ostringstream oss;
  oss << std::put_time(&local, "%Y-%m-%d %H:%M:%S");
  return oss.str();
}

uint32_t parseBitMask(const std::string& text, uint32_t fallback = 0) {
  if (text.empty()) return fallback;
  try {
    return static_cast<uint32_t>(std::stoul(text, nullptr, 0));
  } catch (...) {
    return fallback;
  }
}

std::string reasonText(ReasonForInclusion reason) {
  if (reason == IEC61850_REASON_NOT_INCLUDED) return "not-included";
  std::vector<std::string> parts;
  if (reason & IEC61850_REASON_DATA_CHANGE) parts.emplace_back("data-change");
  if (reason & IEC61850_REASON_QUALITY_CHANGE) parts.emplace_back("quality-change");
  if (reason & IEC61850_REASON_DATA_UPDATE) parts.emplace_back("data-update");
  if (reason & IEC61850_REASON_INTEGRITY) parts.emplace_back("integrity");
  if (reason & IEC61850_REASON_GI) parts.emplace_back("gi");
  if (reason & IEC61850_REASON_UNKNOWN) parts.emplace_back("unknown");
  if (parts.empty()) return std::to_string(reason);
  std::ostringstream oss;
  for (std::size_t i = 0; i < parts.size(); ++i) {
    if (i > 0) oss << ",";
    oss << parts[i];
  }
  return oss.str();
}

struct FileBuffer {
  std::string data;
};

bool collectFileBytes(void* parameter, uint8_t* buffer, uint32_t bytesRead) {
  auto* output = static_cast<FileBuffer*>(parameter);
  if (!output || !buffer) return false;
  output->data.append(reinterpret_cast<const char*>(buffer), bytesRead);
  return true;
}

class StudioModel {
 public:
  StudioModel() : worker_([this] { runLoop(); }) {
    pushEvent("INFO", "后端", "IEC 61850 Client Studio backend started");
  }

  ~StudioModel() { stop(); }

  StudioModel(const StudioModel&) = delete;
  StudioModel& operator=(const StudioModel&) = delete;

  iec61850studio::ServerInfo serverInfo() const {
    iec61850studio::ServerInfo info;
    info.set_name("IEC 61850 Client Studio Backend");
    info.set_version("0.1.0");
    info.set_lib_name("libiec61850");
    info.set_lib_version("1.6.1");
    info.set_transport(std::string("gRPC ") + kServerAddress);
    return info;
  }

  iec61850studio::WorkspaceState workspace() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return makeWorkspaceLocked();
  }

  iec61850studio::CommandReply connect(const iec61850studio::ConnectRequest& request) {
    std::lock_guard<std::mutex> lock(mutex_);
    disconnectLocked("reconnect");

    host_ = request.host().empty() ? "127.0.0.1" : request.host();
    port_ = request.port() > 0 ? request.port() : 102;
    tls_ = request.use_tls();
    mock_ = request.mock();

    if (mock_) {
      connected_ = true;
      stateMessage_ = "Mock station connected";
      connectedSince_ = nowText();
      loadMockModelLocked();
      pushEventLocked("INFO", "连接", "已连接模拟 IED：MockBay");
      notify();
      return ok("已连接模拟 IED。");
    }

    IedClientError error = IED_ERROR_OK;
    connection_ = IedConnection_create();
    IedConnection_connect(connection_, &error, host_.c_str(), port_);

    if (error != IED_ERROR_OK) {
      IedConnection_destroy(connection_);
      connection_ = nullptr;
      connected_ = false;
      mock_ = false;
      stateMessage_ = "连接失败：" + errorText(error);
      clearModelLocked("未连接");
      pushEventLocked("ERROR", "连接", stateMessage_);
      notify();
      return fail(stateMessage_ + "。");
    }

    connected_ = true;
    stateMessage_ = "Connected";
    connectedSince_ = nowText();
    pushEventLocked("INFO", "连接", "已连接 IED " + host_ + ":" + std::to_string(port_));
    refreshModelLocked();
    notify();
    return ok("已连接 IED。");
  }

  iec61850studio::CommandReply disconnect(const std::string& reason) {
    std::lock_guard<std::mutex> lock(mutex_);
    disconnectLocked(reason.empty() ? "用户断开" : reason);
    notify();
    return ok("连接已断开。");
  }

  iec61850studio::ModelTree refreshModel(bool force) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (connected_ && !mock_ && connection_) {
      refreshModelLocked();
    } else if (connected_ && mock_ && (force || model_.roots_size() == 0)) {
      loadMockModelLocked();
    } else {
      clearModelLocked("未连接");
      pushEventLocked("WARN", "模型", "未连接 IED，无法读取在线模型。");
    }
    notify();
    return model_;
  }

  iec61850studio::BrowseResponse browse(const iec61850studio::BrowseRequest& request) {
    std::lock_guard<std::mutex> lock(mutex_);
    iec61850studio::BrowseResponse response;
    response.set_reference(request.reference());
    auto* root = findNode(model_, request.reference());
    if (root) {
      for (const auto& child : root->children()) {
        *response.add_children() = child;
      }
    }
    return response;
  }

  iec61850studio::DataValue readObject(const iec61850studio::ReadObjectRequest& request) {
    std::lock_guard<std::mutex> lock(mutex_);
    requests_ += 1;
    if (connected_ && mock_) {
      return mockValue(request.reference(), request.fc());
    }
    if (!connected_ || !connection_) {
      return notConnectedValue(request.reference(), request.fc());
    }

    IedClientError error = IED_ERROR_OK;
    MmsValue* value = IedConnection_readObject(connection_, &error, request.reference().c_str(), fcFromText(request.fc()));
    iec61850studio::DataValue result;
    result.set_reference(request.reference());
    result.set_fc(request.fc());
    result.set_source("libiec61850");
    result.set_timestamp(nowText());

    if (error != IED_ERROR_OK || value == nullptr) {
      result.set_error(errorText(error));
      pushEventLocked("ERROR", "读取", request.reference() + " " + errorText(error));
      return result;
    }

    result.set_type(mmsTypeName(value));
    result.set_value(mmsValueToText(value));
    result.set_quality("from-device");
    MmsValue_delete(value);
    responses_ += 1;
    pushEventLocked("INFO", "读取", request.reference() + " = " + result.value());
    notify();
    return result;
  }

  iec61850studio::CommandReply writeObject(const iec61850studio::WriteObjectRequest& request) {
    std::lock_guard<std::mutex> lock(mutex_);
    requests_ += 1;
    if (connected_ && mock_) {
      pushEventLocked("WARN", "写入", "模拟写入 " + request.reference() + " = " + request.value());
      notify();
      return ok("模拟写入已记录。真实写入需要连接 IED。");
    }
    if (!connected_ || !connection_) {
      return fail("未连接 IED，无法写入。");
    }

    MmsValue* value = createMmsValueFromText(request.type(), request.value());
    if (!value) {
      pushEventLocked("ERROR", "写入", "无法按指定类型构造 MMS 值：" + request.type());
      notify();
      return fail("写入失败：值类型转换失败。");
    }

    IedClientError error = IED_ERROR_OK;
    IedConnection_writeObject(connection_, &error, request.reference().c_str(), fcFromText(request.fc()), value);
    MmsValue_delete(value);

    if (error != IED_ERROR_OK) {
      pushEventLocked("ERROR", "写入", request.reference() + " " + errorText(error));
      notify();
      return fail("写入失败：" + errorText(error));
    }

    responses_ += 1;
    pushEventLocked("INFO", "写入", request.reference() + " = " + request.value());
    notify();
    return ok("写入成功。");
  }

  iec61850studio::DataSetSnapshot readDataSet(const std::string& reference) {
    std::lock_guard<std::mutex> lock(mutex_);
    iec61850studio::DataSetSnapshot snapshot;
    snapshot.set_reference(reference);
    snapshot.set_deletable(reference.find("@") != std::string::npos);

    if (connected_ && mock_) {
      addDataSetMember(snapshot, "DemoIEDLD0/LLN0.Health.stVal", "ST");
      addDataSetMember(snapshot, "DemoIEDLD0/MMXU1.TotW.mag.f", "MX");
      auto* first = snapshot.add_values();
      *first = mockValue("DemoIEDLD0/LLN0.Health.stVal", "ST");
      auto* second = snapshot.add_values();
      *second = mockValue("DemoIEDLD0/MMXU1.TotW.mag.f", "MX");
      addDataSetPoint(snapshot, snapshot.members(0), *first);
      addDataSetPoint(snapshot, snapshot.members(1), *second);
      cacheDataSetMembersLocked(snapshot);
      applyLiveValuesToSnapshotLocked(snapshot);
      return snapshot;
    }
    if (!connected_ || !connection_) {
      return snapshot;
    }

    IedClientError error = IED_ERROR_OK;
    bool deletable = false;
    LinkedList members = IedConnection_getDataSetDirectory(connection_, &error, reference.c_str(), &deletable);
    snapshot.set_deletable(deletable);
    if (error == IED_ERROR_OK && members) {
      for (const auto& member : linkedListToVector(members)) {
        addDataSetMember(snapshot, member, "");
      }
    }
    destroyLinkedList(members);
    cacheDataSetMembersLocked(snapshot);

    ClientDataSet dataSet = IedConnection_readDataSetValues(connection_, &error, reference.c_str(), nullptr);
    if (error == IED_ERROR_OK && dataSet) {
      MmsValue* values = ClientDataSet_getValues(dataSet);
      const int count = values ? MmsValue_getArraySize(values) : 0;
      for (int i = 0; i < count; ++i) {
        MmsValue* item = MmsValue_getElement(values, i);
        auto* dataValue = snapshot.add_values();
        dataValue->set_reference(i < snapshot.members_size() ? snapshot.members(i).reference() : reference);
        dataValue->set_fc(i < snapshot.members_size() ? snapshot.members(i).fc() : "");
        dataValue->set_type(mmsTypeName(item));
        dataValue->set_value(mmsValueToText(item));
        dataValue->set_quality("from-device");
        dataValue->set_timestamp(nowText());
        dataValue->set_source("libiec61850");
        if (i < snapshot.members_size()) {
          addDataSetPoint(snapshot, snapshot.members(i), *dataValue);
        }
      }
      ClientDataSet_destroy(dataSet);
    }
    applyLiveValuesToSnapshotLocked(snapshot);
    return snapshot;
  }

  iec61850studio::CommandReply createDataSet(const iec61850studio::CreateDataSetRequest& request) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (connected_ && mock_) {
      pushEventLocked("WARN", "数据集", "模拟创建数据集：" + request.reference());
      notify();
      return ok("模拟数据集创建已记录。真实创建需要连接 IED。");
    }
    if (!connected_ || !connection_) {
      return fail("未连接 IED，无法创建数据集。");
    }

    // libiec61850 要求成员引用使用 FCD/Fcda 格式，例如 LD/LN.DO.da[MX]。
    LinkedList elements = LinkedList_create();
    std::vector<std::string> stableMembers;
    stableMembers.reserve(request.members_size());
    for (const auto& member : request.members()) {
      std::string ref = member.reference();
      if (!member.fc().empty() && ref.find('[') == std::string::npos) {
        ref += "[" + member.fc() + "]";
      }
      stableMembers.push_back(ref);
      LinkedList_add(elements, const_cast<char*>(stableMembers.back().c_str()));
    }

    IedClientError error = IED_ERROR_OK;
    IedConnection_createDataSet(connection_, &error, request.reference().c_str(), elements);
    LinkedList_destroyStatic(elements);

    if (error != IED_ERROR_OK) {
      pushEventLocked("ERROR", "数据集", "创建失败：" + request.reference() + " " + errorText(error));
      notify();
      return fail("创建数据集失败：" + errorText(error));
    }

    pushEventLocked("INFO", "数据集", "已创建数据集：" + request.reference());
    notify();
    return ok("数据集创建成功。");
  }

  iec61850studio::CommandReply deleteDataSet(const std::string& reference) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (connected_ && mock_) {
      pushEventLocked("WARN", "数据集", "模拟删除数据集：" + reference);
      notify();
      return ok("模拟数据集删除已记录。真实删除需要连接 IED。");
    }
    if (!connected_ || !connection_) {
      return fail("未连接 IED，无法删除数据集。");
    }

    IedClientError error = IED_ERROR_OK;
    const bool deleted = IedConnection_deleteDataSet(connection_, &error, reference.c_str());
    if (!deleted || error != IED_ERROR_OK) {
      pushEventLocked("ERROR", "数据集", "删除失败：" + reference + " " + errorText(error));
      notify();
      return fail("删除数据集失败：" + errorText(error));
    }

    pushEventLocked("INFO", "数据集", "已删除数据集：" + reference);
    notify();
    return ok("数据集删除成功。");
  }

  iec61850studio::ReportControlBlockList reportBlocks(const std::string& logicalNode) {
    std::lock_guard<std::mutex> lock(mutex_);
    iec61850studio::ReportControlBlockList list;
    const std::string base = logicalNode.empty() ? "DemoIEDLD0/LLN0" : logicalNode;
    if (connected_ && mock_) {
      const std::string urcb = base + ".RP.EventsRCB01";
      const std::string brcb = base + ".BR.EventsBuffered01";
      addRcb(list, urcb, false);
      addRcb(list, brcb, true);
      list.mutable_blocks(0)->set_enabled(enabledReports_[urcb]);
      list.mutable_blocks(1)->set_enabled(enabledReports_[brcb]);
      return list;
    }
    if (!connected_ || !connection_) {
      return list;
    }

    IedClientError error = IED_ERROR_OK;
    for (const auto& classAndBuffered : {std::pair<ACSIClass, bool>{ACSI_CLASS_URCB, false}, {ACSI_CLASS_BRCB, true}}) {
      LinkedList blocks = IedConnection_getLogicalNodeDirectory(connection_, &error, base.c_str(), classAndBuffered.first);
      if (error == IED_ERROR_OK && blocks) {
        for (const auto& name : linkedListToVector(blocks)) {
          const bool buffered = classAndBuffered.second;
          const std::string reference = rcbReferenceForNode(base, name, buffered);
          ClientReportControlBlock rcb = IedConnection_getRCBValues(connection_, &error, reference.c_str(), nullptr);
          if (error == IED_ERROR_OK && rcb) {
            addRcbFromDevice(list, rcb);
            ClientReportControlBlock_destroy(rcb);
          } else {
            addRcb(list, reference, buffered);
          }
        }
      }
      destroyLinkedList(blocks);
    }
    return list;
  }

  iec61850studio::CommandReply setReportBlock(const iec61850studio::ReportControlBlock& rcb) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (connected_ && mock_) {
      enabledReports_[rcb.reference()] = rcb.enabled();
      if (!rcb.data_set().empty()) {
        rcbDataSetByReference_[rcb.reference()] = rcb.data_set();
        ensureDataSetMembersLocked(rcb.data_set());
      }
      pushEventLocked("INFO", "报告", std::string(rcb.enabled() ? "模拟启用 " : "模拟停用 ") + rcb.reference());
      notify();
      return ok("模拟报告控制块参数已记录。真实设置需要连接 IED。");
    }
    if (!connected_ || !connection_) {
      return fail("未连接 IED，无法设置报告控制块。");
    }

    IedClientError error = IED_ERROR_OK;
    ClientReportControlBlock deviceRcb = IedConnection_getRCBValues(connection_, &error, rcb.reference().c_str(), nullptr);
    if (error != IED_ERROR_OK || !deviceRcb) {
      pushEventLocked("ERROR", "报告", "读取 RCB 失败：" + rcb.reference() + " " + errorText(error));
      notify();
      return fail("读取 RCB 失败：" + errorText(error));
    }

    const std::string logicalNode = logicalNodeFromReportReference(rcb.reference());
    const std::string currentDataSet = dataSetReferenceFromRcb(logicalNode, ClientReportControlBlock_getDataSetReference(deviceRcb));
    const std::string dataSet = rcb.data_set().empty() ? currentDataSet : rcb.data_set();
    const std::string rptId = rcb.rpt_id().empty() && ClientReportControlBlock_getRptId(deviceRcb)
      ? ClientReportControlBlock_getRptId(deviceRcb)
      : rcb.rpt_id();

    uint32_t mask = RCB_ELEMENT_RPT_ENA;
    ClientReportControlBlock_setRptEna(deviceRcb, rcb.enabled());

    if (rcb.enabled()) {
      if (!rptId.empty()) {
        ClientReportControlBlock_setRptId(deviceRcb, rptId.c_str());
        mask |= RCB_ELEMENT_RPT_ID;
      }
      if (!dataSet.empty()) {
        const std::string mmsDataSet = datasetReferenceToMmsReference(dataSet);
        ClientReportControlBlock_setDataSetReference(deviceRcb, mmsDataSet.c_str());
        mask |= RCB_ELEMENT_DATSET;
      }
      ClientReportControlBlock_setBufTm(deviceRcb, static_cast<uint32_t>(rcb.buf_time_ms()));
      mask |= RCB_ELEMENT_BUF_TM;
      ClientReportControlBlock_setIntgPd(deviceRcb, static_cast<uint32_t>(rcb.integrity_period_ms()));
      mask |= RCB_ELEMENT_INTG_PD;
      if (!rcb.trigger_options().empty()) {
        ClientReportControlBlock_setTrgOps(deviceRcb, static_cast<int>(parseBitMask(rcb.trigger_options(), ClientReportControlBlock_getTrgOps(deviceRcb))));
        mask |= RCB_ELEMENT_TRG_OPS;
      }
      if (!rcb.optional_fields().empty()) {
        ClientReportControlBlock_setOptFlds(deviceRcb, static_cast<int>(parseBitMask(rcb.optional_fields(), ClientReportControlBlock_getOptFlds(deviceRcb))));
        mask |= RCB_ELEMENT_OPT_FLDS;
      }
      if (rcb.reservation()) {
        ClientReportControlBlock_setResv(deviceRcb, true);
        mask |= RCB_ELEMENT_RESV;
      }
      if (ClientReportControlBlock_hasResvTms(deviceRcb) && rcb.reservation_time_s() > 0) {
        ClientReportControlBlock_setResvTms(deviceRcb, static_cast<int16_t>(rcb.reservation_time_s()));
        mask |= RCB_ELEMENT_RESV_TMS;
      }
      if (rcb.purge_buf()) {
        ClientReportControlBlock_setPurgeBuf(deviceRcb, true);
        mask |= RCB_ELEMENT_PURGE_BUF;
      }
      if (rcb.gi()) {
        ClientReportControlBlock_setGI(deviceRcb, true);
        mask |= RCB_ELEMENT_GI;
      }

      ensureDataSetMembersLocked(dataSet);
      const std::string handlerReference = reportHandlerReference(rcb.reference());
      IedConnection_installReportHandler(connection_, handlerReference.c_str(), rptId.empty() ? nullptr : rptId.c_str(), &StudioModel::reportCallback, this);
      rcbDataSetByReference_[rcb.reference()] = dataSet;
      rcbDataSetByReference_[handlerReference] = dataSet;
    }

    IedConnection_setRCBValues(connection_, &error, deviceRcb, mask, true);
    ClientReportControlBlock_destroy(deviceRcb);
    if (error != IED_ERROR_OK) {
      pushEventLocked("ERROR", "报告", "设置 RCB 失败：" + rcb.reference() + " " + errorText(error));
      notify();
      return fail("设置 RCB 失败：" + errorText(error));
    }

    enabledReports_[rcb.reference()] = rcb.enabled();
    if (!rcb.enabled()) {
      IedConnection_uninstallReportHandler(connection_, rcb.reference().c_str());
      IedConnection_uninstallReportHandler(connection_, reportHandlerReference(rcb.reference()).c_str());
    }
    pushEventLocked("INFO", "报告", std::string(rcb.enabled() ? "启用 " : "停用 ") + rcb.reference());
    notify();
    return ok("报告控制块参数已下发。");
  }

  iec61850studio::CommandReply operate(const iec61850studio::ControlRequest& request) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (connected_ && mock_) {
      pushEventLocked("WARN", "控制", "模拟控制操作：" + request.reference() + " = " + request.value());
      notify();
      return ok("模拟控制命令已记录。真实操作需要连接 IED。");
    }
    if (!connected_ || !connection_) {
      return fail("未连接 IED，无法执行控制。");
    }

    ControlObjectClient control = ControlObjectClient_create(request.reference().c_str(), connection_);
    if (!control) {
      pushEventLocked("ERROR", "控制", "控制对象不存在或不可控：" + request.reference());
      notify();
      return fail("控制对象不存在或不可控。");
    }

    ControlObjectClient_setControlModel(control, controlModelFromText(request.ctl_model()));
    ControlObjectClient_setOrigin(control, "IEC61850ClientStudio", 3);
    MmsValue* ctlValue = createMmsValueFromText("boolean", request.value());
    if (!ctlValue) {
      ControlObjectClient_destroy(control);
      return fail("控制值转换失败。");
    }

    bool success = true;
    if (request.select_before_operate()) {
      const ControlModel model = ControlObjectClient_getControlModel(control);
      success = (model == CONTROL_MODEL_SBO_ENHANCED) ? ControlObjectClient_selectWithValue(control, ctlValue)
                                                      : ControlObjectClient_select(control);
    }
    if (success) {
      success = ControlObjectClient_operate(control, ctlValue, 0);
    }
    const IedClientError lastError = ControlObjectClient_getLastError(control);
    MmsValue_delete(ctlValue);
    ControlObjectClient_destroy(control);

    if (!success || lastError != IED_ERROR_OK) {
      pushEventLocked("ERROR", "控制", "控制失败：" + request.reference() + " " + errorText(lastError));
      notify();
      return fail("控制失败：" + errorText(lastError));
    }

    pushEventLocked("INFO", "控制", "控制成功：" + request.reference() + " = " + request.value());
    notify();
    return ok("控制执行成功。");
  }

  iec61850studio::FileList files(const std::string& directory) {
    std::lock_guard<std::mutex> lock(mutex_);
    iec61850studio::FileList list;
    list.set_directory(directory.empty() ? "/" : directory);
    if (connected_ && mock_) {
      addFile(list, "COMTRADE", "/COMTRADE", true, 0);
      addFile(list, "events_20260626.log", "/events_20260626.log", false, 18432);
      addFile(list, "fault_001.cfg", "/COMTRADE/fault_001.cfg", false, 2048);
      addFile(list, "fault_001.dat", "/COMTRADE/fault_001.dat", false, 524288);
      return list;
    }
    if (!connected_ || !connection_) {
      return list;
    }

    IedClientError error = IED_ERROR_OK;
    const char* dir = directory.empty() || directory == "/" ? nullptr : directory.c_str();
    LinkedList entries = IedConnection_getFileDirectory(connection_, &error, dir);
    if (error == IED_ERROR_OK && entries) {
      for (LinkedList item = LinkedList_getNext(entries); item; item = LinkedList_getNext(item)) {
        FileDirectoryEntry entry = static_cast<FileDirectoryEntry>(LinkedList_getData(item));
        const char* name = FileDirectoryEntry_getFileName(entry);
        const uint32_t size = FileDirectoryEntry_getFileSize(entry);
        const uint64_t modified = FileDirectoryEntry_getLastModified(entry);
        auto* out = list.add_entries();
        out->set_name(name ? name : "");
        out->set_path((directory.empty() || directory == "/") ? std::string("/") + (name ? name : "") : directory + "/" + (name ? name : ""));
        out->set_directory(size == 0 && name && std::string(name).find('.') == std::string::npos);
        out->set_size(size);
        out->set_modified(timeTextFromMs(modified));
      }
      LinkedList_destroyDeep(entries, reinterpret_cast<LinkedListValueDeleteFunction>(FileDirectoryEntry_destroy));
    } else {
      pushEventLocked("ERROR", "文件", "读取目录失败：" + errorText(error));
    }
    return list;
  }

  iec61850studio::FileContent readFile(const std::string& path) {
    std::lock_guard<std::mutex> lock(mutex_);
    iec61850studio::FileContent content;
    content.set_path(path);
    if (connected_ && mock_) {
      content.set_text_preview("Mock 文件内容预览：真实 IED 连接后通过 libiec61850 file service 下载。");
      return content;
    }
    if (!connected_ || !connection_) {
      content.set_text_preview("未连接 IED，无法读取文件。");
      return content;
    }

    FileBuffer buffer;
    IedClientError error = IED_ERROR_OK;
    IedConnection_getFile(connection_, &error, path.c_str(), collectFileBytes, &buffer);
    if (error != IED_ERROR_OK) {
      content.set_text_preview("读取失败：" + errorText(error));
      return content;
    }
    content.set_data(buffer.data);
    content.set_text_preview(buffer.data.substr(0, std::min<std::size_t>(buffer.data.size(), 4096)));
    return content;
  }

  iec61850studio::CommandReply deleteFile(const std::string& path) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (connected_ && mock_) {
      pushEventLocked("WARN", "文件", "模拟删除文件：" + path);
      notify();
      return ok("模拟文件删除已记录。真实删除需要连接 IED。");
    }
    if (!connected_ || !connection_) {
      return fail("未连接 IED，无法删除文件。");
    }

    IedClientError error = IED_ERROR_OK;
    IedConnection_deleteFile(connection_, &error, path.c_str());
    if (error != IED_ERROR_OK) {
      pushEventLocked("ERROR", "文件", "删除失败：" + path + " " + errorText(error));
      notify();
      return fail("删除文件失败：" + errorText(error));
    }

    pushEventLocked("INFO", "文件", "已删除文件：" + path);
    notify();
    return ok("文件删除成功。");
  }

  iec61850studio::LogEntryList logs(const std::string& logicalNode, const std::string& logReference) {
    std::lock_guard<std::mutex> lock(mutex_);
    iec61850studio::LogEntryList list;
    if (connected_ && mock_) {
      auto* entry = list.add_entries();
      entry->set_time(nowText());
      entry->set_reference(logReference.empty() ? logicalNode + ".EventLog" : logReference);
      entry->set_reason("integrity");
      *entry->add_values() = mockValue("DemoIEDLD0/LLN0.Mod.stVal", "ST");
      return list;
    }
    if (!connected_ || !connection_) {
      return list;
    }

    const std::string ref = logReference.empty() ? logReferenceToMmsReference(logicalNode + ".EventLog") : logReferenceToMmsReference(logReference);
    IedClientError error = IED_ERROR_OK;
    bool moreFollows = false;
    const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    LinkedList entries = IedConnection_queryLogByTime(connection_, &error, ref.c_str(), static_cast<uint64_t>(now - 24LL * 60LL * 60LL * 1000LL), static_cast<uint64_t>(now), &moreFollows);
    if (error == IED_ERROR_OK && entries) {
      for (LinkedList item = LinkedList_getNext(entries); item; item = LinkedList_getNext(item)) {
        MmsJournalEntry journalEntry = static_cast<MmsJournalEntry>(LinkedList_getData(item));
        auto* entry = list.add_entries();
        entry->set_time(mmsValueToText(MmsJournalEntry_getOccurenceTime(journalEntry)));
        entry->set_reference(ref);
        entry->set_reason(moreFollows ? "more-follows" : "log");
        LinkedList variables = MmsJournalEntry_getJournalVariables(journalEntry);
        for (LinkedList varItem = LinkedList_getNext(variables); varItem; varItem = LinkedList_getNext(varItem)) {
          MmsJournalVariable variable = static_cast<MmsJournalVariable>(LinkedList_getData(varItem));
          auto* value = entry->add_values();
          value->set_reference(MmsJournalVariable_getTag(variable));
          value->set_value(mmsValueToText(MmsJournalVariable_getValue(variable)));
          value->set_type(mmsTypeName(MmsJournalVariable_getValue(variable)));
          value->set_timestamp(nowText());
          value->set_source("libiec61850");
        }
      }
      LinkedList_destroyDeep(entries, reinterpret_cast<LinkedListValueDeleteFunction>(MmsJournalEntry_destroy));
      return list;
    }

    pushEventLocked("ERROR", "日志", "查询日志失败：" + ref + " " + errorText(error));
    return list;
  }

  iec61850studio::SettingGroupState settingGroups(const std::string& logicalNode) {
    std::lock_guard<std::mutex> lock(mutex_);
    iec61850studio::SettingGroupState state;
    state.set_logical_node(logicalNode.empty() ? "DemoIEDLD0/LLN0" : logicalNode);
    if (connected_ && mock_) {
      state.set_active_group(1);
      state.set_editable_group(2);
      state.set_count(4);
      return state;
    }
    if (!connected_ || !connection_) {
      return state;
    }

    IedClientError error = IED_ERROR_OK;
    MmsValue* sgcb = IedConnection_readObject(connection_, &error, (state.logical_node() + ".SGCB").c_str(), IEC61850_FC_SP);
    if (error == IED_ERROR_OK && sgcb && MmsValue_getType(sgcb) == MMS_STRUCTURE) {
      state.set_count(parseIntegerElement(sgcb, 0));
      state.set_active_group(parseIntegerElement(sgcb, 1));
      state.set_editable_group(parseIntegerElement(sgcb, 2));
      MmsValue_delete(sgcb);
      return state;
    }
    if (sgcb) MmsValue_delete(sgcb);
    pushEventLocked("ERROR", "定值组", "读取 SGCB 失败：" + state.logical_node() + " " + errorText(error));
    return state;
  }

  iec61850studio::CommandReply setSettingGroup(const std::string& logicalNode, int group) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (connected_ && mock_) {
      pushEventLocked("WARN", "定值组", logicalNode + " 模拟切换活动组到 " + std::to_string(group));
      notify();
      return ok("模拟定值组切换已记录。真实切换需要连接 IED。");
    }
    if (!connected_ || !connection_) {
      return fail("未连接 IED，无法切换定值组。");
    }

    IedClientError error = IED_ERROR_OK;
    MmsValue* value = MmsValue_newIntegerFromInt32(group);
    IedConnection_writeObject(connection_, &error, (logicalNode + ".SGCB.ActSG").c_str(), IEC61850_FC_SP, value);
    MmsValue_delete(value);
    if (error != IED_ERROR_OK) {
      pushEventLocked("ERROR", "定值组", logicalNode + " 切换失败：" + errorText(error));
      notify();
      return fail("定值组切换失败：" + errorText(error));
    }

    pushEventLocked("INFO", "定值组", logicalNode + " 切换活动组到 " + std::to_string(group));
    notify();
    return ok("定值组切换成功。");
  }

  iec61850studio::CommandReply subscribeGoose(const std::string& appId) {
    std::lock_guard<std::mutex> lock(mutex_);
    pushEventLocked("INFO", "GOOSE", "订阅配置已保存：" + appId + "。GOOSE 报文接收需要绑定网卡和报文线程，已作为产品扩展点保留。");
    notify();
    return ok("GOOSE 订阅配置已保存。");
  }

  iec61850studio::CommandReply subscribeSv(const std::string& svId) {
    std::lock_guard<std::mutex> lock(mutex_);
    pushEventLocked("INFO", "SV", "采样值订阅配置已保存：" + svId + "。SV 接收需要绑定网卡和采样帧解析线程，已作为产品扩展点保留。");
    notify();
    return ok("Sampled Values 订阅配置已保存。");
  }

  iec61850studio::TrafficSnapshot traffic() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return makeTrafficLocked();
  }

  bool waitTick(std::chrono::milliseconds timeout) {
    std::unique_lock<std::mutex> lock(tickMutex_);
    const auto current = tick_;
    cv_.wait_for(lock, timeout, [&] { return tick_ != current || stopping_.load(); });
    return !stopping_.load();
  }

  bool stopping() const { return stopping_.load(); }

  iec61850studio::DataObjectInspection inspectDataObject(const iec61850studio::InspectDataObjectRequest& request) {
    std::lock_guard<std::mutex> lock(mutex_);
    iec61850studio::DataObjectInspection inspection;
    inspection.set_object_reference(request.object_reference());
    addPointAttributes(inspection, request.object_reference(), request.fc());
    return inspection;
  }

  void stop() {
    const bool wasStopping = stopping_.exchange(true);
    if (!wasStopping) {
      cv_.notify_all();
    }
    if (worker_.joinable()) {
      worker_.join();
    }
    std::lock_guard<std::mutex> lock(mutex_);
    disconnectLocked("backend stop");
  }

 private:
  static iec61850studio::CommandReply ok(const std::string& message) {
    iec61850studio::CommandReply reply;
    reply.set_ok(true);
    reply.set_message(message);
    return reply;
  }

  static iec61850studio::CommandReply fail(const std::string& message) {
    iec61850studio::CommandReply reply;
    reply.set_ok(false);
    reply.set_message(message);
    return reply;
  }

  static int parseIntegerElement(MmsValue* parent, int index) {
    MmsValue* element = MmsValue_getElement(parent, index);
    if (!element) return 0;
    char buffer[64] = {};
    MmsValue_printToBuffer(element, buffer, sizeof(buffer));
    try {
      return std::stoi(buffer);
    } catch (...) {
      return 0;
    }
  }

  static void reportCallback(void* parameter, ClientReport report) {
    auto* model = static_cast<StudioModel*>(parameter);
    if (model) model->handleReport(report);
  }

  void handleReport(ClientReport report) {
    if (!report) return;

    iec61850studio::ReportNotification notification;
    notification.set_time(ClientReport_hasTimestamp(report) ? timeTextFromMs(ClientReport_getTimestamp(report)) : nowText());

    const char* rawRcbReference = ClientReport_getRcbReference(report);
    std::string rcbReference = rawRcbReference ? dotReference(rawRcbReference) : "";
    notification.set_rcb_reference(rcbReference);

    std::string logicalNode = logicalNodeFromReportReference(rcbReference);
    std::string dataSet = ClientReport_hasDataSetName(report)
      ? dataSetReferenceFromRcb(logicalNode, ClientReport_getDataSetName(report))
      : "";

    std::lock_guard<std::mutex> lock(mutex_);
    if (dataSet.empty()) {
      auto it = rcbDataSetByReference_.find(rcbReference);
      if (it == rcbDataSetByReference_.end()) it = rcbDataSetByReference_.find(reportHandlerReference(rcbReference));
      if (it != rcbDataSetByReference_.end()) dataSet = it->second;
    }
    notification.set_data_set(dataSet);

    const auto membersIt = dataSetMembers_.find(dataSet);
    const std::vector<iec61850studio::DataSetMember>* members = membersIt == dataSetMembers_.end() ? nullptr : &membersIt->second;
    MmsValue* values = ClientReport_getDataSetValues(report);
    const int count = values ? MmsValue_getArraySize(values) : 0;
    std::string reportReason = "report";

    for (int i = 0; i < count; ++i) {
      ReasonForInclusion reason = IEC61850_REASON_UNKNOWN;
      if (ClientReport_hasReasonForInclusion(report)) {
        reason = ClientReport_getReasonForInclusion(report, i);
        if (reason == IEC61850_REASON_NOT_INCLUDED) continue;
        if (reportReason == "report") reportReason = reasonText(reason);
      }

      MmsValue* item = MmsValue_getElement(values, i);
      auto* value = notification.add_values();
      std::string reference;
      std::string fc;
      if (ClientReport_hasDataReference(report)) {
        const char* dataReference = ClientReport_getDataReference(report, i);
        if (dataReference) reference = stripFcSuffix(dataReference);
      }
      if (members && i < static_cast<int>(members->size())) {
        if (reference.empty()) reference = members->at(i).reference();
        fc = members->at(i).fc();
      }
      if (reference.empty()) reference = dataSet + "[" + std::to_string(i) + "]";
      if (fc.empty()) fc = fcFromMemberReference(reference);

      value->set_reference(reference);
      value->set_fc(fc);
      value->set_type(mmsTypeName(item));
      value->set_value(mmsValueToText(item));
      value->set_quality(ClientReport_hasReasonForInclusion(report) ? reasonText(reason) : "report");
      value->set_timestamp(notification.time());
      value->set_source("report");
    }

    notification.set_reason(reportReason);
    recordReportLocked(notification);
    notify();
  }

  void notify() {
    {
      std::lock_guard<std::mutex> lock(tickMutex_);
      tick_ += 1;
    }
    cv_.notify_all();
  }

  void runLoop() {
    while (!stopping_.load()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1000));
      {
        std::lock_guard<std::mutex> lock(mutex_);
        if (connected_ && mock_ && !enabledReports_.empty()) {
          for (const auto& item : enabledReports_) {
            if (!item.second) continue;
            iec61850studio::ReportNotification report;
            report.set_time(nowText());
            report.set_rcb_reference(item.first);
            report.set_data_set(rcbDataSetByReference_[item.first].empty() ? "DemoIEDLD0/LLN0.Events" : rcbDataSetByReference_[item.first]);
            report.set_reason("data-change");
            auto value = mockValue("DemoIEDLD0/MMXU1.TotW.mag.f", "MX");
            value.set_value(mockAnalogValueLocked());
            *report.add_values() = value;
            recordReportLocked(report);
          }
        }
      }
      notify();
    }
  }

  std::string mockAnalogValueLocked() const {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2) << (13.0 + static_cast<double>(reports_ % 25) * 0.08) << " MW";
    return oss.str();
  }

  static std::string liveValueKey(const std::string& dataSet, const std::string& reference) {
    return dataSet + "|" + dataObjectReferenceFromMember(reference);
  }

  void recordReportLocked(const iec61850studio::ReportNotification& report) {
    reports_ += 1;
    recentReports_.insert(recentReports_.begin(), report);
    if (recentReports_.size() > 40) recentReports_.resize(40);

    for (const auto& value : report.values()) {
      if (report.data_set().empty() || value.reference().empty()) continue;
      iec61850studio::LiveDataValue live;
      live.set_data_set(report.data_set());
      *live.mutable_value() = value;
      liveValues_[liveValueKey(report.data_set(), value.reference())] = live;
    }
  }

  void cacheDataSetMembersLocked(const iec61850studio::DataSetSnapshot& snapshot) {
    if (snapshot.reference().empty()) return;
    std::vector<iec61850studio::DataSetMember> members;
    members.reserve(snapshot.members_size());
    for (const auto& member : snapshot.members()) {
      members.push_back(member);
    }
    dataSetMembers_[snapshot.reference()] = std::move(members);
  }

  void ensureDataSetMembersLocked(const std::string& dataSetReference) {
    if (dataSetReference.empty() || dataSetMembers_.find(dataSetReference) != dataSetMembers_.end()) return;

    iec61850studio::DataSetSnapshot snapshot;
    snapshot.set_reference(dataSetReference);
    if (connected_ && mock_) {
      addDataSetMember(snapshot, "DemoIEDLD0/LLN0.Health.stVal", "ST");
      addDataSetMember(snapshot, "DemoIEDLD0/MMXU1.TotW.mag.f", "MX");
      cacheDataSetMembersLocked(snapshot);
      return;
    }
    if (!connected_ || !connection_) return;

    IedClientError error = IED_ERROR_OK;
    bool deletable = false;
    LinkedList members = IedConnection_getDataSetDirectory(connection_, &error, dataSetReference.c_str(), &deletable);
    if (error == IED_ERROR_OK && members) {
      for (const auto& member : linkedListToVector(members)) {
        addDataSetMember(snapshot, member, "");
      }
    }
    destroyLinkedList(members);
    cacheDataSetMembersLocked(snapshot);
  }

  void applyLiveValuesToSnapshotLocked(iec61850studio::DataSetSnapshot& snapshot) const {
    if (snapshot.reference().empty() || snapshot.points_size() == 0) return;

    for (auto& point : *snapshot.mutable_points()) {
      const std::string key = liveValueKey(snapshot.reference(), point.object_reference());
      const auto it = liveValues_.find(key);
      if (it == liveValues_.end()) continue;
      *point.mutable_value() = it->second.value();
      point.set_quality(it->second.value().quality());
      point.set_timestamp(it->second.value().timestamp());
    }
  }

  void disconnectLocked(const std::string& reason) {
    if (connection_) {
      IedConnection_close(connection_);
      IedConnection_destroy(connection_);
      connection_ = nullptr;
    }
    if (connected_) {
      pushEventLocked("INFO", "连接", "连接断开：" + reason);
    }
    connected_ = false;
    mock_ = false;
    stateMessage_ = "Disconnected";
    connectedSince_.clear();
    clearModelLocked("未连接");
    recentReports_.clear();
    liveValues_.clear();
    enabledReports_.clear();
    rcbDataSetByReference_.clear();
    dataSetMembers_.clear();
  }

  void refreshModelLocked() {
    model_.Clear();
    model_.set_source(mock_ ? "mock" : "libiec61850");

    if (!connection_) {
      clearModelLocked("未连接");
      return;
    }

    IedClientError error = IED_ERROR_OK;
    LinkedList deviceList = IedConnection_getLogicalDeviceList(connection_, &error);
    if (error != IED_ERROR_OK || deviceList == nullptr) {
      pushEventLocked("ERROR", "模型", "读取逻辑设备失败：" + errorText(error));
      clearModelLocked("读取失败");
      return;
    }

    int logicalDevices = 0;
    int logicalNodes = 0;
    int dataObjects = 0;
    int dataAttributes = 0;

    for (const auto& device : linkedListToVector(deviceList)) {
      logicalDevices += 1;
      auto* deviceNode = model_.add_roots();
      fillNode(*deviceNode, device, device, "LD", "", "", false, false, false);

      LinkedList lnList = IedConnection_getLogicalDeviceDirectory(connection_, &error, device.c_str());
      if (error == IED_ERROR_OK && lnList) {
        for (const auto& ln : linkedListToVector(lnList)) {
          logicalNodes += 1;
          const std::string lnRef = device + "/" + ln;
          auto* lnNode = deviceNode->add_children();
          fillNode(*lnNode, ln, lnRef, "LN", "", "", false, false, false);

          LinkedList doList = IedConnection_getLogicalNodeDirectory(connection_, &error, lnRef.c_str(), ACSI_CLASS_DATA_OBJECT);
          if (error == IED_ERROR_OK && doList) {
            for (const auto& dataObject : linkedListToVector(doList)) {
              dataObjects += 1;
              const std::string doRef = lnRef + "." + dataObject;
              auto* doNode = lnNode->add_children();
              fillNode(*doNode, dataObject, doRef, "DO", "", "", true, false, false);
              addDataAttributesFromDevice(*doNode, doRef, dataAttributes);
            }
          }
          destroyLinkedList(doList);
          addDataSetsFromDevice(*lnNode, lnRef);
          addReportControlsFromDevice(*lnNode, lnRef);
        }
      }
      destroyLinkedList(lnList);
    }

    destroyLinkedList(deviceList);
    model_.set_logical_devices(logicalDevices);
    model_.set_logical_nodes(logicalNodes);
    model_.set_data_objects(dataObjects);
    model_.set_data_attributes(dataAttributes);
    pushEventLocked("INFO", "模型", "在线模型读取完成");
  }

  void addDataAttributesFromDevice(iec61850studio::ModelNode& parent, const std::string& doRef, int& count) {
    if (!connection_) return;
    IedClientError error = IED_ERROR_OK;
    LinkedList daList = IedConnection_getDataDirectoryFC(connection_, &error, doRef.c_str());
    if (error != IED_ERROR_OK || daList == nullptr) return;
    for (const auto& item : linkedListToVector(daList)) {
      count += 1;
      auto* node = parent.add_children();
      const std::string reference = doRef + "." + item;
      fillNode(*node, item, reference, "DA", "", "MMS", true, true, false);
    }
    destroyLinkedList(daList);
  }

  void addDataSetsFromDevice(iec61850studio::ModelNode& logicalNode, const std::string& logicalNodeRef) {
    if (!connection_) return;

    IedClientError error = IED_ERROR_OK;
    LinkedList dataSetList = IedConnection_getLogicalNodeDirectory(connection_, &error, logicalNodeRef.c_str(), ACSI_CLASS_DATA_SET);
    if (error != IED_ERROR_OK || dataSetList == nullptr) {
      destroyLinkedList(dataSetList);
      return;
    }

    auto* group = logicalNode.add_children();
    fillNode(*group, "DataSets", logicalNodeRef + ".$DataSets", "GROUP", "", "DataSetFolder", false, false, false);
    group->set_description("IEC 61850 数据集，结构对应 ICD 中 LN/LN0 下的 DataSet 节点");

    for (const auto& dataSetName : linkedListToVector(dataSetList)) {
      const std::string dataSetRef = dataSetReferenceForNode(logicalNodeRef, dataSetName);
      auto* dataSetNode = group->add_children();
      fillNode(*dataSetNode, dataSetName, dataSetRef, "DS", "", "DataSet", true, true, false);
      dataSetNode->set_description("可读取数据集成员和值，可被 ReportControl.datSet 引用");

      bool deletable = false;
      IedClientError directoryError = IED_ERROR_OK;
      LinkedList members = IedConnection_getDataSetDirectory(connection_, &directoryError, dataSetRef.c_str(), &deletable);
      if (directoryError == IED_ERROR_OK && members) {
        int index = 1;
        for (const auto& member : linkedListToVector(members)) {
          auto* memberNode = dataSetNode->add_children();
          const std::string fc = fcFromMemberReference(member);
          const std::string linkedRef = stripFcSuffix(member);
          fillNode(*memberNode, tailName(member), member, "FCDA", fc, "DataSetMember", true, false, false);
          memberNode->set_linked_reference(linkedRef);
          memberNode->set_link_kind("DATA_OBJECT");
          memberNode->set_description("数据集成员 #" + std::to_string(index));
          index += 1;
        }
      }
      destroyLinkedList(members);
    }

    destroyLinkedList(dataSetList);
  }

  void addReportControlsFromDevice(iec61850studio::ModelNode& logicalNode, const std::string& logicalNodeRef) {
    if (!connection_) return;

    auto* group = logicalNode.add_children();
    fillNode(*group, "ReportControls", logicalNodeRef + ".$ReportControls", "GROUP", "", "ReportControlFolder", false, false, false);
    group->set_description("IEC 61850 报告控制块，结构对应 ICD 中 LN/LN0 下的 ReportControl 节点");

    int count = 0;
    const std::pair<ACSIClass, bool> classes[] = {
      {ACSI_CLASS_URCB, false},
      {ACSI_CLASS_BRCB, true}
    };

    for (const auto& item : classes) {
      IedClientError error = IED_ERROR_OK;
      LinkedList blockList = IedConnection_getLogicalNodeDirectory(connection_, &error, logicalNodeRef.c_str(), item.first);
      if (error != IED_ERROR_OK || blockList == nullptr) {
        destroyLinkedList(blockList);
        continue;
      }

      for (const auto& blockName : linkedListToVector(blockList)) {
        const bool buffered = item.second;
        std::string blockRef = rcbReferenceForNode(logicalNodeRef, blockName, buffered);
        IedClientError rcbError = IED_ERROR_OK;
        ClientReportControlBlock rcb = IedConnection_getRCBValues(connection_, &rcbError, blockRef.c_str(), nullptr);
        std::string dataSetRef;
        std::string rptId;
        int bufTime = 0;
        int intgPeriod = 0;

        if (rcbError == IED_ERROR_OK && rcb) {
          const char* objectRef = ClientReportControlBlock_getObjectReference(rcb);
          if (objectRef && std::string(objectRef).find(".") != std::string::npos) blockRef = dotReference(objectRef);
          dataSetRef = dataSetReferenceFromRcb(logicalNodeRef, ClientReportControlBlock_getDataSetReference(rcb));
          rptId = ClientReportControlBlock_getRptId(rcb) ? ClientReportControlBlock_getRptId(rcb) : "";
          bufTime = ClientReportControlBlock_getBufTm(rcb);
          intgPeriod = ClientReportControlBlock_getIntgPd(rcb);
        }

        auto* blockNode = group->add_children();
        fillNode(*blockNode, tailName(blockRef), blockRef, buffered ? "BRCB" : "URCB", buffered ? "BR" : "RP", "ReportControl", true, true, false);
        blockNode->set_linked_reference(dataSetRef);
        blockNode->set_link_kind("DATASET");
        blockNode->set_description("RptID=" + rptId + " / DatSet=" + dataSetRef);

        auto* dataSetLink = blockNode->add_children();
        fillNode(*dataSetLink, "DatSet", dataSetRef, "LINK", "", "DataSetLink", true, false, false);
        dataSetLink->set_linked_reference(dataSetRef);
        dataSetLink->set_link_kind("DATASET");
        dataSetLink->set_description("ReportControl.datSet 指向的数据集");

        if (!rptId.empty()) {
          auto* rptIdNode = blockNode->add_children();
          fillNode(*rptIdNode, "RptID", rptId, "PROP", "", "string", false, false, false);
          rptIdNode->set_description("报告标识");
        }
        auto* timingNode = blockNode->add_children();
        fillNode(*timingNode, "Timing", blockRef + ".$Timing", "PROP", "", "BufTm/IntgPd", false, false, false);
        timingNode->set_description("bufTime=" + std::to_string(bufTime) + " ms / intgPd=" + std::to_string(intgPeriod) + " ms");

        if (rcb) ClientReportControlBlock_destroy(rcb);
        count += 1;
      }
      destroyLinkedList(blockList);
    }

    if (count == 0) {
      logicalNode.mutable_children()->RemoveLast();
    }
  }

  void loadMockModelLocked() {
    model_.Clear();
    model_.set_source("mock");
    model_.set_logical_devices(2);
    model_.set_logical_nodes(5);
    model_.set_data_objects(12);
    model_.set_data_attributes(42);

    auto* ld0 = model_.add_roots();
    fillNode(*ld0, "DemoIEDLD0", "DemoIEDLD0", "LD", "", "", false, false, false);
    auto* lln0 = ld0->add_children();
    fillNode(*lln0, "LLN0", "DemoIEDLD0/LLN0", "LN", "", "", false, false, false);
    addMockDo(*lln0, "Mod", "ST", true, false);
    addMockDo(*lln0, "Beh", "ST", true, false);
    addMockDo(*lln0, "Health", "ST", true, false);
    addMockDo(*lln0, "NamPlt", "DC", true, false);
    addMockDataSetsAndReports(*lln0);

    auto* mmxu = ld0->add_children();
    fillNode(*mmxu, "MMXU1", "DemoIEDLD0/MMXU1", "LN", "", "", false, false, false);
    addMockDo(*mmxu, "TotW", "MX", true, false);
    addMockDo(*mmxu, "PhV", "MX", true, false);
    addMockDo(*mmxu, "A", "MX", true, false);
    addMockDo(*mmxu, "Hz", "MX", true, false);

    auto* cswi = ld0->add_children();
    fillNode(*cswi, "CSWI1", "DemoIEDLD0/CSWI1", "LN", "", "", false, false, false);
    addMockDo(*cswi, "Pos", "ST", true, true);
    addMockDo(*cswi, "OpOpn", "CO", true, true);
    addMockDo(*cswi, "OpCls", "CO", true, true);

    auto* ld1 = model_.add_roots();
    fillNode(*ld1, "RelayLD", "RelayLD", "LD", "", "", false, false, false);
    auto* pdis = ld1->add_children();
    fillNode(*pdis, "PDIS1", "RelayLD/PDIS1", "LN", "", "", false, false, false);
    addMockDo(*pdis, "Str", "ST", true, false);
    addMockDo(*pdis, "Op", "ST", true, false);
  }

  void clearModelLocked(const std::string& source) {
    model_.Clear();
    model_.set_source(source);
  }

  static void fillNode(iec61850studio::ModelNode& node, const std::string& name, const std::string& reference,
                       const std::string& kind, const std::string& fc, const std::string& type,
                       bool readable, bool writable, bool controllable) {
    node.set_id(reference);
    node.set_name(name);
    node.set_reference(reference);
    node.set_kind(kind);
    node.set_fc(fc);
    node.set_type(type);
    node.set_readable(readable);
    node.set_writable(writable);
    node.set_controllable(controllable);
  }

  void addMockDo(iec61850studio::ModelNode& ln, const std::string& name, const std::string& fc, bool readable, bool controllable) {
    const std::string doRef = ln.reference() + "." + name;
    auto* node = ln.add_children();
    fillNode(*node, name, doRef, "DO", fc, "", readable, false, controllable);
    addMockDa(*node, "stVal", fc, "BOOLEAN", true, true);
    addMockDa(*node, "q", fc, "QUALITY", true, false);
    addMockDa(*node, "t", fc, "TIMESTAMP", true, false);
    if (fc == "MX") {
      auto* mag = node->add_children();
      fillNode(*mag, "mag", doRef + ".mag", "DA", fc, "STRUCT", true, false, false);
      addMockDa(*mag, "f", fc, "FLOAT32", true, false);
    }
  }

  static void addMockDa(iec61850studio::ModelNode& parent, const std::string& name, const std::string& fc,
                        const std::string& type, bool readable, bool writable) {
    auto* child = parent.add_children();
    fillNode(*child, name, parent.reference() + "." + name, "DA", fc, type, readable, writable, false);
  }

  static void addMockDataSetsAndReports(iec61850studio::ModelNode& logicalNode) {
    auto* dataSetGroup = logicalNode.add_children();
    fillNode(*dataSetGroup, "DataSets", logicalNode.reference() + ".$DataSets", "GROUP", "", "DataSetFolder", false, false, false);
    dataSetGroup->set_description("模拟 ICD 中的 DataSet 分组");

    auto* events = dataSetGroup->add_children();
    fillNode(*events, "Events", logicalNode.reference() + ".Events", "DS", "", "DataSet", true, true, false);
    events->set_description("模拟事件数据集");

    auto* member1 = events->add_children();
    fillNode(*member1, "Health.stVal", "DemoIEDLD0/LLN0.Health.stVal[ST]", "FCDA", "ST", "DataSetMember", true, false, false);
    member1->set_linked_reference("DemoIEDLD0/LLN0.Health.stVal");
    member1->set_link_kind("DATA_OBJECT");
    member1->set_description("数据集成员 #1");

    auto* member2 = events->add_children();
    fillNode(*member2, "TotW.mag.f", "DemoIEDLD0/MMXU1.TotW.mag.f[MX]", "FCDA", "MX", "DataSetMember", true, false, false);
    member2->set_linked_reference("DemoIEDLD0/MMXU1.TotW.mag.f");
    member2->set_link_kind("DATA_OBJECT");
    member2->set_description("数据集成员 #2");

    auto* reportGroup = logicalNode.add_children();
    fillNode(*reportGroup, "ReportControls", logicalNode.reference() + ".$ReportControls", "GROUP", "", "ReportControlFolder", false, false, false);
    reportGroup->set_description("模拟 ICD 中的 ReportControl 分组");

    auto* urcb = reportGroup->add_children();
    fillNode(*urcb, "EventsRCB01", logicalNode.reference() + ".RP.EventsRCB01", "URCB", "RP", "ReportControl", true, true, false);
    urcb->set_linked_reference(events->reference());
    urcb->set_link_kind("DATASET");
    urcb->set_description("RptID=Events / DatSet=" + events->reference());

    auto* urcbDataSet = urcb->add_children();
    fillNode(*urcbDataSet, "DatSet", events->reference(), "LINK", "", "DataSetLink", true, false, false);
    urcbDataSet->set_linked_reference(events->reference());
    urcbDataSet->set_link_kind("DATASET");
    urcbDataSet->set_description("ReportControl.datSet 指向的数据集");

    auto* brcb = reportGroup->add_children();
    fillNode(*brcb, "EventsBuffered01", logicalNode.reference() + ".BR.EventsBuffered01", "BRCB", "BR", "ReportControl", true, true, false);
    brcb->set_linked_reference(events->reference());
    brcb->set_link_kind("DATASET");
    brcb->set_description("RptID=EventsBuffered / DatSet=" + events->reference());

    auto* brcbDataSet = brcb->add_children();
    fillNode(*brcbDataSet, "DatSet", events->reference(), "LINK", "", "DataSetLink", true, false, false);
    brcbDataSet->set_linked_reference(events->reference());
    brcbDataSet->set_link_kind("DATASET");
    brcbDataSet->set_description("ReportControl.datSet 指向的数据集");
  }

  static iec61850studio::ModelNode* findNode(iec61850studio::ModelTree& tree, const std::string& reference) {
    for (auto& root : *tree.mutable_roots()) {
      if (auto* found = findNode(root, reference)) return found;
    }
    return nullptr;
  }

  static iec61850studio::ModelNode* findNode(iec61850studio::ModelNode& node, const std::string& reference) {
    if (node.reference() == reference) return &node;
    for (auto& child : *node.mutable_children()) {
      if (auto* found = findNode(child, reference)) return found;
    }
    return nullptr;
  }

  iec61850studio::DataValue mockValue(const std::string& reference, const std::string& fc) const {
    iec61850studio::DataValue value;
    value.set_reference(reference);
    value.set_fc(fc.empty() ? "MX" : fc);
    value.set_timestamp(nowText());
    value.set_source("mock");
    value.set_quality("good");
    if (reference.find("TotW") != std::string::npos) {
      value.set_type("FLOAT32");
      value.set_value("13.72 MW");
    } else if (reference.find("PhV") != std::string::npos) {
      value.set_type("FLOAT32");
      value.set_value("110.04 kV");
    } else if (reference.find("A.") != std::string::npos) {
      value.set_type("FLOAT32");
      value.set_value("512.6 A");
    } else if (reference.find("Pos") != std::string::npos) {
      value.set_type("BOOLEAN");
      value.set_value("true");
    } else {
      value.set_type("BOOLEAN");
      value.set_value("false");
    }
    return value;
  }

  iec61850studio::DataValue notConnectedValue(const std::string& reference, const std::string& fc) const {
    iec61850studio::DataValue value;
    value.set_reference(reference);
    value.set_fc(fc.empty() ? "MX" : fc);
    value.set_timestamp(nowText());
    value.set_source("backend");
    value.set_error("未连接 IED");
    return value;
  }

  static void addDataSetMember(iec61850studio::DataSetSnapshot& snapshot, const std::string& reference, const std::string& fc) {
    auto* member = snapshot.add_members();
    member->set_reference(reference);
    const std::string memberFc = fc.empty() ? fcFromMemberReference(reference) : fc;
    member->set_fc(memberFc);
    member->set_object_reference(dataObjectReferenceFromMember(reference));
    member->set_description("");
  }

  void addDataSetPoint(iec61850studio::DataSetSnapshot& snapshot, const iec61850studio::DataSetMember& member,
                       const iec61850studio::DataValue& value) {
    auto* point = snapshot.add_points();
    point->set_object_reference(member.object_reference().empty() ? dataObjectReferenceFromMember(member.reference()) : member.object_reference());
    point->set_member_reference(member.reference());
    point->set_fc(member.fc());
    *point->mutable_value() = value;
    point->set_quality(value.quality());
    point->set_timestamp(value.timestamp());
    point->set_du(readDescription(point->object_reference(), member.fc()));
  }

  std::string readDescription(const std::string& objectReference, const std::string& fc) {
    const std::string duReference = objectReference + ".dU";
    if (connected_ && mock_) {
      if (objectReference.find("Health") != std::string::npos) return "站控服务健康状态";
      if (objectReference.find("TotW") != std::string::npos) return "主变低压侧有功功率";
      return "模拟点位描述";
    }
    if (!connected_ || !connection_) return "";

    // dU 是 CDC 里的描述文本，通常属于 DC 功能约束。数据集成员可能是 MX/ST，
    // 不能沿用成员 FC 读取 dU，否则部分设备会返回 MMS data-access-error。
    IedClientError error = IED_ERROR_OK;
    MmsValue* value = IedConnection_readObject(connection_, &error, duReference.c_str(), IEC61850_FC_DC);
    if ((error != IED_ERROR_OK || !value || MmsValue_getType(value) == MMS_DATA_ACCESS_ERROR) && fc != "DC") {
      if (value) MmsValue_delete(value);
      error = IED_ERROR_OK;
      value = IedConnection_readObject(connection_, &error, duReference.c_str(), fcFromText(fc));
    }
    if (error != IED_ERROR_OK || !value || MmsValue_getType(value) == MMS_DATA_ACCESS_ERROR) {
      if (value) MmsValue_delete(value);
      return "";
    }
    const std::string text = mmsValueToText(value);
    MmsValue_delete(value);
    return text;
  }

  void addPointAttributes(iec61850studio::DataObjectInspection& inspection, const std::string& objectReference, const std::string& fc) {
    if (connected_ && mock_) {
      for (const auto& name : std::vector<std::string>{"stVal", "q", "t", "dU"}) {
        auto* attribute = inspection.add_attributes();
        const std::string reference = objectReference + "." + name;
        const std::string attrFc = name == "dU" ? "DC" : (fc.empty() ? "ST" : fc);
        *attribute = mockValue(reference, attrFc);
        if (name == "dU") {
          attribute->set_type("VISIBLE_STRING");
          attribute->set_value(readDescription(objectReference, fc));
        }
      }
      return;
    }
    if (!connected_ || !connection_) return;

    IedClientError error = IED_ERROR_OK;
    LinkedList attributes = IedConnection_getDataDirectoryFC(connection_, &error, objectReference.c_str());
    if (error != IED_ERROR_OK || attributes == nullptr) {
      destroyLinkedList(attributes);
      return;
    }

    for (const auto& attributeName : linkedListToVector(attributes)) {
      const std::string attrName = stripFcSuffix(attributeName);
      const std::string reference = objectReference + "." + attrName;
      const std::string attrFc = fcFromMemberReference(attributeName);
      IedClientError readError = IED_ERROR_OK;
      MmsValue* value = IedConnection_readObject(connection_, &readError, reference.c_str(), fcFromText(attrFc.empty() ? fc : attrFc));
      auto* dataValue = inspection.add_attributes();
      dataValue->set_reference(reference);
      dataValue->set_fc(attrFc.empty() ? fc : attrFc);
      dataValue->set_timestamp(nowText());
      dataValue->set_source("libiec61850");
      if (readError == IED_ERROR_OK && value) {
        dataValue->set_type(mmsTypeName(value));
        dataValue->set_value(mmsValueToText(value));
        dataValue->set_quality("from-device");
        MmsValue_delete(value);
      } else {
        dataValue->set_error(errorText(readError));
      }
    }
    destroyLinkedList(attributes);
  }

  static void addRcb(iec61850studio::ReportControlBlockList& list, const std::string& reference, bool buffered) {
    auto* rcb = list.add_blocks();
    rcb->set_reference(reference);
    rcb->set_rpt_id(buffered ? "BufferedEvents01" : "EventsRCB01");
    rcb->set_data_set("DemoIEDLD0/LLN0.Events");
    rcb->set_enabled(false);
    rcb->set_buffered(buffered);
    rcb->set_buf_time_ms(100);
    rcb->set_integrity_period_ms(1000);
    rcb->set_trigger_options(std::to_string(TRG_OPT_DATA_CHANGED | TRG_OPT_QUALITY_CHANGED | TRG_OPT_GI));
    rcb->set_optional_fields(std::to_string(RPT_OPT_SEQ_NUM | RPT_OPT_TIME_STAMP | RPT_OPT_REASON_FOR_INCLUSION | RPT_OPT_DATA_SET));
    rcb->set_conf_rev(1);
    rcb->set_sq_num(0);
    rcb->set_gi(true);
    rcb->set_purge_buf(false);
    rcb->set_reservation(false);
    rcb->set_reservation_time_s(0);
    rcb->set_owner("");
  }

  static void addRcbFromDevice(iec61850studio::ReportControlBlockList& list, ClientReportControlBlock source) {
    auto* rcb = list.add_blocks();
    const char* reference = ClientReportControlBlock_getObjectReference(source);
    const char* rptId = ClientReportControlBlock_getRptId(source);
    const char* dataSet = ClientReportControlBlock_getDataSetReference(source);
    const std::string reportRef = reference ? dotReference(reference) : "";
    rcb->set_reference(reportRef);
    rcb->set_rpt_id(rptId ? rptId : "");
    rcb->set_data_set(dataSetReferenceFromRcb(logicalNodeFromReportReference(reportRef), dataSet));
    rcb->set_enabled(ClientReportControlBlock_getRptEna(source));
    rcb->set_buffered(ClientReportControlBlock_isBuffered(source));
    rcb->set_buf_time_ms(static_cast<int32_t>(ClientReportControlBlock_getBufTm(source)));
    rcb->set_integrity_period_ms(static_cast<int32_t>(ClientReportControlBlock_getIntgPd(source)));
    rcb->set_trigger_options(std::to_string(ClientReportControlBlock_getTrgOps(source)));
    rcb->set_optional_fields(std::to_string(ClientReportControlBlock_getOptFlds(source)));
    rcb->set_conf_rev(ClientReportControlBlock_getConfRev(source));
    rcb->set_sq_num(ClientReportControlBlock_getSqNum(source));
    rcb->set_gi(ClientReportControlBlock_getGI(source));
    rcb->set_purge_buf(ClientReportControlBlock_getPurgeBuf(source));
    rcb->set_reservation(ClientReportControlBlock_getResv(source));
    if (ClientReportControlBlock_hasResvTms(source)) {
      rcb->set_reservation_time_s(ClientReportControlBlock_getResvTms(source));
    }
    MmsValue* owner = ClientReportControlBlock_getOwner(source);
    if (owner) rcb->set_owner(mmsValueToText(owner));
  }

  static void addFile(iec61850studio::FileList& list, const std::string& name, const std::string& path, bool directory, int64_t size) {
    auto* entry = list.add_entries();
    entry->set_name(name);
    entry->set_path(path);
    entry->set_directory(directory);
    entry->set_size(size);
    entry->set_modified(nowText());
  }

  void pushEvent(const std::string& level, const std::string& source, const std::string& message) {
    std::lock_guard<std::mutex> lock(mutex_);
    pushEventLocked(level, source, message);
    notify();
  }

  void pushEventLocked(const std::string& level, const std::string& source, const std::string& message) const {
    auto* event = events_.Add();
    event->set_time(nowText());
    event->set_level(level);
    event->set_source(source);
    event->set_message(message);
    if (events_.size() > 100) {
      events_.DeleteSubrange(0, events_.size() - 100);
    }
  }

  iec61850studio::TrafficSnapshot makeTrafficLocked() const {
    iec61850studio::TrafficSnapshot snapshot;
    snapshot.set_requests(requests_);
    snapshot.set_responses(responses_);
    snapshot.set_reports(reports_);
    snapshot.set_goose_frames(gooseFrames_);
    snapshot.set_sampled_value_frames(svFrames_);
    snapshot.set_average_latency_ms(mock_ ? 1.8 : 4.2);
    return snapshot;
  }

  iec61850studio::WorkspaceState makeWorkspaceLocked() const {
    iec61850studio::WorkspaceState state;
    *state.mutable_server() = serverInfo();
    auto* connection = state.mutable_connection();
    connection->set_connected(connected_);
    connection->set_host(host_);
    connection->set_port(port_);
    connection->set_mock(mock_);
    connection->set_tls(tls_);
    connection->set_state(connected_ ? "CONNECTED" : "DISCONNECTED");
    connection->set_message(stateMessage_);
    connection->set_connected_since(connectedSince_);
    *state.mutable_model() = model_;
    *state.mutable_traffic() = makeTrafficLocked();

    if (connected_ && mock_) {
      *state.add_watch_values() = mockValue("DemoIEDLD0/MMXU1.TotW.mag.f", "MX");
      *state.add_watch_values() = mockValue("DemoIEDLD0/CSWI1.Pos.stVal", "ST");
    }

    for (const auto& report : recentReports_) {
      *state.add_recent_reports() = report;
    }
    for (const auto& live : liveValues_) {
      *state.add_live_values() = live.second;
    }
    for (const auto& event : events_) {
      *state.add_events() = event;
    }
    return state;
  }

  mutable std::mutex mutex_;
  IedConnection connection_ = nullptr;
  bool connected_ = false;
  bool mock_ = false;
  bool tls_ = false;
  std::string host_ = "127.0.0.1";
  int port_ = 102;
  std::string stateMessage_ = "Disconnected";
  std::string connectedSince_;
  iec61850studio::ModelTree model_;
  mutable google::protobuf::RepeatedPtrField<iec61850studio::EventLog> events_;
  std::vector<iec61850studio::ReportNotification> recentReports_;
  std::map<std::string, iec61850studio::LiveDataValue> liveValues_;
  std::map<std::string, bool> enabledReports_;
  std::map<std::string, std::string> rcbDataSetByReference_;
  std::map<std::string, std::vector<iec61850studio::DataSetMember>> dataSetMembers_;
  int64_t requests_ = 0;
  int64_t responses_ = 0;
  int64_t reports_ = 0;
  int64_t gooseFrames_ = 0;
  int64_t svFrames_ = 0;

  std::atomic_bool stopping_{false};
  std::thread worker_;
  std::condition_variable cv_;
  std::mutex tickMutex_;
  std::uint64_t tick_ = 0;
};

class StudioService final : public iec61850studio::Iec61850Studio::Service {
 public:
  explicit StudioService(StudioModel& model) : model_(model) {}

  grpc::Status GetServerInfo(grpc::ServerContext*, const iec61850studio::Empty*, iec61850studio::ServerInfo* response) override {
    *response = model_.serverInfo();
    return grpc::Status::OK;
  }

  grpc::Status GetWorkspace(grpc::ServerContext*, const iec61850studio::Empty*, iec61850studio::WorkspaceState* response) override {
    *response = model_.workspace();
    return grpc::Status::OK;
  }

  grpc::Status StreamWorkspace(grpc::ServerContext* context, const iec61850studio::Empty*, grpc::ServerWriter<iec61850studio::WorkspaceState>* writer) override {
    while (!context->IsCancelled() && !model_.stopping()) {
      if (!writer->Write(model_.workspace())) break;
      model_.waitTick(std::chrono::milliseconds(3000));
    }
    return grpc::Status::OK;
  }

  grpc::Status Connect(grpc::ServerContext*, const iec61850studio::ConnectRequest* request, iec61850studio::CommandReply* response) override {
    *response = model_.connect(*request);
    return grpc::Status::OK;
  }

  grpc::Status Disconnect(grpc::ServerContext*, const iec61850studio::DisconnectRequest* request, iec61850studio::CommandReply* response) override {
    *response = model_.disconnect(request->reason());
    return grpc::Status::OK;
  }

  grpc::Status RefreshModel(grpc::ServerContext*, const iec61850studio::RefreshModelRequest* request, iec61850studio::ModelTree* response) override {
    *response = model_.refreshModel(request->force());
    return grpc::Status::OK;
  }

  grpc::Status Browse(grpc::ServerContext*, const iec61850studio::BrowseRequest* request, iec61850studio::BrowseResponse* response) override {
    *response = model_.browse(*request);
    return grpc::Status::OK;
  }

  grpc::Status ReadObject(grpc::ServerContext*, const iec61850studio::ReadObjectRequest* request, iec61850studio::DataValue* response) override {
    *response = model_.readObject(*request);
    return grpc::Status::OK;
  }

  grpc::Status InspectDataObject(grpc::ServerContext*, const iec61850studio::InspectDataObjectRequest* request,
                                 iec61850studio::DataObjectInspection* response) override {
    *response = model_.inspectDataObject(*request);
    return grpc::Status::OK;
  }

  grpc::Status WriteObject(grpc::ServerContext*, const iec61850studio::WriteObjectRequest* request, iec61850studio::CommandReply* response) override {
    *response = model_.writeObject(*request);
    return grpc::Status::OK;
  }

  grpc::Status ReadDataSet(grpc::ServerContext*, const iec61850studio::ReadDataSetRequest* request, iec61850studio::DataSetSnapshot* response) override {
    *response = model_.readDataSet(request->reference());
    return grpc::Status::OK;
  }

  grpc::Status CreateDataSet(grpc::ServerContext*, const iec61850studio::CreateDataSetRequest* request, iec61850studio::CommandReply* response) override {
    *response = model_.createDataSet(*request);
    return grpc::Status::OK;
  }

  grpc::Status DeleteDataSet(grpc::ServerContext*, const iec61850studio::DeleteDataSetRequest* request, iec61850studio::CommandReply* response) override {
    *response = model_.deleteDataSet(request->reference());
    return grpc::Status::OK;
  }

  grpc::Status GetReportControlBlocks(grpc::ServerContext*, const iec61850studio::ReportQuery* request, iec61850studio::ReportControlBlockList* response) override {
    *response = model_.reportBlocks(request->logical_node());
    return grpc::Status::OK;
  }

  grpc::Status SetReportControlBlock(grpc::ServerContext*, const iec61850studio::ReportControlBlock* request, iec61850studio::CommandReply* response) override {
    *response = model_.setReportBlock(*request);
    return grpc::Status::OK;
  }

  grpc::Status StreamReports(grpc::ServerContext* context, const iec61850studio::ReportStreamRequest*, grpc::ServerWriter<iec61850studio::ReportNotification>* writer) override {
    while (!context->IsCancelled() && !model_.stopping()) {
      auto workspace = model_.workspace();
      if (workspace.recent_reports_size() > 0) {
        if (!writer->Write(workspace.recent_reports(0))) break;
      }
      model_.waitTick(std::chrono::milliseconds(1000));
    }
    return grpc::Status::OK;
  }

  grpc::Status OperateControl(grpc::ServerContext*, const iec61850studio::ControlRequest* request, iec61850studio::CommandReply* response) override {
    *response = model_.operate(*request);
    return grpc::Status::OK;
  }

  grpc::Status GetFiles(grpc::ServerContext*, const iec61850studio::FileQuery* request, iec61850studio::FileList* response) override {
    *response = model_.files(request->directory());
    return grpc::Status::OK;
  }

  grpc::Status ReadFile(grpc::ServerContext*, const iec61850studio::FileReadRequest* request, iec61850studio::FileContent* response) override {
    *response = model_.readFile(request->path());
    return grpc::Status::OK;
  }

  grpc::Status DeleteFile(grpc::ServerContext*, const iec61850studio::FileDeleteRequest* request, iec61850studio::CommandReply* response) override {
    *response = model_.deleteFile(request->path());
    return grpc::Status::OK;
  }

  grpc::Status GetLogs(grpc::ServerContext*, const iec61850studio::LogQuery* request, iec61850studio::LogEntryList* response) override {
    *response = model_.logs(request->logical_node(), request->log_reference());
    return grpc::Status::OK;
  }

  grpc::Status GetSettingGroups(grpc::ServerContext*, const iec61850studio::SettingGroupRequest* request, iec61850studio::SettingGroupState* response) override {
    *response = model_.settingGroups(request->logical_node());
    return grpc::Status::OK;
  }

  grpc::Status SetActiveSettingGroup(grpc::ServerContext*, const iec61850studio::SetSettingGroupRequest* request, iec61850studio::CommandReply* response) override {
    *response = model_.setSettingGroup(request->logical_node(), request->active_group());
    return grpc::Status::OK;
  }

  grpc::Status SubscribeGoose(grpc::ServerContext*, const iec61850studio::GooseSubscriptionRequest* request, iec61850studio::CommandReply* response) override {
    *response = model_.subscribeGoose(request->app_id());
    return grpc::Status::OK;
  }

  grpc::Status SubscribeSampledValues(grpc::ServerContext*, const iec61850studio::SvSubscriptionRequest* request, iec61850studio::CommandReply* response) override {
    *response = model_.subscribeSv(request->sv_id());
    return grpc::Status::OK;
  }

  grpc::Status GetTraffic(grpc::ServerContext*, const iec61850studio::Empty*, iec61850studio::TrafficSnapshot* response) override {
    *response = model_.traffic();
    return grpc::Status::OK;
  }

 private:
  StudioModel& model_;
};

}  // namespace

int main() {
  StudioModel model;
  StudioService service(model);

  grpc::ServerBuilder builder;
  builder.AddListeningPort(kServerAddress, grpc::InsecureServerCredentials());
  builder.RegisterService(&service);

  std::unique_ptr<grpc::Server> server(builder.BuildAndStart());
  if (!server) {
    std::cerr << "Failed to start IEC 61850 Client Studio backend on " << kServerAddress << std::endl;
    return 1;
  }

  std::cout << "IEC 61850 Client Studio backend listening on grpc://" << kServerAddress << std::endl;

  while (!model.stopping()) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }

  server->Shutdown();
  model.stop();
  return 0;
}

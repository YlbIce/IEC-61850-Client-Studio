#include "utils.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>

namespace studio {

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

uint32_t parseBitMask(const std::string& text, uint32_t fallback) {
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

bool collectFileBytes(void* parameter, uint8_t* buffer, uint32_t bytesRead) {
  auto* output = static_cast<FileBuffer*>(parameter);
  if (!output || !buffer) return false;
  output->data.append(reinterpret_cast<const char*>(buffer), bytesRead);
  return true;
}

}  // namespace studio

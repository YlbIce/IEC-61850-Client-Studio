#pragma once

#include <string>
#include <vector>
#include <cstdint>

extern "C" {
#include "iec61850_client.h"
}

namespace studio {

// Time helpers
std::string nowText();
std::string timeTextFromMs(uint64_t ms);

// Error / type helpers
std::string errorText(IedClientError error);
std::string fcText(FunctionalConstraint fc);
FunctionalConstraint fcFromText(const std::string& text);
std::string mmsValueToText(MmsValue* value);
std::string mmsTypeName(MmsValue* value);
MmsValue* createMmsValueFromText(const std::string& type, const std::string& text);
ControlModel controlModelFromText(const std::string& text);
uint32_t parseBitMask(const std::string& text, uint32_t fallback = 0);
std::string reasonText(ReasonForInclusion reason);

// LinkedList helpers
void destroyLinkedList(LinkedList list);
std::vector<std::string> linkedListToVector(LinkedList list);

// Reference conversion helpers
std::string datasetReferenceToMmsReference(const std::string& reference);
std::string logReferenceToMmsReference(const std::string& reference);
std::string dotReference(std::string reference);
std::string logicalDeviceOf(const std::string& logicalNode);
std::string logicalNodeNameOf(const std::string& logicalNode);
std::string dataSetReferenceForNode(const std::string& logicalNode, const std::string& dataSetName);
std::string rcbReferenceForNode(const std::string& logicalNode, const std::string& rcbName, bool buffered);
std::string logicalNodeFromReportReference(const std::string& reportReference);
std::string dataSetReferenceFromRcb(const std::string& logicalNode, const char* dataSetReference);
std::string reportHandlerReference(const std::string& rcbReference);
std::string fcFromMemberReference(const std::string& reference);
std::string stripFcSuffix(const std::string& reference);
std::string dataObjectReferenceFromMember(const std::string& reference);
std::string tailName(const std::string& reference);

// File transfer helper
struct FileBuffer {
  std::string data;
};

bool collectFileBytes(void* parameter, uint8_t* buffer, uint32_t bytesRead);

}  // namespace studio

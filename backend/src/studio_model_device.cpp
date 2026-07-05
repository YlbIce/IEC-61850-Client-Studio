#include "studio_model.h"

#include <algorithm>
#include <chrono>
#include <vector>

#include "utils.h"

using namespace studio;

// =========================================================================
// Model browsing
// =========================================================================

iec61850studio::ModelTree StudioModel::refreshModel(bool force) {
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

iec61850studio::BrowseResponse StudioModel::browse(const iec61850studio::BrowseRequest& request) {
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

void StudioModel::refreshModelLocked() {
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

void StudioModel::addDataAttributesFromDevice(iec61850studio::ModelNode& parent, const std::string& doRef, int& count) {
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

void StudioModel::addDataSetsFromDevice(iec61850studio::ModelNode& logicalNode, const std::string& logicalNodeRef) {
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

void StudioModel::addReportControlsFromDevice(iec61850studio::ModelNode& logicalNode, const std::string& logicalNodeRef) {
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

void StudioModel::fillNode(iec61850studio::ModelNode& node, const std::string& name, const std::string& reference,
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

iec61850studio::ModelNode* StudioModel::findNode(iec61850studio::ModelTree& tree, const std::string& reference) {
  for (auto& root : *tree.mutable_roots()) {
    if (auto* found = findNode(root, reference)) return found;
  }
  return nullptr;
}

iec61850studio::ModelNode* StudioModel::findNode(iec61850studio::ModelNode& node, const std::string& reference) {
  if (node.reference() == reference) return &node;
  for (auto& child : *node.mutable_children()) {
    if (auto* found = findNode(child, reference)) return found;
  }
  return nullptr;
}

// =========================================================================
// Data read / write / inspect
// =========================================================================

iec61850studio::DataValue StudioModel::readObject(const iec61850studio::ReadObjectRequest& request) {
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

iec61850studio::CommandReply StudioModel::writeObject(const iec61850studio::WriteObjectRequest& request) {
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

iec61850studio::DataObjectInspection StudioModel::inspectDataObject(const iec61850studio::InspectDataObjectRequest& request) {
  std::lock_guard<std::mutex> lock(mutex_);
  iec61850studio::DataObjectInspection inspection;
  inspection.set_object_reference(request.object_reference());
  addPointAttributes(inspection, request.object_reference(), request.fc());
  return inspection;
}

void StudioModel::addDataSetMember(iec61850studio::DataSetSnapshot& snapshot, const std::string& reference, const std::string& fc) {
  auto* member = snapshot.add_members();
  member->set_reference(reference);
  const std::string memberFc = fc.empty() ? fcFromMemberReference(reference) : fc;
  member->set_fc(memberFc);
  member->set_object_reference(dataObjectReferenceFromMember(reference));
  member->set_description("");
}

void StudioModel::addDataSetPoint(iec61850studio::DataSetSnapshot& snapshot, const iec61850studio::DataSetMember& member,
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

std::string StudioModel::readDescription(const std::string& objectReference, const std::string& fc) {
  const std::string duReference = objectReference + ".dU";
  if (connected_ && mock_) {
    if (objectReference.find("Health") != std::string::npos) return "站控服务健康状态";
    if (objectReference.find("TotW") != std::string::npos) return "主变低压侧有功功率";
    return "模拟点位描述";
  }
  if (!connected_ || !connection_) return "";

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

void StudioModel::addPointAttributes(iec61850studio::DataObjectInspection& inspection, const std::string& objectReference, const std::string& fc) {
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

// =========================================================================
// DataSet operations
// =========================================================================

iec61850studio::DataSetSnapshot StudioModel::readDataSet(const std::string& reference) {
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

iec61850studio::CommandReply StudioModel::createDataSet(const iec61850studio::CreateDataSetRequest& request) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (connected_ && mock_) {
    pushEventLocked("WARN", "数据集", "模拟创建数据集：" + request.reference());
    notify();
    return ok("模拟数据集创建已记录。真实创建需要连接 IED。");
  }
  if (!connected_ || !connection_) {
    return fail("未连接 IED，无法创建数据集。");
  }

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

iec61850studio::CommandReply StudioModel::deleteDataSet(const std::string& reference) {
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

// =========================================================================
// Report control blocks
// =========================================================================

iec61850studio::ReportControlBlockList StudioModel::reportBlocks(const std::string& logicalNode) {
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

iec61850studio::CommandReply StudioModel::setReportBlock(const iec61850studio::ReportControlBlock& rcb) {
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

void StudioModel::addRcb(iec61850studio::ReportControlBlockList& list, const std::string& reference, bool buffered) {
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

void StudioModel::addRcbFromDevice(iec61850studio::ReportControlBlockList& list, ClientReportControlBlock source) {
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

// =========================================================================
// Control operations
// =========================================================================

iec61850studio::CommandReply StudioModel::operate(const iec61850studio::ControlRequest& request) {
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

// =========================================================================
// File operations
// =========================================================================

iec61850studio::FileList StudioModel::files(const std::string& directory) {
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

iec61850studio::FileContent StudioModel::readFile(const std::string& path) {
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

iec61850studio::CommandReply StudioModel::deleteFile(const std::string& path) {
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

void StudioModel::addFile(iec61850studio::FileList& list, const std::string& name, const std::string& path, bool directory, int64_t size) {
  auto* entry = list.add_entries();
  entry->set_name(name);
  entry->set_path(path);
  entry->set_directory(directory);
  entry->set_size(size);
  entry->set_modified(nowText());
}

// =========================================================================
// Log operations
// =========================================================================

iec61850studio::LogEntryList StudioModel::logs(const std::string& logicalNode, const std::string& logReference) {
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

// =========================================================================
// Setting groups
// =========================================================================

iec61850studio::SettingGroupState StudioModel::settingGroups(const std::string& logicalNode) {
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

iec61850studio::CommandReply StudioModel::setSettingGroup(const std::string& logicalNode, int group) {
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

// =========================================================================
// GOOSE / SV subscriptions
// =========================================================================

iec61850studio::CommandReply StudioModel::subscribeGoose(const std::string& appId) {
  std::lock_guard<std::mutex> lock(mutex_);
  pushEventLocked("INFO", "GOOSE", "订阅配置已保存：" + appId + "。GOOSE 报文接收需要绑定网卡和报文线程，已作为产品扩展点保留。");
  notify();
  return ok("GOOSE 订阅配置已保存。");
}

iec61850studio::CommandReply StudioModel::subscribeSv(const std::string& svId) {
  std::lock_guard<std::mutex> lock(mutex_);
  pushEventLocked("INFO", "SV", "采样值订阅配置已保存：" + svId + "。SV 接收需要绑定网卡和采样帧解析线程，已作为产品扩展点保留。");
  notify();
  return ok("Sampled Values 订阅配置已保存。");
}

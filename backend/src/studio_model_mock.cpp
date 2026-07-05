#include "studio_model.h"

#include "utils.h"

using namespace studio;

void StudioModel::loadMockModelLocked() {
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

void StudioModel::addMockDo(iec61850studio::ModelNode& ln, const std::string& name, const std::string& fc, bool readable, bool controllable) {
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

void StudioModel::addMockDa(iec61850studio::ModelNode& parent, const std::string& name, const std::string& fc,
                            const std::string& type, bool readable, bool writable) {
  auto* child = parent.add_children();
  fillNode(*child, name, parent.reference() + "." + name, "DA", fc, type, readable, writable, false);
}

void StudioModel::addMockDataSetsAndReports(iec61850studio::ModelNode& logicalNode) {
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

iec61850studio::DataValue StudioModel::mockValue(const std::string& reference, const std::string& fc) const {
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

iec61850studio::DataValue StudioModel::notConnectedValue(const std::string& reference, const std::string& fc) const {
  iec61850studio::DataValue value;
  value.set_reference(reference);
  value.set_fc(fc.empty() ? "MX" : fc);
  value.set_timestamp(nowText());
  value.set_source("backend");
  value.set_error("未连接 IED");
  return value;
}

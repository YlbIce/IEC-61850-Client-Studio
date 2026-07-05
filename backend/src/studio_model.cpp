#include "studio_model.h"

#include <sstream>
#include <iomanip>

#include "utils.h"

using namespace studio;

constexpr char kServerAddress[] = "127.0.0.1:48650";

StudioModel::StudioModel() : worker_([this] { runLoop(); }) {
  pushEvent("INFO", "后端", "IEC 61850 Client Studio backend started");
}

StudioModel::~StudioModel() { stop(); }

iec61850studio::ServerInfo StudioModel::serverInfo() const {
  iec61850studio::ServerInfo info;
  info.set_name("IEC 61850 Client Studio Backend");
  info.set_version("0.1.0");
  info.set_lib_name("libiec61850");
  info.set_lib_version("1.6.1");
  info.set_transport(std::string("gRPC ") + kServerAddress);
  return info;
}

iec61850studio::WorkspaceState StudioModel::workspace() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return makeWorkspaceLocked();
}

iec61850studio::CommandReply StudioModel::connect(const iec61850studio::ConnectRequest& request) {
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

iec61850studio::CommandReply StudioModel::disconnect(const std::string& reason) {
  std::lock_guard<std::mutex> lock(mutex_);
  disconnectLocked(reason.empty() ? "用户断开" : reason);
  notify();
  return ok("连接已断开。");
}

void StudioModel::disconnectLocked(const std::string& reason) {
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

void StudioModel::clearModelLocked(const std::string& source) {
  model_.Clear();
  model_.set_source(source);
}

void StudioModel::notify() {
  {
    std::lock_guard<std::mutex> lock(tickMutex_);
    tick_ += 1;
  }
  cv_.notify_all();
}

void StudioModel::pushEvent(const std::string& level, const std::string& source, const std::string& message) {
  std::lock_guard<std::mutex> lock(mutex_);
  pushEventLocked(level, source, message);
  notify();
}

void StudioModel::pushEventLocked(const std::string& level, const std::string& source, const std::string& message) const {
  auto* event = events_.Add();
  event->set_time(nowText());
  event->set_level(level);
  event->set_source(source);
  event->set_message(message);
  if (events_.size() > 100) {
    events_.DeleteSubrange(0, events_.size() - 100);
  }
}

void StudioModel::runLoop() {
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

std::string StudioModel::mockAnalogValueLocked() const {
  std::ostringstream oss;
  oss << std::fixed << std::setprecision(2) << (13.0 + static_cast<double>(reports_ % 25) * 0.08) << " MW";
  return oss.str();
}

void StudioModel::reportCallback(void* parameter, ClientReport report) {
  auto* model = static_cast<StudioModel*>(parameter);
  if (model) model->handleReport(report);
}

void StudioModel::handleReport(ClientReport report) {
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

std::string StudioModel::liveValueKey(const std::string& dataSet, const std::string& reference) {
  return dataSet + "|" + dataObjectReferenceFromMember(reference);
}

void StudioModel::recordReportLocked(const iec61850studio::ReportNotification& report) {
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

void StudioModel::cacheDataSetMembersLocked(const iec61850studio::DataSetSnapshot& snapshot) {
  if (snapshot.reference().empty()) return;
  std::vector<iec61850studio::DataSetMember> members;
  members.reserve(snapshot.members_size());
  for (const auto& member : snapshot.members()) {
    members.push_back(member);
  }
  dataSetMembers_[snapshot.reference()] = std::move(members);
}

void StudioModel::ensureDataSetMembersLocked(const std::string& dataSetReference) {
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

void StudioModel::applyLiveValuesToSnapshotLocked(iec61850studio::DataSetSnapshot& snapshot) const {
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

iec61850studio::CommandReply StudioModel::ok(const std::string& message) {
  iec61850studio::CommandReply reply;
  reply.set_ok(true);
  reply.set_message(message);
  return reply;
}

iec61850studio::CommandReply StudioModel::fail(const std::string& message) {
  iec61850studio::CommandReply reply;
  reply.set_ok(false);
  reply.set_message(message);
  return reply;
}

int StudioModel::parseIntegerElement(MmsValue* parent, int index) {
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

iec61850studio::TrafficSnapshot StudioModel::makeTrafficLocked() const {
  iec61850studio::TrafficSnapshot snapshot;
  snapshot.set_requests(requests_);
  snapshot.set_responses(responses_);
  snapshot.set_reports(reports_);
  snapshot.set_goose_frames(gooseFrames_);
  snapshot.set_sampled_value_frames(svFrames_);
  snapshot.set_average_latency_ms(mock_ ? 1.8 : 4.2);
  return snapshot;
}

iec61850studio::WorkspaceState StudioModel::makeWorkspaceLocked() const {
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

iec61850studio::TrafficSnapshot StudioModel::traffic() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return makeTrafficLocked();
}

bool StudioModel::waitTick(std::chrono::milliseconds timeout) {
  std::unique_lock<std::mutex> lock(tickMutex_);
  const auto current = tick_;
  cv_.wait_for(lock, timeout, [&] { return tick_ != current || stopping_.load(); });
  return !stopping_.load();
}

bool StudioModel::stopping() const { return stopping_.load(); }

void StudioModel::stop() {
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

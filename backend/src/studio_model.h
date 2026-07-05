#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

extern "C" {
#include "iec61850_client.h"
}

#include "iec61850studio.grpc.pb.h"

class StudioModel {
 public:
  StudioModel();
  ~StudioModel();

  StudioModel(const StudioModel&) = delete;
  StudioModel& operator=(const StudioModel&) = delete;

  iec61850studio::ServerInfo serverInfo() const;
  iec61850studio::WorkspaceState workspace() const;

  iec61850studio::CommandReply connect(const iec61850studio::ConnectRequest& request);
  iec61850studio::CommandReply disconnect(const std::string& reason);
  iec61850studio::ModelTree refreshModel(bool force);
  iec61850studio::BrowseResponse browse(const iec61850studio::BrowseRequest& request);

  iec61850studio::DataValue readObject(const iec61850studio::ReadObjectRequest& request);
  iec61850studio::DataObjectInspection inspectDataObject(const iec61850studio::InspectDataObjectRequest& request);
  iec61850studio::CommandReply writeObject(const iec61850studio::WriteObjectRequest& request);

  iec61850studio::DataSetSnapshot readDataSet(const std::string& reference);
  iec61850studio::CommandReply createDataSet(const iec61850studio::CreateDataSetRequest& request);
  iec61850studio::CommandReply deleteDataSet(const std::string& reference);

  iec61850studio::ReportControlBlockList reportBlocks(const std::string& logicalNode);
  iec61850studio::CommandReply setReportBlock(const iec61850studio::ReportControlBlock& rcb);

  iec61850studio::CommandReply operate(const iec61850studio::ControlRequest& request);

  iec61850studio::FileList files(const std::string& directory);
  iec61850studio::FileContent readFile(const std::string& path);
  iec61850studio::CommandReply deleteFile(const std::string& path);

  iec61850studio::LogEntryList logs(const std::string& logicalNode, const std::string& logReference);

  iec61850studio::SettingGroupState settingGroups(const std::string& logicalNode);
  iec61850studio::CommandReply setSettingGroup(const std::string& logicalNode, int group);

  iec61850studio::CommandReply subscribeGoose(const std::string& appId);
  iec61850studio::CommandReply subscribeSv(const std::string& svId);

  iec61850studio::TrafficSnapshot traffic() const;

  bool waitTick(std::chrono::milliseconds timeout);
  bool stopping() const;

  void stop();

 private:
  // Reply helpers
  static iec61850studio::CommandReply ok(const std::string& message);
  static iec61850studio::CommandReply fail(const std::string& message);

  // Report callback
  static void reportCallback(void* parameter, ClientReport report);
  void handleReport(ClientReport report);

  // Event / notify
  void notify();
  void pushEvent(const std::string& level, const std::string& source, const std::string& message);
  void pushEventLocked(const std::string& level, const std::string& source, const std::string& message) const;

  // Worker loop
  void runLoop();
  std::string mockAnalogValueLocked() const;

  // Report / dataset cache helpers
  static std::string liveValueKey(const std::string& dataSet, const std::string& reference);
  void recordReportLocked(const iec61850studio::ReportNotification& report);
  void cacheDataSetMembersLocked(const iec61850studio::DataSetSnapshot& snapshot);
  void ensureDataSetMembersLocked(const std::string& dataSetReference);
  void applyLiveValuesToSnapshotLocked(iec61850studio::DataSetSnapshot& snapshot) const;

  // Connection helpers
  void disconnectLocked(const std::string& reason);

  // Model browsing (device)
  void refreshModelLocked();
  void addDataAttributesFromDevice(iec61850studio::ModelNode& parent, const std::string& doRef, int& count);
  void addDataSetsFromDevice(iec61850studio::ModelNode& logicalNode, const std::string& logicalNodeRef);
  void addReportControlsFromDevice(iec61850studio::ModelNode& logicalNode, const std::string& logicalNodeRef);
  void clearModelLocked(const std::string& source);

  // Mock model
  void loadMockModelLocked();
  void addMockDo(iec61850studio::ModelNode& ln, const std::string& name, const std::string& fc, bool readable, bool controllable);
  static void addMockDa(iec61850studio::ModelNode& parent, const std::string& name, const std::string& fc,
                        const std::string& type, bool readable, bool writable);
  static void addMockDataSetsAndReports(iec61850studio::ModelNode& logicalNode);

  // Model node helpers
  static void fillNode(iec61850studio::ModelNode& node, const std::string& name, const std::string& reference,
                       const std::string& kind, const std::string& fc, const std::string& type,
                       bool readable, bool writable, bool controllable);
  static iec61850studio::ModelNode* findNode(iec61850studio::ModelTree& tree, const std::string& reference);
  static iec61850studio::ModelNode* findNode(iec61850studio::ModelNode& node, const std::string& reference);

  // Mock value helpers
  iec61850studio::DataValue mockValue(const std::string& reference, const std::string& fc) const;
  iec61850studio::DataValue notConnectedValue(const std::string& reference, const std::string& fc) const;

  // Dataset helpers
  static void addDataSetMember(iec61850studio::DataSetSnapshot& snapshot, const std::string& reference, const std::string& fc);
  void addDataSetPoint(iec61850studio::DataSetSnapshot& snapshot, const iec61850studio::DataSetMember& member,
                       const iec61850studio::DataValue& value);
  std::string readDescription(const std::string& objectReference, const std::string& fc);
  void addPointAttributes(iec61850studio::DataObjectInspection& inspection, const std::string& objectReference, const std::string& fc);
  static int parseIntegerElement(MmsValue* parent, int index);

  // RCB helpers
  static void addRcb(iec61850studio::ReportControlBlockList& list, const std::string& reference, bool buffered);
  static void addRcbFromDevice(iec61850studio::ReportControlBlockList& list, ClientReportControlBlock source);

  // File helpers
  static void addFile(iec61850studio::FileList& list, const std::string& name, const std::string& path, bool directory, int64_t size);

  // State builders
  iec61850studio::TrafficSnapshot makeTrafficLocked() const;
  iec61850studio::WorkspaceState makeWorkspaceLocked() const;

  // -----------------------------------------------------------------
  // Member variables
  // -----------------------------------------------------------------
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

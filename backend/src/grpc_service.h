#pragma once

#include <grpcpp/grpcpp.h>

#include "iec61850studio.grpc.pb.h"
#include "studio_model.h"

class StudioService final : public iec61850studio::Iec61850Studio::Service {
 public:
  explicit StudioService(StudioModel& model);

  grpc::Status GetServerInfo(grpc::ServerContext*, const iec61850studio::Empty*, iec61850studio::ServerInfo* response) override;
  grpc::Status GetWorkspace(grpc::ServerContext*, const iec61850studio::Empty*, iec61850studio::WorkspaceState* response) override;
  grpc::Status StreamWorkspace(grpc::ServerContext* context, const iec61850studio::Empty*, grpc::ServerWriter<iec61850studio::WorkspaceState>* writer) override;

  grpc::Status Connect(grpc::ServerContext*, const iec61850studio::ConnectRequest* request, iec61850studio::CommandReply* response) override;
  grpc::Status Disconnect(grpc::ServerContext*, const iec61850studio::DisconnectRequest* request, iec61850studio::CommandReply* response) override;

  grpc::Status RefreshModel(grpc::ServerContext*, const iec61850studio::RefreshModelRequest* request, iec61850studio::ModelTree* response) override;
  grpc::Status Browse(grpc::ServerContext*, const iec61850studio::BrowseRequest* request, iec61850studio::BrowseResponse* response) override;

  grpc::Status ReadObject(grpc::ServerContext*, const iec61850studio::ReadObjectRequest* request, iec61850studio::DataValue* response) override;
  grpc::Status InspectDataObject(grpc::ServerContext*, const iec61850studio::InspectDataObjectRequest* request, iec61850studio::DataObjectInspection* response) override;
  grpc::Status WriteObject(grpc::ServerContext*, const iec61850studio::WriteObjectRequest* request, iec61850studio::CommandReply* response) override;

  grpc::Status ReadDataSet(grpc::ServerContext*, const iec61850studio::ReadDataSetRequest* request, iec61850studio::DataSetSnapshot* response) override;
  grpc::Status CreateDataSet(grpc::ServerContext*, const iec61850studio::CreateDataSetRequest* request, iec61850studio::CommandReply* response) override;
  grpc::Status DeleteDataSet(grpc::ServerContext*, const iec61850studio::DeleteDataSetRequest* request, iec61850studio::CommandReply* response) override;

  grpc::Status GetReportControlBlocks(grpc::ServerContext*, const iec61850studio::ReportQuery* request, iec61850studio::ReportControlBlockList* response) override;
  grpc::Status SetReportControlBlock(grpc::ServerContext*, const iec61850studio::ReportControlBlock* request, iec61850studio::CommandReply* response) override;
  grpc::Status StreamReports(grpc::ServerContext* context, const iec61850studio::ReportStreamRequest*, grpc::ServerWriter<iec61850studio::ReportNotification>* writer) override;

  grpc::Status OperateControl(grpc::ServerContext*, const iec61850studio::ControlRequest* request, iec61850studio::CommandReply* response) override;

  grpc::Status GetFiles(grpc::ServerContext*, const iec61850studio::FileQuery* request, iec61850studio::FileList* response) override;
  grpc::Status ReadFile(grpc::ServerContext*, const iec61850studio::FileReadRequest* request, iec61850studio::FileContent* response) override;
  grpc::Status DeleteFile(grpc::ServerContext*, const iec61850studio::FileDeleteRequest* request, iec61850studio::CommandReply* response) override;

  grpc::Status GetLogs(grpc::ServerContext*, const iec61850studio::LogQuery* request, iec61850studio::LogEntryList* response) override;

  grpc::Status GetSettingGroups(grpc::ServerContext*, const iec61850studio::SettingGroupRequest* request, iec61850studio::SettingGroupState* response) override;
  grpc::Status SetActiveSettingGroup(grpc::ServerContext*, const iec61850studio::SetSettingGroupRequest* request, iec61850studio::CommandReply* response) override;

  grpc::Status SubscribeGoose(grpc::ServerContext*, const iec61850studio::GooseSubscriptionRequest* request, iec61850studio::CommandReply* response) override;
  grpc::Status SubscribeSampledValues(grpc::ServerContext*, const iec61850studio::SvSubscriptionRequest* request, iec61850studio::CommandReply* response) override;
  grpc::Status GetTraffic(grpc::ServerContext*, const iec61850studio::Empty*, iec61850studio::TrafficSnapshot* response) override;

 private:
  StudioModel& model_;
};

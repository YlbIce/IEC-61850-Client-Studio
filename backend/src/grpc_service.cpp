#include "grpc_service.h"

#include <chrono>

StudioService::StudioService(StudioModel& model) : model_(model) {}

grpc::Status StudioService::GetServerInfo(grpc::ServerContext*, const iec61850studio::Empty*, iec61850studio::ServerInfo* response) {
  *response = model_.serverInfo();
  return grpc::Status::OK;
}

grpc::Status StudioService::GetWorkspace(grpc::ServerContext*, const iec61850studio::Empty*, iec61850studio::WorkspaceState* response) {
  *response = model_.workspace();
  return grpc::Status::OK;
}

grpc::Status StudioService::StreamWorkspace(grpc::ServerContext* context, const iec61850studio::Empty*, grpc::ServerWriter<iec61850studio::WorkspaceState>* writer) {
  while (!context->IsCancelled() && !model_.stopping()) {
    if (!writer->Write(model_.workspace())) break;
    model_.waitTick(std::chrono::milliseconds(3000));
  }
  return grpc::Status::OK;
}

grpc::Status StudioService::Connect(grpc::ServerContext*, const iec61850studio::ConnectRequest* request, iec61850studio::CommandReply* response) {
  *response = model_.connect(*request);
  return grpc::Status::OK;
}

grpc::Status StudioService::Disconnect(grpc::ServerContext*, const iec61850studio::DisconnectRequest* request, iec61850studio::CommandReply* response) {
  *response = model_.disconnect(request->reason());
  return grpc::Status::OK;
}

grpc::Status StudioService::RefreshModel(grpc::ServerContext*, const iec61850studio::RefreshModelRequest* request, iec61850studio::ModelTree* response) {
  *response = model_.refreshModel(request->force());
  return grpc::Status::OK;
}

grpc::Status StudioService::Browse(grpc::ServerContext*, const iec61850studio::BrowseRequest* request, iec61850studio::BrowseResponse* response) {
  *response = model_.browse(*request);
  return grpc::Status::OK;
}

grpc::Status StudioService::ReadObject(grpc::ServerContext*, const iec61850studio::ReadObjectRequest* request, iec61850studio::DataValue* response) {
  *response = model_.readObject(*request);
  return grpc::Status::OK;
}

grpc::Status StudioService::InspectDataObject(grpc::ServerContext*, const iec61850studio::InspectDataObjectRequest* request, iec61850studio::DataObjectInspection* response) {
  *response = model_.inspectDataObject(*request);
  return grpc::Status::OK;
}

grpc::Status StudioService::WriteObject(grpc::ServerContext*, const iec61850studio::WriteObjectRequest* request, iec61850studio::CommandReply* response) {
  *response = model_.writeObject(*request);
  return grpc::Status::OK;
}

grpc::Status StudioService::ReadDataSet(grpc::ServerContext*, const iec61850studio::ReadDataSetRequest* request, iec61850studio::DataSetSnapshot* response) {
  *response = model_.readDataSet(request->reference());
  return grpc::Status::OK;
}

grpc::Status StudioService::CreateDataSet(grpc::ServerContext*, const iec61850studio::CreateDataSetRequest* request, iec61850studio::CommandReply* response) {
  *response = model_.createDataSet(*request);
  return grpc::Status::OK;
}

grpc::Status StudioService::DeleteDataSet(grpc::ServerContext*, const iec61850studio::DeleteDataSetRequest* request, iec61850studio::CommandReply* response) {
  *response = model_.deleteDataSet(request->reference());
  return grpc::Status::OK;
}

grpc::Status StudioService::GetReportControlBlocks(grpc::ServerContext*, const iec61850studio::ReportQuery* request, iec61850studio::ReportControlBlockList* response) {
  *response = model_.reportBlocks(request->logical_node());
  return grpc::Status::OK;
}

grpc::Status StudioService::SetReportControlBlock(grpc::ServerContext*, const iec61850studio::ReportControlBlock* request, iec61850studio::CommandReply* response) {
  *response = model_.setReportBlock(*request);
  return grpc::Status::OK;
}

grpc::Status StudioService::StreamReports(grpc::ServerContext* context, const iec61850studio::ReportStreamRequest*, grpc::ServerWriter<iec61850studio::ReportNotification>* writer) {
  while (!context->IsCancelled() && !model_.stopping()) {
    auto workspace = model_.workspace();
    if (workspace.recent_reports_size() > 0) {
      if (!writer->Write(workspace.recent_reports(0))) break;
    }
    model_.waitTick(std::chrono::milliseconds(1000));
  }
  return grpc::Status::OK;
}

grpc::Status StudioService::OperateControl(grpc::ServerContext*, const iec61850studio::ControlRequest* request, iec61850studio::CommandReply* response) {
  *response = model_.operate(*request);
  return grpc::Status::OK;
}

grpc::Status StudioService::GetFiles(grpc::ServerContext*, const iec61850studio::FileQuery* request, iec61850studio::FileList* response) {
  *response = model_.files(request->directory());
  return grpc::Status::OK;
}

grpc::Status StudioService::ReadFile(grpc::ServerContext*, const iec61850studio::FileReadRequest* request, iec61850studio::FileContent* response) {
  *response = model_.readFile(request->path());
  return grpc::Status::OK;
}

grpc::Status StudioService::DeleteFile(grpc::ServerContext*, const iec61850studio::FileDeleteRequest* request, iec61850studio::CommandReply* response) {
  *response = model_.deleteFile(request->path());
  return grpc::Status::OK;
}

grpc::Status StudioService::GetLogs(grpc::ServerContext*, const iec61850studio::LogQuery* request, iec61850studio::LogEntryList* response) {
  *response = model_.logs(request->logical_node(), request->log_reference());
  return grpc::Status::OK;
}

grpc::Status StudioService::GetSettingGroups(grpc::ServerContext*, const iec61850studio::SettingGroupRequest* request, iec61850studio::SettingGroupState* response) {
  *response = model_.settingGroups(request->logical_node());
  return grpc::Status::OK;
}

grpc::Status StudioService::SetActiveSettingGroup(grpc::ServerContext*, const iec61850studio::SetSettingGroupRequest* request, iec61850studio::CommandReply* response) {
  *response = model_.setSettingGroup(request->logical_node(), request->active_group());
  return grpc::Status::OK;
}

grpc::Status StudioService::SubscribeGoose(grpc::ServerContext*, const iec61850studio::GooseSubscriptionRequest* request, iec61850studio::CommandReply* response) {
  *response = model_.subscribeGoose(request->app_id());
  return grpc::Status::OK;
}

grpc::Status StudioService::SubscribeSampledValues(grpc::ServerContext*, const iec61850studio::SvSubscriptionRequest* request, iec61850studio::CommandReply* response) {
  *response = model_.subscribeSv(request->sv_id());
  return grpc::Status::OK;
}

grpc::Status StudioService::GetTraffic(grpc::ServerContext*, const iec61850studio::Empty*, iec61850studio::TrafficSnapshot* response) {
  *response = model_.traffic();
  return grpc::Status::OK;
}

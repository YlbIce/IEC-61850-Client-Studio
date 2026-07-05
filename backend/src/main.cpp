// IEC 61850 Client Studio C++ 后端入口
//
// 设计目标：
// 1. Electron 只负责 UI、菜单、窗口和布局。
// 2. C++ 后端负责 IEC 61850 客户端能力和业务状态。
// 3. libiec61850 是真实 IED 通信适配层；Mock 适配器只用于无设备时验证界面和流程。
// 4. gRPC/Protobuf 是前后端唯一通信契约。

#include <chrono>
#include <iostream>
#include <memory>
#include <thread>

#include <grpcpp/grpcpp.h>

#include "grpc_service.h"
#include "studio_model.h"

namespace {
constexpr char kServerAddress[] = "127.0.0.1:48650";
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

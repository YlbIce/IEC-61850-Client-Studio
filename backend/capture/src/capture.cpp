#include "capture_api.h"
#include "process.h"
#include "mms_semantics.h"
#include "packet_store.h"
#include <deque>
#include <nlohmann/json.hpp>
#include <filesystem>
#include <sstream>
#include <fstream>
#include <memory>
#include <map>
#include <algorithm>
using json = nlohmann::ordered_json;
namespace fs = std::filesystem;
struct Context {
    std::atomic<bool> cancelled{false};
    std::string response, file, filter;
    std::shared_ptr<capture::PacketStore> packets = std::make_shared<capture::PacketStore>(),
                                          allPackets = packets;
    capture::PacketStore layerStore, semanticStore;
    std::map<int, size_t> layerIndices;
    std::vector<int> replayFrames;
    struct Correlation {
        int request, response;
        double latency;
        std::string stream, invoke;
    };
    std::map<int, Correlation> correlations;
    std::map<int, json> layersCache;
    std::deque<int> cacheOrder;
    std::map<int, size_t> cacheWeights;
    size_t cacheBytes = 0;
    bool mmsCached = false;
    std::unique_ptr<Process> capture;
    std::string captureFile;
    std::vector<int> ports;
    std::wstring tshark, dumpcap;
    HANDLE signal = nullptr;
    ~Context() {
        if (signal)
            CloseHandle(signal);
        if (capture && capture->process)
            WaitForSingleObject(capture->process, 5000);
    }
};
static std::wstring tool(const wchar_t *name) {
    std::vector<fs::path> dirs;
    wchar_t env[32768];
    if (GetEnvironmentVariableW(L"IEC61850_WIRESHARK_DIR", env, 32768))
        dirs.emplace_back(env);
    dirs.emplace_back(fs::path(executableDirectory()) / L"wireshark");
    dirs.emplace_back(L"C:/Program Files/Wireshark");
    for (auto &d : dirs) {
        auto p = d / name;
        if (fs::is_regular_file(p))
            return p.wstring();
    }
    wchar_t found[32768];
    if (SearchPathW(nullptr, name, nullptr, 32768, found, nullptr))
        return found;
    return {};
}
static std::string run(Context &c, const std::vector<std::string> &args) {
    if (c.tshark.empty())
        throw std::runtime_error("TShark unavailable. Install Wireshark or set IEC61850_WIRESHARK_DIR.");
    Process p;
    p.start(c.tshark, args);
    return p.read(c.cancelled);
}
static std::vector<std::string> base(Context &c) {
    if (c.file.empty())
        throw std::runtime_error("No capture file is open");
    std::vector<std::string> args = {"-n", "-2",
                                     "-r", c.file,
                                     "-o", "tcp.desegment_tcp_streams:TRUE",
                                     "-o", "tcp.reassemble_out_of_order:TRUE"};
    for (int port : c.ports) {
        args.push_back("-d");
        args.push_back("tcp.port==" + std::to_string(port) + ",tpkt");
    }
    return args;
}
static std::vector<std::string> split(const std::string &s, char delimiter) {
    std::vector<std::string> r;
    std::istringstream in(s);
    std::string v;
    while (std::getline(in, v, delimiter))
        r.push_back(v);
    return r;
}
static double number(const std::string &s) {
    try {
        return std::stod(s);
    } catch (...) {
        return 0;
    }
}
static std::shared_ptr<capture::PacketStore> index(Context &c, const std::string &filter) {
    auto args = base(c);
    if (!filter.empty()) {
        args.push_back("-Y");
        args.push_back(filter);
    }
    args.insert(args.end(), {"-T", "fields", "-E", "separator=/t", "-E", "occurrence=f"});
    std::vector<std::string> fields = {"frame.number",   "frame.time_epoch",    "frame.time_relative",
                                       "_ws.col.Source", "_ws.col.Destination", "_ws.col.Protocol",
                                       "frame.len",      "_ws.col.Info",        "tcp.stream",
                                       "mms.invokeID"};
    for (auto &f : fields) {
        args.push_back("-e");
        args.push_back(f);
    }
    auto rows = std::make_shared<capture::PacketStore>();
    auto consumeLine = [&](const std::string &line) {
        auto v = split(line, '\t');
        v.resize(fields.size());
        if (v[0].empty())
            return;
        std::string info = v[7];
        if (!v.back().empty() && v.back().back() == '\r')
            v.back().pop_back();
        rows->append({{"frame", (int)number(v[0])},
                      {"time", number(v[1])},
                      {"relative_time", number(v[2])},
                      {"source", v[3]},
                      {"destination", v[4]},
                      {"protocol", v[5]},
                      {"length", (int)number(v[6])},
                      {"info", info},
                      {"stream", v[8]},
                      {"invoke_id", v[9]},
                      {"service", info},
                      {"direction", info.find("Request") != std::string::npos    ? "request"
                                    : info.find("Response") != std::string::npos ? "response"
                                                                                 : ""}});
    };
    if (c.tshark.empty())
        throw std::runtime_error("TShark unavailable");
    Process process;
    process.start(c.tshark, args);
    std::string buffer;
    process.stream(c.cancelled, [&](const char *bytes, size_t length) {
        buffer.append(bytes, length);
        size_t consumed = 0, end;
        while ((end = buffer.find('\n', consumed)) != std::string::npos) {
            consumeLine(buffer.substr(consumed, end - consumed));
            consumed = end + 1;
        }
        buffer.erase(0, consumed);
        if (buffer.size() > 4 * 1024 * 1024)
            throw std::runtime_error("Packet summary exceeds 4 MiB limit");
    });
    if (!buffer.empty())
        consumeLine(buffer);
    return rows;
}
static json tree(const json &value, const std::string &name) {
    json node = {{"name", name}, {"label", name}, {"children", json::array()}};
    if (value.is_structured()) {
        for (auto i = value.begin(); i != value.end(); ++i)
            node["children"].push_back(
                tree(i.value(), value.is_array() ? std::to_string(i - value.begin()) : i.key()));
    } else {
        node["value"] = value;
        node["label"] = name + ": " + (value.is_string() ? value.get<std::string>() : value.dump());
    }
    return node;
}

static std::string field(const json &node, const std::string &key) {
    if (node.is_object()) {
        if (node.contains(key) && node[key].is_string())
            return node[key].get<std::string>();
        for (auto &v : node) {
            auto result = field(v, key);
            if (!result.empty())
                return result;
        }
    } else if (node.is_array())
        for (auto &v : node) {
            auto result = field(v, key);
            if (!result.empty())
                return result;
        }
    return {};
}
static bool has(const json &node, const std::string &key) {
    if (node.is_object()) {
        if (node.contains(key))
            return true;
        for (auto &v : node)
            if (has(v, key))
                return true;
    } else if (node.is_array())
        for (auto &v : node)
            if (has(v, key))
                return true;
    return false;
}
static size_t jsonMemory(const json &value) {
    size_t bytes = sizeof(json);
    if (value.is_string())
        return bytes + value.get_ref<const json::string_t &>().capacity();
    if (value.is_structured())
        for (auto i = value.begin(); i != value.end(); ++i) {
            bytes += jsonMemory(i.value()) + sizeof(json);
            if (value.is_object())
                bytes += i.key().capacity() + sizeof(std::string);
        }
    return bytes;
}
static void rememberLayers(Context &c, int frame, const json &value) {
    constexpr size_t budget = 64 * 1024 * 1024;
    size_t bytes = jsonMemory(value);
    if (bytes > budget)
        return;
    if (c.layersCache.count(frame))
        return;
    while (!c.cacheOrder.empty() && (c.layersCache.size() >= 32 || c.cacheBytes + bytes > budget)) {
        int victim = c.cacheOrder.front();
        c.cacheOrder.pop_front();
        c.cacheBytes -= c.cacheWeights.at(victim);
        c.cacheWeights.erase(victim);
        c.layersCache.erase(victim);
    }
    c.layersCache[frame] = value;
    c.cacheOrder.push_back(frame);
    c.cacheWeights[frame] = bytes;
    c.cacheBytes += bytes;
}
static json getLayers(Context &c, int frame) {
    auto cached = c.layersCache.find(frame);
    if (cached != c.layersCache.end())
        return cached->second;
    auto value = c.layerStore.at(c.layerIndices.at(frame));
    rememberLayers(c, frame, value);
    return value;
}
static void cacheMms(Context &c) {
    if (c.mmsCached)
        return;
    c.layerStore.clear();
    c.semanticStore.clear();
    c.layerIndices.clear();
    c.replayFrames.clear();
    c.layersCache.clear();
    c.cacheOrder.clear();
    c.cacheWeights.clear();
    c.cacheBytes = 0;
    c.correlations.clear();
    auto args = base(c);
    args.insert(args.end(), {"-Y", "mms", "-T", "json", "--no-duplicate-keys", "-x"});
    std::map<std::string, int> pending;
    std::map<std::string, json> datasetStates;
    auto consume = [&](const json &packet) {
        const auto &layers = packet["_source"]["layers"];
        int frame = (int)number(field(layers, "frame.number"));
        c.layerIndices[frame] = c.layerStore.size();
        c.layerStore.append(layers);
        auto stream = field(layers, "tcp.stream"), invoke = field(layers, "mms.invokeID"),
             src = field(layers, "tcp.srcport"), dst = field(layers, "tcp.dstport");
        bool req = has(layers, "mms.confirmed_RequestPDU_element"),
             res = has(layers, "mms.confirmed_ResponsePDU_element") ||
                   has(layers, "mms.confirmed_ErrorPDU_element");
        std::string key = stream + ":" + (req ? src : dst) + ":" + invoke;
        json preceding = json::object();
        bool ambiguous = false;
        if (req && !invoke.empty()) {
            // Reuse before a response is ambiguous. Do not replace the older
            // request with a potentially unrelated operation.
            ambiguous = pending.count(key) != 0;
            pending[key] = ambiguous ? 0 : frame;
        } else if (res && !invoke.empty() && pending.count(key)) {
            int request = pending[key];
            ambiguous = request == 0;
            if (!ambiguous) {
                preceding = getLayers(c, request);
                Context::Correlation relation{request, frame,
                                              (number(field(layers, "frame.time_relative")) -
                                               number(field(preceding, "frame.time_relative"))) *
                                                  1000,
                                              stream, invoke};
                c.correlations[request] = relation;
                c.correlations[frame] = relation;
            }
            pending.erase(key);
        }
        auto &datasets = datasetStates[stream];
        if (datasets.is_null())
            datasets = json::object();
        preceding["_capture_context"]["datasets"] = datasets;
        auto semantics = capture_mms::decode(layers, preceding);
        if (ambiguous)
            semantics["warnings"].push_back(
                "An invoke ID was reused before its response; request correlation is ambiguous.");
        for (auto &update : semantics["dataset_updates"]) {
            auto action = update.value("action", std::string());
            if (action == "clear")
                datasets = json::object();
            else if (action == "invalidate")
                datasets.erase(update.at("name").get<std::string>());
            else if (action == "upsert")
                datasets[update.at("name").get<std::string>()] = update.at("members");
        }
        // Bind a learned member order to the first observed report revision.
        // A later ConfRev change must not reuse the old order until a fresh
        // directory/definition response supplies evidence again.
        if (semantics.contains("report")) {
            auto& report = semantics["report"];
            auto name = report.value("dataset", std::string());
            if (!name.empty() && report.contains("conf_rev") && datasets.contains(name) && datasets[name].is_array()) {
                auto members = datasets[name];
                datasets[name] = {{"members", members}, {"conf_rev", report["conf_rev"]}};
            }
        }
        semantics["relative_time"] = number(field(layers, "frame.time_relative"));
        for (const auto &value : semantics["values"])
            if (value.value("replayable", false)) {
                c.replayFrames.push_back(frame);
                break;
            }
        c.semanticStore.append(semantics);
    };
    Process process;
    process.start(c.tshark, args);
    std::string object;
    int depth = 0;
    bool quoted = false, escaped = false;
    process.stream(c.cancelled, [&](const char *data, size_t length) {
        for (size_t i = 0; i < length; i++) {
            char ch = data[i];
            if (!depth) {
                if (ch != '{')
                    continue;
                depth = 1;
                object = "{";
                continue;
            }
            object += ch;
            if (object.size() > 32 * 1024 * 1024)
                throw std::runtime_error("One decoded frame exceeds 32 MiB limit");
            if (quoted) {
                if (escaped)
                    escaped = false;
                else if (ch == '\\')
                    escaped = true;
                else if (ch == '"')
                    quoted = false;
                continue;
            }
            if (ch == '"')
                quoted = true;
            else if (ch == '{')
                depth++;
            else if (ch == '}')
                depth--;
            if (!depth) {
                consume(json::parse(object));
                object.clear();
            }
        }
    });
    if (depth)
        throw std::runtime_error("Incomplete protocol decoder output");
    c.mmsCached = true;
}
static json detail(Context &c, int frame) {
    if (frame <= 0)
        throw std::runtime_error("Invalid frame");
    cacheMms(c);
    json layers;
    if (c.layersCache.count(frame))
        layers = c.layersCache.at(frame);
    else if (c.layerIndices.count(frame))
        layers = getLayers(c, frame);
    else {
        auto args = base(c);
        args.insert(args.end(), {"-Y", "frame.number == " + std::to_string(frame), "-T", "json",
                                 "--no-duplicate-keys", "-x"});
        auto all = json::parse(run(c, args));
        if (all.empty())
            throw std::runtime_error("Frame not found");
        layers = all[0]["_source"]["layers"];
        rememberLayers(c, frame, layers);
    }
    std::string hex;
    if (layers.contains("frame_raw") && layers["frame_raw"].is_array() && !layers["frame_raw"].empty())
        hex = layers["frame_raw"][0].get<std::string>();
    json correlation = json::object();
    auto found = c.correlations.find(frame);
    if (found != c.correlations.end()) {
        auto &r = found->second;
        correlation = {{"request_frame", r.request},
                       {"response_frame", r.response},
                       {"latency_ms", r.latency},
                       {"stream", r.stream},
                       {"invoke_id", r.invoke}};
    }
    auto semantics = c.layerIndices.count(frame) ? c.semanticStore.at(c.layerIndices.at(frame))
                                                 : capture_mms::decode(layers, json::object());
    return {{"frame", frame},
            {"layers", layers},
            {"tree", tree(layers, "Frame " + std::to_string(frame))},
            {"hex", hex},
            {"values", semantics["values"]},
            {"warnings", semantics["warnings"]},
            {"report", semantics.value("report", json::object())},
            {"correlation", correlation}};
}
static json request(Context &c, const json &q) {
    std::string command = q.at("command");
    json a = q.value("args", json::object());
    if (command == "capabilities") {
        return {{"abi", 65536},
                {"available", true},
                {"analysis", !c.tshark.empty()},
                {"capture", !c.dumpcap.empty()},
                {"tshark", utf8(c.tshark)},
                {"dumpcap", utf8(c.dumpcap)},
                {"formats", {"pcap", "pcapng"}},
                {"replay", "offline-observed-values"},
                {"reason", c.tshark.empty()
                               ? "Wireshark/TShark 未安装；设置 IEC61850_WIRESHARK_DIR 后重新打开窗口"
                               : ""}};
    }
    if (command == "interfaces") {
        if (c.dumpcap.empty())
            throw std::runtime_error("Dumpcap unavailable");
        Process p;
        p.start(c.dumpcap, {"-D"});
        auto text = p.read(c.cancelled, 15000);
        json list = json::array();
        for (auto &line : split(text, '\n')) {
            auto dot = line.find(". ");
            if (dot == std::string::npos)
                continue;
            auto label = line.substr(dot + 2);
            if (!label.empty() && label.back() == '\r')
                label.pop_back();
            auto space = label.find(" (");
            list.push_back({{"id", label.substr(0, space)},
                            {"name", label},
                            {"description", label},
                            {"index", (int)number(line.substr(0, dot))}});
        }
        return {{"interfaces", list}};
    }
    if (command == "open") {
        std::string file = a.at("file");
        auto p = fs::path(wide(file));
        if (!fs::is_regular_file(p))
            throw std::runtime_error("Capture file does not exist");
        if (fs::file_size(p) > 2ULL * 1024 * 1024 * 1024)
            throw std::runtime_error("Capture exceeds 2 GiB limit; split with editcap");
        auto old = c.file;
        auto oldPorts = c.ports;
        auto nextPorts = a.value("mms_ports", std::vector<int>{});
        for (int port : nextPorts)
            if (port < 1 || port > 65535)
                throw std::runtime_error("Invalid MMS decode port");
        c.ports = std::move(nextPorts);
        c.file = file;
        try {
            auto rows = index(c, "");
            c.packets = std::move(rows);
            c.allPackets = c.packets;
            c.layersCache.clear();
            c.correlations.clear();
            c.layerIndices.clear();
            c.replayFrames.clear();
            c.cacheOrder.clear();
            c.cacheWeights.clear();
            c.cacheBytes = 0;
            c.layerStore.clear();
            c.semanticStore.clear();
            c.mmsCached = false;
            c.filter = "";
        } catch (...) {
            c.file = old;
            c.ports = oldPorts;
            throw;
        }
        return {{"file", file},
                {"total", c.packets->size()},
                {"duration", !c.packets->size()
                                 ? 0
                                 : c.packets->at(c.packets->size() - 1)["relative_time"].get<double>()}};
    }
    if (command == "packets") {
        auto f = a.value("filter", std::string());
        if (f != c.filter) {
            auto rows = f.empty() ? c.allPackets : index(c, f);
            c.packets = std::move(rows);
            c.filter = f;
        }
        int offset = std::max(0, a.value("offset", 0)), limit = std::clamp(a.value("limit", 200), 1, 1000);
        json rows = json::array();
        for (size_t n = offset; n < c.packets->size() && n < (size_t)offset + limit; n++)
            rows.push_back(c.packets->at(n));
        return {{"packets", rows}, {"total", c.packets->size()}, {"offset", offset}};
    }
    if (command == "detail")
        return detail(c, a.at("frame"));
    if (command == "replay") {
        cacheMms(c);
        json events = json::array(), snapshot = json::object();
        const int through = a.value("through_frame", std::numeric_limits<int>::max());
        bool snapshotMode = a.value("mode", std::string()) == "snapshot";
        size_t total =
            std::upper_bound(c.replayFrames.begin(), c.replayFrames.end(), through) - c.replayFrames.begin();
        int offset = std::max(0, a.value("offset", 0)), limit = std::clamp(a.value("limit", 200), 1, 1000);
        size_t begin = snapshotMode ? 0 : std::min(total, (size_t)offset),
               end = snapshotMode ? total : std::min(total, begin + limit);
        for (size_t n = begin; n < end; n++) {
            if (c.cancelled)
                throw std::runtime_error("Operation cancelled");
            int frame = c.replayFrames[n];
            auto semantics = c.semanticStore.at(c.layerIndices.at(frame));
            json observed = json::array();
            for (auto value : semantics["values"]) {
                if (!value.value("replayable", false))
                    continue;
                value["frame"] = frame;
                value["relative_time"] = semantics["relative_time"];
                if (snapshotMode)
                    snapshot[value["reference"].get<std::string>() + "[" + value.value("fc", std::string()) +
                             "]"] = value;
                else
                    observed.push_back(value);
            }
            if (!snapshotMode)
                events.push_back(
                    {{"frame", frame}, {"relative_time", semantics["relative_time"]}, {"values", observed}});
        }
        if (snapshotMode) {
            json values = json::array();
            for (auto &value : snapshot)
                values.push_back(value);
            return {{"through_frame", through}, {"values", values}, {"observations", total}};
        }
        return {{"events", events}, {"total", total}, {"offset", offset}};
    }
    if (command == "sessions") {
        std::map<std::string, json> sessions;
        for (size_t index = 0; index < c.allPackets->size(); index++) {
            auto p = c.allPackets->at(index);
            auto stream = p["stream"].get<std::string>();
            if (stream.empty())
                continue;
            if (!sessions.count(stream))
                sessions[stream] = {
                    {"stream", stream},     {"source", p["source"]}, {"destination", p["destination"]},
                    {"started", p["time"]}, {"packets", 0},          {"mms_packets", 0},
                    {"bytes", 0},           {"duration", 0}};
            auto &session = sessions[stream];
            session["packets"] = session["packets"].get<int>() + 1;
            session["bytes"] = session["bytes"].get<int>() + p["length"].get<int>();
            session["duration"] = p["time"].get<double>() - session["started"].get<double>();
            if (p["protocol"] == "MMS")
                session["mms_packets"] = session["mms_packets"].get<int>() + 1;
        }
        json rows = json::array();
        for (auto &pair : sessions)
            rows.push_back(pair.second);
        return {{"sessions", rows}};
    }
    if (command == "capture-start") {
        if (c.dumpcap.empty())
            throw std::runtime_error("Dumpcap unavailable");
        if (c.capture && c.capture->running())
            throw std::runtime_error("Capture already running");
        std::string iface = a.at("interface"), file = a.at("output"),
                    filter = a.value("filter", std::string());
        if (iface.empty() || file.empty())
            throw std::runtime_error("Interface and output required");
        if (fs::exists(fs::path(wide(file))))
            throw std::runtime_error("Output already exists; choose a new file");
        auto proc = std::make_unique<Process>();
        std::string signalId =
            "iec61850-" + std::to_string(GetCurrentProcessId()) + "-" + std::to_string(GetTickCount64());
        if (c.signal) {
            CloseHandle(c.signal);
            c.signal = nullptr;
        }
        auto signalName = wide("\\\\.\\pipe\\wireshark." + signalId + ".signal");
        c.signal = CreateNamedPipeW(signalName.c_str(), PIPE_ACCESS_OUTBOUND, PIPE_TYPE_BYTE | PIPE_WAIT, 1,
                                    64, 64, 0, nullptr);
        if (c.signal == INVALID_HANDLE_VALUE) {
            c.signal = nullptr;
            throw std::runtime_error("Cannot create capture stop signal");
        }
        std::vector<std::string> args = {"-i",
                                         iface,
                                         "-w",
                                         file,
                                         "-q",
                                         "-Z",
                                         "none",
                                         "--signal-pipe",
                                         signalId,
                                         "-a",
                                         "filesize:262144",
                                         "-a",
                                         "duration:3600"};
        if (!filter.empty()) {
            args.push_back("-f");
            args.push_back(filter);
        }
        proc->start(c.dumpcap, args);
        Sleep(400);
        if (!proc->running()) {
            proc->read(c.cancelled, 1000);
            throw std::runtime_error("Capture failed to start; check Npcap/interface permissions");
        }
        c.capture = std::move(proc);
        c.captureFile = file;
        return {{"capturing", true}, {"file", file}, {"max_bytes", 268435456}, {"max_seconds", 3600}};
    }
    if (command == "capture-status")
        return {{"capturing", c.capture && c.capture->running()}, {"file", c.captureFile}};
    if (command == "capture-stop") {
        if (c.signal) {
            CloseHandle(c.signal);
            c.signal = nullptr;
        }
        if (c.capture) {
            if (c.capture->process)
                WaitForSingleObject(c.capture->process, 5000);
            c.capture->stop();
            c.capture.reset();
        }
        return {{"capturing", false}, {"file", c.captureFile}};
    }
    if (command == "close") {
        if (c.signal) {
            CloseHandle(c.signal);
            c.signal = nullptr;
        }
        if (c.capture) {
            if (c.capture->process)
                WaitForSingleObject(c.capture->process, 5000);
            c.capture.reset();
        }
        c.file.clear();
        c.packets = std::make_shared<capture::PacketStore>();
        c.allPackets = c.packets;
        c.layerStore.clear();
        c.semanticStore.clear();
        c.layerIndices.clear();
        c.replayFrames.clear();
        c.layersCache.clear();
        c.correlations.clear();
        c.cacheOrder.clear();
        c.cacheWeights.clear();
        c.cacheBytes = 0;
        c.mmsCached = false;
        return {{"closed", true}};
    }
    throw std::runtime_error("Unknown capture command: " + command);
}
uint32_t capture_abi_version() {
    return 0x00010000;
}
void *capture_create() {
    try {
        auto c = new Context;
        c->tshark = tool(L"tshark.exe");
        c->dumpcap = tool(L"dumpcap.exe");
        return c;
    } catch (...) {
        return nullptr;
    }
}
const char *capture_request(void *context, const char *input) {
    if (!context || !input)
        return "{\"error\":{\"code\":\"INVALID_ARGUMENT\",\"message\":\"Context and input are required\"}}";
    auto &c = *static_cast<Context *>(context);
    c.cancelled = false;
    json reply;
    try {
        auto q = json::parse(input);
        reply["id"] = q.value("id", json());
        reply["result"] = request(c, q);
    } catch (const std::exception &e) {
        reply["error"] = {{"code", c.cancelled ? "CANCELLED" : "CAPTURE_ERROR"}, {"message", e.what()}};
    } catch (...) {
        reply["error"] = {{"code", "INTERNAL"}, {"message", "Unexpected capture library failure"}};
    }
    c.response = reply.dump(-1, ' ', false, json::error_handler_t::replace);
    return c.response.c_str();
}
void capture_cancel(void *context) {
    if (context)
        static_cast<Context *>(context)->cancelled = true;
}
void capture_destroy(void *context) {
    delete static_cast<Context *>(context);
}

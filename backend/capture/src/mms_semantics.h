#pragma once

// Semantic adapter for TShark -T json --no-duplicate-keys -x output.
// The caller supplies an association-scoped, preceding request and optional
// _capture_context.datasets. No frame-summary text is used as protocol evidence.
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

namespace capture_mms {
using json = nlohmann::ordered_json;

inline const json& emptyObject() { static const json j = json::object(); return j; }
inline const json& get(const json& j, const char* key) {
    if (!j.is_object()) return emptyObject();
    const auto it = j.find(key);
    return it == j.end() ? emptyObject() : *it;
}
inline std::string text(const json& j) {
    if (j.is_string()) return j.get<std::string>();
    if (j.is_number() || j.is_boolean()) return j.dump();
    return {};
}
inline int integer(const json& j, int fallback = -1) {
    try { const auto s = text(j); size_t used = 0; int n = std::stoi(s, &used, 0); return used == s.size() ? n : fallback; }
    catch (...) { return fallback; }
}
inline std::vector<const json*> many(const json& j) {
    std::vector<const json*> result;
    if (j.is_array()) for (const auto& v : j) result.push_back(&v);
    else if (!j.is_null() && !(j.is_object() && j.empty())) result.push_back(&j);
    return result;
}
inline const json& element(const json& j, size_t n) {
    if (j.is_array()) return n < j.size() ? j[n] : emptyObject();
    return n == 0 ? j : emptyObject();
}
inline std::string rawHex(const json& raw) {
    return raw.is_array() && !raw.empty() && raw[0].is_string() ? raw[0].get<std::string>() : std::string();
}
inline std::vector<uint8_t> bytes(const std::string& input) {
    std::vector<uint8_t> result;
    int high = -1;
    for (unsigned char c : input) {
        if (c == ':' || c == ' ') continue;
        int n = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
        if (n < 0) return {};
        if (high < 0) high = n;
        else { result.push_back(static_cast<uint8_t>((high << 4) | n)); high = -1; }
    }
    if (high >= 0) return {};
    return result;
}
inline std::string firstText(const json& tree, std::initializer_list<const char*> names) {
    for (const auto name : names) { const auto s = text(get(tree, name)); if (!s.empty()) return s; }
    return {};
}

// ASN.1 INTEGER can exceed both JavaScript's exact integer range and uint64_t.
inline std::string decimal(std::vector<uint8_t> data, bool isSigned) {
    if (data.empty()) return {};
    const bool negative = isSigned && (data[0] & 0x80);
    if (negative) {
        unsigned carry = 1;
        for (auto i = data.rbegin(); i != data.rend(); ++i) { unsigned v = static_cast<uint8_t>(~*i) + carry; *i = static_cast<uint8_t>(v); carry = v >> 8; }
    }
    std::string result = "0";
    for (auto b : data) {
        unsigned carry = b;
        for (auto i = result.rbegin(); i != result.rend(); ++i) { unsigned v = static_cast<unsigned>(*i - '0') * 256 + carry; *i = static_cast<char>('0' + v % 10); carry = v / 10; }
        while (carry) { result.insert(result.begin(), static_cast<char>('0' + carry % 10)); carry /= 10; }
    }
    return negative && result != "0" ? "-" + result : result;
}

inline json typed(int tag, const json& tree, const json& raw = json(), unsigned depth = 0);
inline json sequence(const json& node, const char* field, unsigned depth = 0) {
    const auto tags = many(get(node, field));
    const auto& trees = get(node, (std::string(field) + "_tree").c_str());
    const auto& raws = get(node, (std::string(field) + "_raw").c_str());
    json result = json::array();
    for (size_t n = 0; n < tags.size() && n < 65536; ++n) {
        // A single raw field is [hex,offset,length,...], not an array of fields.
        const auto& r = tags.size() == 1 ? raws : element(raws, n);
        result.push_back(typed(integer(*tags[n]), element(trees, n), r, depth + 1));
    }
    return result;
}
inline json typed(int tag, const json& tree, const json& raw, unsigned depth) {
    json value = {{"type", "unsupported"}, {"value", nullptr}, {"complete", false}};
    const auto encoded = rawHex(raw);
    if (!encoded.empty()) value["raw"] = encoded;
    if (depth > 64) { value["error"] = "Nesting limit exceeded"; return value; }
    if (tag == 1 || tag == 2) {
        const char* key = tag == 1 ? "mms.array_tree" : "mms.structure_tree";
        const char* countKey = tag == 1 ? "mms.array" : "mms.structure";
        value["type"] = tag == 1 ? "array" : "structure";
        value["value"] = sequence(get(tree, key), "mms.Data", depth);
        bool complete = integer(get(tree, countKey)) == static_cast<int>(value["value"].size());
        for (const auto& child : value["value"]) if (!child.value("complete", false)) complete = false;
        value["complete"] = complete;
        return value;
    }
    if (tag == 3) {
        value["type"] = "boolean";
        auto s = firstText(tree, {"mms.boolean", "mms.iec61850.bufovfl"});
        auto data = bytes(encoded);
        if (s == "0" || s == "false") value["value"] = false;
        else if (s == "1" || s == "true") value["value"] = true;
        else if (data.size() == 1) value["value"] = data[0] != 0;
    } else if (tag == 5 || tag == 6) {
        value["type"] = tag == 5 ? "integer" : "unsigned";
        auto data = bytes(encoded);
        std::string s = data.empty() ? firstText(tree, {tag == 5 ? "mms.integer" : "mms.unsigned", "mms.iec61850.seqnum", "mms.iec61850.confrev"}) : decimal(data, tag == 5);
        if (!s.empty() && s.find_first_not_of("-0123456789") == std::string::npos) value["value"] = s;
    } else if (tag == 7) {
        value["type"] = "floating-point";
        auto data = bytes(firstText(tree, {"mms.floating_point"}));
        if (data.empty()) data = bytes(encoded);
        double decoded = 0;
        bool valid = false;
        if (data.size() == 5 && data[0] == 8) {
            uint32_t bits = 0; for (size_t n = 1; n < data.size(); ++n) bits = (bits << 8) | data[n];
            float f; std::memcpy(&f, &bits, sizeof(f)); decoded = f; valid = true;
        } else if (data.size() == 9 && data[0] == 11) {
            uint64_t bits = 0; for (size_t n = 1; n < data.size(); ++n) bits = (bits << 8) | data[n];
            std::memcpy(&decoded, &bits, sizeof(decoded)); valid = true;
        }
        if (valid) value["value"] = std::isfinite(decoded) ? json(decoded) : json(std::isnan(decoded) ? "NaN" : decoded > 0 ? "+Infinity" : "-Infinity");
    } else if (tag == 4 || tag == 14) {
        value["type"] = "bit-string";
        std::string s = firstText(tree, {"mms.data_bit-string", "mms.booleanArray", "mms.iec61850.reported_optfld", "mms.iec61850.inclusion_bitstring"});
        int padding = integer(get(tree, "ber.bitstring.padding"));
        auto data = bytes(s);
        if (data.empty() && s.empty()) {
            data = bytes(encoded);
            if (!data.empty()) { padding = data.front(); data.erase(data.begin()); }
        }
        if (padding >= 0 && padding <= 7 && (padding == 0 || !data.empty())) {
            std::string bits;
            for (auto b : data) for (int bit = 7; bit >= 0; --bit) bits.push_back((b & (1 << bit)) ? '1' : '0');
            if (bits.size() >= static_cast<size_t>(padding)) {
                bits.resize(bits.size() - padding); value["value"] = bits; value["bit_length"] = bits.size();
            }
        }
    } else if (tag == 9) {
        value["type"] = "octet-string";
        const auto s = firstText(tree, {"mms.data.octet-string"});
        if (!s.empty() || !encoded.empty()) value["value"] = s.empty() ? encoded : s;
    } else if (tag == 10 || tag == 16) {
        value["type"] = tag == 10 ? "visible-string" : "mms-string";
        for (const auto* name : {"mms.data.visible-string", "mms.mMSString", "mms.iec61850.rptid", "mms.iec61850.datset"}) {
            const auto& candidate = get(tree, name);
            if (candidate.is_string()) { value["value"] = candidate; break; }
        }
        if (value["value"].is_null() && !encoded.empty()) {
            auto data = bytes(encoded); value["value"] = std::string(data.begin(), data.end());
        }
    } else if (tag == 12 || tag == 17) {
        value["type"] = tag == 17 ? "utc-time" : "binary-time";
        auto s = firstText(tree, {"mms.utc_time", "mms.data.binary-time", "mms.iec61850.timeofentry"});
        if (!s.empty()) value["value"] = s;
        auto data = bytes(encoded);
        if (tag == 17 && data.size() == 8) {
            uint64_t seconds = 0; for (size_t n = 0; n < 4; ++n) seconds = (seconds << 8) | data[n];
            uint64_t fraction = (static_cast<uint32_t>(data[4]) << 16) | (static_cast<uint32_t>(data[5]) << 8) | data[6];
            value["epoch_ns"] = std::to_string(seconds * 1000000000ULL + (fraction * 1000000000ULL) / 16777216ULL);
            value["time_quality"] = {{"raw", data[7]}, {"leap_second_known", (data[7] & 0x80) != 0}, {"clock_failure", (data[7] & 0x40) != 0}, {"not_synchronized", (data[7] & 0x20) != 0}, {"accuracy", data[7] & 0x1f}};
            if (value["value"].is_null()) value["value"] = value["epoch_ns"];
        }
    }
    value["complete"] = !value["value"].is_null();
    return value;
}

inline const json& find(const json& node, const std::string& key, unsigned depth = 0) {
    if (depth > 64) return emptyObject();
    if (node.is_object()) {
        auto it = node.find(key); if (it != node.end()) return *it;
        for (auto i = node.begin(); i != node.end(); ++i) {
            if (i.key().size() >= 4 && i.key().compare(i.key().size() - 4, 4, "_raw") == 0) continue;
            const auto& found = find(*i, key, depth + 1); if (&found != &emptyObject()) return found;
        }
    } else if (node.is_array()) for (const auto& item : node) { const auto& found = find(item, key, depth + 1); if (&found != &emptyObject()) return found; }
    return emptyObject();
}
inline std::string objectName(const json& tree) {
    const auto& domain = find(tree, "mms.domain_specific_element");
    const auto d = text(get(domain, "mms.domainId"));
    const auto i = text(get(domain, "mms.objectName_domain_specific_itemId"));
    return !d.empty() && !i.empty() ? d + "/" + i : std::string();
}
inline json reference(const std::string& raw, const std::string& origin) {
    json r = {{"reference", ""}, {"original_reference", raw}, {"fc", ""}, {"mapping", "unresolved"}, {"mapping_source", origin}};
    const auto slash = raw.find('/'), first = raw.find('$', slash == std::string::npos ? 0 : slash + 1);
    const auto second = first == std::string::npos ? first : raw.find('$', first + 1);
    if (slash == std::string::npos || first == std::string::npos || second == std::string::npos) return r;
    const auto fc = raw.substr(first + 1, second - first - 1);
    const std::string known = "|ST|MX|SP|SV|CF|DC|SG|SE|SR|OR|BL|EX|CO|RP|BR|LG|GO|GS|MS|US|";
    if (known.find("|" + fc + "|") == std::string::npos) return r;
    std::string canonical = raw.substr(0, first) + "." + raw.substr(second + 1);
    std::replace(canonical.begin(), canonical.end(), '$', '.');
    r["reference"] = canonical; r["fc"] = fc; r["mapping"] = "resolved";
    return r;
}
inline json dataSetMembers(const json& context, const std::string& name, const std::string& confRev = {}) {
    const auto& all = get(get(context, "_capture_context"), "datasets");
    if (!all.is_object() || !all.contains(name)) return json::array();
    const auto& entry = all[name];
    if (entry.is_array()) return entry;
    if (!confRev.empty() && entry.contains("conf_rev") && text(entry["conf_rev"]) != confRev) return json::array();
    const auto& members = get(entry, "members"); return members.is_array() ? members : json::array();
}
inline json references(const json& service, const json& context) {
    json result = json::array();
    const auto& variables = find(service, "mms.listOfVariable_tree");
    if (!variables.empty()) {
        for (const auto* item : many(get(variables, "mms.listOfVariable_item_element"))) {
            const auto& alternate = find(*item, "mms.alternateAccess");
            result.push_back(reference(alternate.empty() ? objectName(*item) : "", "request-variable-list"));
        }
    } else {
        const auto name = objectName(find(service, "mms.variableListName_tree"));
        for (const auto& member : dataSetMembers(context, name)) result.push_back(reference(text(member), "dataset-definition"));
    }
    return result;
}
inline json accessResults(const json& service) {
    json results = json::array();
    const auto& list = find(service, "mms.listOfAccessResult_tree");
    for (const auto* item : many(get(list, "mms.AccessResult_tree"))) {
        if (item->contains("mms.failure")) results.push_back({{"type", "access-error"}, {"value", nullptr}, {"complete", false}, {"error", text((*item)["mms.failure"])}});
        else results.push_back(typed(integer(get(*item, "mms.success")), get(*item, "mms.success_tree"), get(*item, "mms.success_raw")));
    }
    return results;
}
inline json event(json ref, const json& value, const char* source) {
    for (auto it = value.begin(); it != value.end(); ++it) ref[it.key()] = it.value();
    ref["source"] = source;
    ref["replayable"] = ref.value("mapping", "unresolved") == "resolved" && value.value("complete", false) && std::string(source) != "write-intent";
    ref["injectable"] = false;
    return ref;
}
inline void warn(json& output, const std::string& message) { output["warnings"].push_back(message); }

// Updates are evidence-based deltas. The caller applies them in frame order to
// the matching association only, never to an application-global dataset map.
inline void datasetUpdates(const json& response, const json& requestLayers, json& output) {
    const auto& req = find(requestLayers, "mms.confirmed_RequestPDU_element");
    if (!req.is_object() || req.empty() || text(get(req, "mms.invokeID")) != text(get(response, "mms.invokeID"))) return;
    const int operation = integer(get(response, "mms.confirmedServiceResponse"));
    if (integer(get(req, "mms.confirmedServiceRequest")) != operation) return;
    const auto& requestTree = get(req, "mms.confirmedServiceRequest_tree");
    const auto& responseTree = get(response, "mms.confirmedServiceResponse_tree");
    if (operation == 11 || operation == 12) {
        const auto& definition = get(requestTree, "mms.defineNamedVariableList_element");
        const auto& attributes = get(responseTree, "mms.getNamedVariableListAttributes_element");
        if (operation == 11 && !responseTree.contains("mms.defineNamedVariableList_element")) return;
        if (operation == 12 && !attributes.is_object()) return;
        const auto name = operation == 11 ? objectName(get(definition, "mms.variableListName_tree")) : objectName(get(requestTree, "mms.getNamedVariableListAttributes_tree"));
        if (name.empty()) { warn(output, "Dataset scope is not a resolved domain-specific name; no mapping was cached."); return; }
        const auto& source = operation == 11 ? definition : attributes;
        const auto refs = references(source, emptyObject());
        const auto declared = integer(get(source, "mms.listOfVariable"));
        json members = json::array();
        bool complete = declared >= 0 && declared == static_cast<int>(refs.size());
        for (const auto& ref : refs) {
            if (ref.value("mapping", "unresolved") != "resolved") complete = false;
            members.push_back(ref["original_reference"]);
        }
        if (complete) output["dataset_updates"].push_back({{"action", "upsert"}, {"name", name}, {"members", members}, {"source", operation == 11 ? "define-success" : "directory-response"}});
        else {
            output["dataset_updates"].push_back({{"action", "invalidate"}, {"name", name}, {"source", "unsupported-member-definition"}});
            warn(output, "Dataset contains unresolved or unsupported members; its previous mapping is invalidated.");
        }
    } else if (operation == 13) {
        const auto& deletion = get(requestTree, "mms.deleteNamedVariableList_element");
        const auto& result = get(responseTree, "mms.deleteNamedVariableList_element");
        const int deleted = integer(get(result, "mms.numberDeleted"));
        if (deleted <= 0) return;
        const int scope = integer(get(deletion, "mms.scopeOfDelete"), 0);
        if (scope == 0) {
            const auto& names = get(get(deletion, "mms.listOfVariableListName_tree"), "mms.ObjectName_tree");
            for (const auto* candidate : many(names)) {
                const auto name = objectName(*candidate);
                if (!name.empty()) output["dataset_updates"].push_back({{"action", "invalidate"}, {"name", name}, {"source", "delete-response"}});
            }
            // A partial delete does not identify precisely which entries were
            // deleted. Invalidating every requested name is conservative.
        } else output["dataset_updates"].push_back({{"action", "clear"}, {"source", "scoped-delete-response"}});
    }
}

inline bool bit(const json& bits, size_t n) {
    if (bits.value("type", "") != "bit-string" || !bits.value("complete", false)) return false;
    const auto s = text(get(bits, "value")); return n < s.size() && s[n] == '1';
}

inline void report(const json& service, const json& context, json& output) {
    const auto scope = text(find(service, "mms.vmd_specific"));
    if (scope != "RPT") { warn(output, "InformationReport is not the IEC 61850 RPT variable list; no report layout was assumed."); return; }
    auto values = accessResults(service);
    if (values.size() < 3 || values[0].value("type", "") != "visible-string" || values[1].value("type", "") != "bit-string" || !values[0].value("complete", false) || text(values[1]["value"]).size() < 10) {
        warn(output, "Incomplete or malformed IEC 61850 report header."); return;
    }
    const auto& options = values[1];
    json metadata = {{"rpt_id", values[0]["value"]}, {"options", options["value"]}};
    size_t index = 2;
    auto header = [&](size_t flag, const char* name, const char* type) {
        if (!bit(options, flag)) return true;
        if (index >= values.size() || values[index].value("type", "") != type || !values[index].value("complete", false)) return false;
        metadata[name] = values[index++]["value"]; return true;
    };
    if (!header(1, "sequence", "unsigned") || !header(2, "report_time", "binary-time") || !header(4, "dataset", "visible-string") || !header(6, "buffer_overflow", "boolean") || !header(7, "entry_id", "octet-string") || !header(8, "conf_rev", "unsigned")) {
        warn(output, "Report optional fields have an invalid type or are missing."); return;
    }
    if (bit(options, 9)) {
        if (index + 1 >= values.size() || values[index].value("type", "") != "unsigned" || values[index + 1].value("type", "") != "boolean") { warn(output, "Malformed report segmentation fields."); return; }
        metadata["sub_sequence"] = values[index++]["value"]; metadata["more_segments_follow"] = values[index++]["value"];
        output["report"] = metadata;
        warn(output, "Segmented report requires association-scoped assembly; this segment is excluded from replay.");
        return;
    }
    if (index >= values.size() || values[index].value("type", "") != "bit-string" || !values[index].value("complete", false)) { warn(output, "Report inclusion bitstring is missing."); return; }
    const auto inclusion = text(values[index++]["value"]);
    metadata["inclusion"] = inclusion;
    std::vector<size_t> included;
    for (size_t n = 0; n < inclusion.size(); ++n) if (inclusion[n] == '1') included.push_back(n);
    const size_t count = included.size();
    const size_t required = count * (1 + (bit(options, 5) ? 1 : 0) + (bit(options, 3) ? 1 : 0));
    if (values.size() - index != required) { warn(output, "Report member count does not match inclusion/options; no values were mapped."); output["report"] = metadata; return; }
    json refs = json::array();
    const auto dataset = text(get(metadata, "dataset"));
    const auto members = dataSetMembers(context, dataset, text(get(metadata, "conf_rev")));
    for (size_t n = 0; n < count; ++n) {
        if (bit(options, 5)) {
            if (values[index].value("type", "") != "visible-string") { warn(output, "Report DataRef is not a visible-string."); return; }
            refs.push_back(reference(text(values[index++]["value"]), "report-data-reference"));
        } else refs.push_back(reference(members.size() == inclusion.size() ? text(members[included[n]]) : "", "dataset-definition"));
    }
    json pending = json::array();
    for (size_t n = 0; n < count; ++n) {
        auto e = event(refs[n], values[index++], "report"); e["dataset_index"] = included[n]; e["dataset"] = dataset; e["rpt_id"] = metadata["rpt_id"];
        if (e["mapping"] == "unresolved") e["label"] = (dataset.empty() ? "dataset" : dataset) + "/member[" + std::to_string(included[n]) + "]";
        if (metadata.contains("report_time")) e["report_time"] = metadata["report_time"];
        pending.push_back(std::move(e));
    }
    if (bit(options, 3)) for (auto& e : pending) {
        if (values[index].value("type", "") != "bit-string") { warn(output, "Report reason-for-inclusion has an invalid type."); return; }
        e["reason_bits"] = values[index++]["value"];
    }
    for (auto& e : pending) output["values"].push_back(std::move(e));
    output["report"] = metadata;
    if (!bit(options, 5) && members.size() != inclusion.size()) warn(output, "Report dataset definition is unavailable or incompatible; member references are unresolved and excluded from replay.");
}

inline json decode(const json& layers, const json& requestLayers = json::object()) {
    json output = {{"values", json::array()}, {"warnings", json::array()}, {"dataset_updates", json::array()}};
    const auto& frame = get(layers, "frame");
    const auto captured = text(get(frame, "frame.cap_len")), original = text(get(frame, "frame.len"));
    if (!captured.empty() && !original.empty() && integer(get(frame, "frame.cap_len"), 0) < integer(get(frame, "frame.len"), 0)) {
        warn(output, "Captured frame is truncated; semantic values are excluded from replay."); return output;
    }
    const auto& mmss = get(layers, "mms");
    for (const auto* mms : many(mmss)) {
        const auto& response = get(*mms, "mms.confirmed_ResponsePDU_element");
        if (!response.empty()) {
            datasetUpdates(response, requestLayers, output);
            const auto& read = get(get(response, "mms.confirmedServiceResponse_tree"), "mms.read_element");
            if (!read.empty()) {
                const auto values = accessResults(read);
                auto refs = references(read, requestLayers);
                if (refs.empty()) {
                    const auto& req = find(requestLayers, "mms.confirmed_RequestPDU_element");
                    if (!req.empty() && text(get(req, "mms.invokeID")) == text(get(response, "mms.invokeID")) && integer(get(req, "mms.confirmedServiceRequest")) == 4)
                        refs = references(get(get(req, "mms.confirmedServiceRequest_tree"), "mms.read_element"), requestLayers);
                }
                if (refs.size() != values.size()) warn(output, "Read references are unavailable or count-mismatched; unresolved results are excluded from replay.");
                for (size_t n = 0; n < values.size(); ++n) {
                    auto ref = refs.size() == values.size() ? refs[n] : reference("", "request-unavailable");
                    auto e = event(ref, values[n], "read-response"); e["result_index"] = n;
                    if (e["mapping"] == "unresolved") e["label"] = "Read result[" + std::to_string(n) + "]";
                    output["values"].push_back(std::move(e));
                }
            }
        }
        const auto& request = get(*mms, "mms.confirmed_RequestPDU_element");
        const auto& write = get(get(request, "mms.confirmedServiceRequest_tree"), "mms.write_element");
        if (!write.empty()) {
            auto refs = references(write, requestLayers);
            auto values = sequence(get(write, "mms.listOfData_tree"), "mms.Data");
            for (size_t n = 0; n < values.size(); ++n) output["values"].push_back(event(refs.size() == values.size() ? refs[n] : reference("", "request-unavailable"), values[n], "write-intent"));
            warn(output, "Write values are requests only; they do not represent an observed device state and are excluded from replay.");
        }
        const auto& unconfirmed = get(*mms, "mms.unconfirmed_PDU_element");
        const auto& rpt = get(get(unconfirmed, "mms.unconfirmedService_tree"), "mms.informationReport_element");
        if (!rpt.empty()) report(rpt, requestLayers, output);
    }
    if (output["values"].empty() && get(layers, "mms").empty() && !get(layers, "tls").empty()) warn(output, "TLS payload is not decoded as MMS; compatible session secrets are required.");
    return output;
}
} // namespace capture_mms

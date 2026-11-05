# Optional IEC 61850 capture engine

The main MMS service does not link or load this library. Electron starts
`capture-helper.exe` only for the independent analysis window. The helper loads
`iec61850-capture.dll` by absolute path and checks ABI major version 1. Missing
DLLs or dependencies return a capability result; server operation is unaffected.

## Build and deploy

```
node scripts/build-capture.js
node scripts/build-capture.js --test-client
node scripts/test-capture-backend.js
```

The second form adds an optional loopback test client, linking the separately
built main backend's test libraries. Production DLL/helper targets have no such
dependency. Copy `backend/capture/bin/capture-helper.exe`,
`iec61850-capture.dll` and `wireshark/**` to the application's
`resources/capture/`. Do not ship `capture-mms-client.exe` or test output.
Both adapter binaries use the static Microsoft C++ runtime.

Tool discovery order: `IEC61850_WIRESHARK_DIR`, adjacent `wireshark` directory,
`C:\Program Files\Wireshark`, then PATH. Npcap is a separate system capture
driver; offline import does not require it. No drivers or services are installed
or reconfigured by this module.

For a fresh checkout, obtain the Windows x64 PortableApps package from the
[official Wireshark download page](https://www.wireshark.org/download.html).
Verify its Authenticode signature and the SHA-256 for the version recorded below.
Extract the signed archive into a staging directory with 7-Zip; do not run a
system installer. The portable archive's tool directory is `App/Wireshark`.
Build the CLI runtime with:

```
python backend/capture/prepare-runtime.py "<extracted directory>/App/Wireshark" "backend/capture/bin/wireshark"
```

The destination **must not already exist**. For an upgrade, stage a new directory
and validate it before replacing the previous runtime. The preparation script
does not download, overwrite or delete an existing installation. Run the native
capture integration test after preparation, then run the project's release
validation before packaging. Merely building the DLL/helper does not supply the
external dissector runtime.

## Versioned C ABI

See `include/capture_api.h`. `capture_abi_version()` returns `0x00010000`.
`capture_create` owns a context. `capture_request` accepts UTF-8 JSON and returns
context-owned UTF-8 JSON, valid until the next request or destruction. One
request runs per context. `capture_cancel` is thread-safe and interrupts a
running operation. Never free returned strings in another CRT. Destroy only
after the request finishes. Exceptions are translated at the ABI boundary.

## Helper JSONL protocol

Input is `{ "id": 1, "command": "open", "args": {...} }`. Output is one
`{ "id": 1, "result": {...} }` or
`{ "id": 1, "error": { "code": "CAPTURE_ERROR", "message": "..." } }`.
Only stdout carries protocol messages. Requests are serialized in a bounded
queue; `cancel` is handled immediately by the input thread. Closing stdin
cancels work and releases the DLL and its process tree.

| Command | Arguments | Result |
|---|---|---|
| capabilities | none | available, analysis, capture, ABI, tool paths, formats, reason |
| interfaces | none | interfaces: id, name, description, index |
| open | file, optional mms_ports array | file, total packet count, duration |
| packets | offset, limit (1–1000), filter | packets, total, offset |
| detail | frame | layers, tree, hex, values, warnings, report, correlation |
| sessions | none | TCP streams, endpoints, packets, MMS packets, bytes, duration |
| replay | offset, limit, optional through_frame | paged observed events, total, offset |
| replay | mode: snapshot, through_frame | cumulative observed values, observations |
| capture-start | interface, output, optional filter | capturing, file, size/time limits |
| capture-status | none | capturing, file |
| capture-stop | none | capturing: false, file |
| close | none | closed: true |
| cancel | none | cancelled: true |

`packets.filter` is a **Wireshark display filter**. `capture-start.filter` is a
**libpcap BPF capture filter**. Arguments are passed directly through
`CreateProcessW`, never interpolated into a shell. Capture output must be a new
file. Capture stops automatically after 256 MiB or one hour. To inspect a live
capture, stop it and open its resulting PCAPNG. `mms_ports` forces TPKT decoding
for nonstandard plaintext MMS TCP ports; port 102 works automatically.

## Protocol and replay semantics

TShark performs TCP sequence/out-of-order reassembly, TPKT/COTP, presentation,
ASN.1 BER and MMS dissection. Offline analysis uses two-pass file decoding.
The full nested decoder tree and original frame bytes are retained. Flattened
summary columns are for navigation, not semantic evidence. Duplicate protocol
fields are retained using `--no-duplicate-keys`.

Requests and responses correlate by TCP stream, requester port and invoke ID.
Completed requests are retired so invoke ID reuse does not match an earlier
transaction. Only successful read responses and complete reports produce
observed values. Write requests remain visible as `write-intent` and are never
replayed as device state. Typed compound values retain their nesting; integer
precision, raw quality bits and IEC timestamps are preserved.

Report OptFlds, optional headers, inclusion mask, data-reference block, values
and reason block are decoded in wire order. A captured, successful dataset
directory/definition response teaches the member order **for that association
from that frame onward**. Delete responses invalidate the mapping. Unknown
members, conflicting counts, incomplete segments, failed access results or
truncation remain unresolved and are excluded from replay. No current project
model is silently substituted for missing capture evidence. TLS ciphertext
remains encrypted when decryption secrets are unavailable.

`values[].replayable` is the authoritative eligibility flag. Offline replay
reconstructs the most recent observed values up to a selected frame; seeking
backward removes observations that had not happened yet. `injectable` is false:
this engine never resends captured Ethernet/TCP frames or controls a device.
The GUI must keep offline observation and any explicitly reviewed application
value update separate.

## Resource and lifecycle boundaries

Packet metadata and decoded protocol/semantic records are temporary disk JSONL
stores with 64-bit offsets. Only offsets, association mappings, pending request
metadata and up to 32 decoded frames (64 MiB estimated allocation budget)
remain cached in memory. TShark output is
consumed incrementally, one JSON frame at a time, instead of retaining the whole
decoded capture. The MMS pass runs once per open file; paged replay reads the
semantic index and never launches a decoder per frame.

Stores enforce per-record (32 MiB) and per-store (8 GiB) limits and use
delete-on-close handles, including abnormal helper termination. Import currently
accepts captures up to 2 GiB. A decoder operation has a five-minute timeout and
can be cancelled. Tool processes run hidden in a kill-on-close Windows Job.
Capture stop uses Wireshark's Windows signal-pipe protocol so Dumpcap flushes
tail packets and PCAPNG statistics; a bounded shutdown timeout provides cleanup
if the tool hangs. No production interface is selected by automated tests.

## Runtime provenance

The bundled CLI runtime is extracted without installation from the signed
Wireshark Foundation PortableApps **4.6.8** distribution:

- [Official download](https://www.wireshark.org/download.html)
- [4.6.8 corresponding source](https://gitlab.com/wireshark/wireshark/-/tree/v4.6.8)
- [TShark manual](https://www.wireshark.org/docs/man-pages/tshark.html)
- [Dumpcap manual](https://www.wireshark.org/docs/man-pages/dumpcap.html)

Portable installer SHA-256:
`d6cf5056586a0156c8634a9d9b3097e60a8dda408b889c9afc4445ec2b18d581`.
Authenticode validation returned **Valid**, signer **Wireshark Foundation**.
`prepare-runtime.py` derives the PE import dependency closure of TShark, Dumpcap,
Editcap, Mergecap and Capinfos and retains required protocol configuration and
upstream licenses. It omits Qt GUI, multimedia codecs and unrelated Falco
plugins; IEC 61850 dissectors are built into libwireshark. The tested CLI runtime
contains 44 EXE/DLL files and occupies approximately **138.8 MiB**. See its
`COPYING.txt` and `studio-runtime.json`; upstream executable/DLL bytes are
unmodified.

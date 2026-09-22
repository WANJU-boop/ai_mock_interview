# AI Mock Interview — C++ / Qt

A desktop interview practice project built with C++17 and Qt. It brings together
question generation, answer evaluation, follow-up questions and local interview
reports, with adapters for HTTP LLMs and realtime voice services.

**Try it without an API key:** the default configuration runs an offline Mock
interview. The Qt walkthrough uses scripted answers; the CLI lets you type your own.

[中文说明](README.zh-CN.md) · [Build & first run](docs/building.md) ·
[30-second walkthrough](docs/assets/mock-walkthrough.mp4) ·
[CI runs](https://github.com/WANJU-boop/ai_mock_interview/actions/workflows/ci.yml)

## See it running

Actual macOS screenshots using a fictional candidate and the default Mock rules.
The application UI is currently in Chinese.

![Qt desktop interface, ready to start an offline interview](docs/assets/qt-main.png)

![Completed interview report with three answers, scores and feedback](docs/assets/qt-report.png)

The [30-second illustrated walkthrough](docs/assets/mock-walkthrough.mp4) shows
the real UI before an interview, the completed conversation, and the report viewer.
It is assembled from application screenshots with English captions. The displayed
95/100 scores come from deterministic Mock rules, not a measured AI accuracy result.

## What works

| Capability | Current behavior |
| --- | --- |
| Offline demonstration | Three scripted answers, scoring and JSON export; no key or microphone |
| Interactive CLI | Type answers, respond to follow-ups and receive a report |
| Qt desktop | Start/stop, conversation display, readable report preview and report folder access |
| LLM integration | OpenAI-compatible HTTPS adapter for questions and scoring |
| Realtime voice | Volcengine WebSocket adapter with PortAudio capture/playback; credentials required |
| Resume context | Optional PoDoFo PDF text extraction |
| Tests | Offline domain, protocol, service-adapter and Qt tests; an application smoke check in CI |

This is a learning project focused on C++ engineering. Mock feedback is rule-based.
Real-service availability depends on the configured provider, credentials and
account balance. The screenshots demonstrate the offline path.

## Quick start

**First build? Start with [the complete setup guide](docs/building.md)** for compiler,
platform libraries, vcpkg bootstrap and `VCPKG_ROOT` setup. Once those are ready:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake"
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure
```

Run the automatic offline demo on macOS or Ubuntu:

```sh
./build/AI_mock_interview_realtime_demo config.example.json
```

Or launch the desktop on macOS:

```sh
./build/AI_mock_interview_qt.app/Contents/MacOS/AI_mock_interview_qt config.example.json
```

On Ubuntu, launch `./build/AI_mock_interview_qt config.example.json` in a graphical
session. Click **开始面试** (Start interview), then **查看报告** (View report).
Reports are saved under `reports/` relative to the launch directory.

To answer questions yourself, run `./build/AI_mock_interview config.example.json`.
The current Qt Mock mode plays preset answers automatically; it does not provide
an answer text box or offline speech recognition.

## Engineering focus

```text
Qt MainWindow / CLI
        │
        ▼
Interview setup → manager / dialog orchestrator → JSON report
        │
        ▼
Service interfaces → Mock or HTTP / PDF / WebSocket / audio adapters
```

- Service interfaces separate application logic from external providers and enable
  deterministic tests without network access or audio hardware.
- The Qt main thread owns widgets. A worker thread owns the blocking interview
  session; queued signals carry progress back to the UI.
- Cancellation requests are handled at worker cleanup points. Resources have
  explicit ownership and shutdown paths.
- Reports retain primary answers, optional follow-ups, scores and feedback.
  File writes use a temporary file and rename; existing reports are not overwritten.

## Documentation

- [Build, first run and troubleshooting](docs/building.md)
- [Architecture and module notes (Chinese)](docs/code_explanations/README.md)
- [Learning roadmap (Chinese)](docs/rebuild_learning_path.md)
- [Development and commit conventions (Chinese)](docs/development.md)
- [Demo provenance and reproduction](docs/demo.md)
- [Development records (Chinese)](development_records/)

Real credentials belong in the Git-ignored `config.local.json` or environment
variables. Never include them, real resumes or candidate answers in commits or
public screenshots. CI uses only `config.example.json` and offline tests.

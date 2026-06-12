# Project Map

## Target Product

The project is a C++ desktop AI mock interview system. It lets a candidate start an interview, optionally load a PDF resume, generate C++ interview questions with an LLM, conduct a voice dialogue through a realtime speech service, score answers, and save a report.

## Module Boundaries

- `common`: configuration, logging, protocol structures, interview state machine, small utilities.
- `services`: adapters for external capabilities such as LLM HTTP API, PDF parsing, audio devices, and realtime WebSocket service.
- `interview`: domain workflow. It owns questions, answers, scoring, follow-up logic, summary generation, and dialogue orchestration.
- `ui`: Qt widgets, user input, progress display, and main-thread UI updates.

## Dependency Direction

Prefer this direction:

`ui -> interview -> services -> common`

`common` should not depend on `ui`, `interview`, or concrete service implementations.

## Runtime Data Flow

1. App starts and loads config.
2. User creates a session with candidate name, optional resume path, and question count.
3. `InterviewSession` generates default questions or resume-based questions.
4. `DialogSession` starts audio and realtime client.
5. Realtime service emits TTS, ASR, and speaking-state events.
6. State machine updates UI.
7. Candidate answers are recorded and scored.
8. The session saves a report and optional summary.

## Safer Rebuild Shape

The reference project currently builds the Qt GUI entry point. For a beginner rebuild, it is still safer to start with CLI/domain slices and return to Qt after the core logic is testable. Avoid starting with the full realtime audio stack. Build in this order:

1. Pure domain model with hard-coded questions.
2. Config and logging.
3. Mock LLM client.
4. Real LLM client.
5. Mock PDF parser, then real PDF parser.
6. Protocol parser tests.
7. Mock realtime client.
8. PortAudio integration.
9. Qt UI.
10. Real WebSocket integration.

## Integration Boundaries

Create interfaces before concrete integrations:

- `ILlmClient`
- `IPdfParser`
- `IAudioDevice`
- `IRealtimeClient`

This lets unit tests run without API keys, microphone permission, speakers, or network.

## Source-Specific Hazards

- Do not treat a legacy CLI entry point as the current product entry unless the build files support it.
- Keep headers self-contained; include every standard library and Qt type used by the header.
- Do not log complete LLM request bodies, resume text, full candidate answers, API keys, or realtime auth headers.
- Keep TLS certificate verification enabled for HTTPS and WSS.
- Avoid detached threads that capture raw `this`.
- Define one owner for WebSocket read/write/close operations.

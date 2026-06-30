# Project Map

## Target Product

The project is a C++ desktop AI mock interview system. It lets a candidate start an interview, optionally load a PDF resume, generate C++ interview questions with an LLM, conduct a voice dialogue through a realtime speech service, score answers, and save a report.

The final learning target is not just "call an LLM once." The target experience is a realtime AI interviewer, but the first half-month pass only needs to build and understand the full skeleton:

1. The app opens an interview session with role, candidate, resume/context, and question plan.
2. The interviewer speaks an opening prompt.
3. Candidate speech is transcribed by ASR or typed in the CLI learning scaffold.
4. Current answer and small context are assembled into an LLM request.
5. The LLM or mock responds with scoring, feedback, or a follow-up.
6. The response is stored in history/report data and later can be sent to TTS in the realtime version.
7. The loop continues until the interview ends, then a report is generated.

Keep this final shape visible while rebuilding, but do not over-split it in the first pass. Learn the whole project once, then deepen prompt, context, streaming, and safety details in a second pass.

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
3. `InterviewSession` generates default questions, role-based questions, or resume-based questions.
4. `DialogSession` keeps the session state, recent history, current answer, score records, and report records.
5. A prompt builder converts interview type, recent history, current answer, and role constraints into an LLM prompt.
6. LLM services generate questions, score answers, or produce interviewer follow-up text.
7. In the realtime version, ASR provides candidate text and TTS speaks interviewer text.
8. State machine updates CLI or UI.
9. Candidate answers are recorded and scored.
10. The session saves a report and optional summary.

## Safer Rebuild Shape

The reference project currently builds the Qt GUI entry point. For a beginner rebuild, it is still safer to start with CLI/domain slices and return to Qt after the core logic is testable. Avoid starting with the full realtime audio stack.

First half-month pass:

1. Build/common basics: CMake, config, logging, local run.
2. Interview domain and CLI flow: questions, answers, scoring, report.
3. LLM path: mock client, HTTP client, simple prompt rules, report integration.
4. External service boundaries: PDF boundary and realtime protocol/event concepts.
5. Dialogue/UI overview: mock dialogue loop, audio/WebSocket basics, minimal Qt window.
6. Review: README, learning record, known gaps, second-pass backlog.

Second pass:

1. Deeper prompt builder and conversation history.
2. More structured LLM outputs and validation.
3. Real PDF, PortAudio, WebSocket, streaming, and UI polish.

## Integration Boundaries

Create interfaces before concrete integrations, but only when the boundary is useful for learning, testing, or replacing an external service:

- `ILlmClient`
- `IPdfParser`
- `IAudioDevice`
- `IRealtimeClient`

Keep these boundaries simple in the first pass:

- `ILlmClient`: model capability such as generating questions and scoring answers.
- `IHttpTransport`: HTTP request delivery for the LLM client.
- prompt/context logic can start as small helper functions or value structs. Promote it to a dedicated component only when the code becomes hard to test or reuse.

This lets unit tests run without API keys, microphone permission, speakers, or network.

## Source-Specific Hazards

- Do not treat a legacy CLI entry point as the current product entry unless the build files support it.
- Keep headers self-contained; include every standard library and Qt type used by the header.
- Do not log complete LLM request bodies, resume text, full candidate answers, API keys, or realtime auth headers.
- Keep TLS certificate verification enabled for HTTPS and WSS.
- Avoid detached threads that capture raw `this`.
- Define one owner for WebSocket read/write/close operations.

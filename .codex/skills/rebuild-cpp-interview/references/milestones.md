# Milestones

## 0. Repository Skeleton

Output: CMake project, `src/main.cpp`, `include/`, `tests/`, `.clang-format`, `.gitignore`.

Learn: compiler, CMake target, include paths, build directory.

Verify: configure, build, run hello command, run empty test target if present.

## 1. Common Utilities

Output: logger wrapper, config loader with JSON validation, simple state enum.

Learn: namespaces, headers, source files, exceptions, `nlohmann::json`.

Verify: unit tests for missing config keys and invalid values.

## 2. Interview Domain

Output: `InterviewSession` with questions, answers, scores, report serialization.

Learn: classes, structs, vectors, value semantics, timestamps, JSON serialization.

Verify: tests for next question, answer recording, completion, report JSON.

## 3. LLM Abstraction

Output: `ILlmClient`, `MockLlmClient`, later `HttpLlmClient`.

Learn: interfaces, dependency injection, HTTP boundaries, error fallback.

Verify: domain tests use mock; one manual integration command can call the real API.

## 4. PDF Abstraction

Output: `IPdfParser`, fake parser for tests, real parser behind PoDoFo or another library.

Learn: adapter pattern, file validation, UTF-8 text, dependency isolation.

Verify: tests use sample text file or fake parser; real PDF test is optional/manual.

## 5. Protocol Codec

Output: binary protocol header generation, response parser, compression helpers if needed.

Learn: bytes, endian conversion, enum class, binary layout, parser tests.

Verify: unit tests with known byte fixtures.

## 6. Dialogue Orchestration

Output: `DialogSession` using mock realtime/audio clients.

Learn: callbacks, state transitions, queues, thread ownership, shutdown order.

Verify: deterministic tests for event sequences.

## 7. Audio Integration

Output: PortAudio adapter with explicit open/read/write/cleanup lifecycle.

Learn: RAII, blocking I/O, sample format, device errors, permissions.

Verify: manual device listing or short local record/play smoke test.

## 8. Realtime WebSocket Integration

Output: realtime client that connects, sends audio/text, receives events.

Learn: TLS, WebSocket handshake, auth headers, background receive thread.

Verify: manual integration test with local config and no committed secrets.

## 9. Qt UI

Output: main window, config dialog, state display, transcript area, progress.

Learn: Qt signals/slots, main-thread UI updates, worker thread safety.

Verify: manual run plus screenshot or checklist.

## 10. GitHub Polish

Output: README, CI, PR template, release checklist.

Learn: commits, branches, CI signals, review habit.

Verify: clean `git status`, passing CI or documented skipped checks.

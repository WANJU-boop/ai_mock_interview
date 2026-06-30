# Milestones

This project should be learned in one broad pass first, then deepened in later passes. For a beginner, avoid turning every LLM, audio, WebSocket, or Qt detail into a separate long milestone during the first half-month.

## 1. Build And Common Basics

Approximate days: Day 1-2.

Output: buildable CMake project, configuration loading, logging, simple validation, and a clear local run command.

Learn: compiler errors, CMake targets, include paths, JSON config, environment variables, and why secrets stay outside git.

Verify: configure/build succeeds; config tests cover missing keys, wrong types, and a valid example.

## 2. Interview Domain And CLI Flow

Approximate days: Day 3-4.

Output: question list, answer recording, score records, session completion, report JSON, and a CLI learning loop.

Learn: `struct` vs `class`, vectors, strings, value semantics, single responsibility, and keeping domain logic independent from UI/network code.

Verify: tests for next question, answer recording, completion, report serialization, plus one manual CLI run.

## 3. LLM Path With Mock And HTTP

Approximate days: Day 5-8.

Output: `ILlmClient`, `MockLlmClient`, `HttpLlmClient`, fake HTTP transport tests, simple prompt rules, scoring feedback, and report integration.

Learn: interfaces, dependency injection, why tests use mocks, JSON request/response handling, API key environment variables, base URL, status-code errors, and basic prompt design.

Verify: unit tests do not require network; fake transport tests cover URL, headers, body, bad JSON, missing fields, score range, and non-2xx responses. Real LLM calls stay as manual integration checks.

## 4. External Service Boundaries

Approximate days: Day 9-10.

Output: lightweight boundaries for PDF parsing, realtime protocol/event parsing, and mock service data.

Learn: adapter pattern, binary protocol basics, enum values, payload boundaries, and how to keep third-party services outside core domain tests.

Verify: tests use fake PDF text or fixed byte/event fixtures. Real PDF and realtime services remain optional manual checks in the first pass.

## 5. Dialogue, Audio, WebSocket, And Qt Overview

Approximate days: Day 11-14.

Output: mock dialogue orchestration, audio/WebSocket learning notes or smoke checks, and a minimal Qt window that can display interview state.

Learn: callbacks, state transitions, event queues, thread ownership, shutdown order, audio vocabulary, WebSocket connection lifecycle, Qt signals/slots, and main-thread UI updates.

Verify: deterministic mock event sequence test, optional audio smoke test, optional manual WebSocket check, and manual Qt window run.

## 6. Review, Documentation, And Next Iteration

Approximate day: Day 15.

Output: README updates, development record, known gaps, and a second-pass backlog.

Learn: small commits, clean diffs, test reporting, and how to turn a learning prototype into a maintainable project.

Verify: build and relevant tests pass, `git status` is understood, skipped checks are documented.

## Second-Pass Deepening Backlog

After the first half-month pass, deepen these areas one by one:

- Prompt builder and conversation history control.
- Structured question, scoring, and follow-up payloads.
- LLM response validation and safety guardrails.
- Real PDF parser integration.
- Real PortAudio adapter.
- Real WebSocket realtime client and streaming output.
- Qt UI polish and packaging.

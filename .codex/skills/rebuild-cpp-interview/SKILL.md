---
name: rebuild-cpp-interview
description: Rebuild a C++ AI mock interview desktop project from scratch with Codex. Use when planning or implementing milestones, mapping source-project modules, adding CMake/vcpkg/Qt/audio/PDF/LLM/WebSocket features, or keeping the work beginner-friendly and verifiable.
---

# Rebuild C++ Interview

## Core Rule

Build the new project incrementally. Prefer a small runnable slice with tests over a large copied block. Treat the original project as a reference for behavior and architecture, not as code to paste.

## Workflow

1. Inspect the current new repository before editing.
2. Identify the current milestone from `references/milestones.md`.
3. State the learning goal, the files to touch, and the verification command before editing.
4. Implement the smallest useful slice.
5. Run formatting, build, and tests that exist for the current milestone.
6. Summarize what changed, what concept was learned, and the next small task.

## Architecture References

Read only the reference needed for the current task:

- `references/project-map.md`: module boundaries, data flow, dependency direction.
- `references/milestones.md`: staged rebuild path from command-line skeleton to Qt app.
- `references/verification.md`: build, test, Git, and external-service checks.

## Implementation Guidance

- Start with CLI and pure C++ modules before Qt.
- Create interfaces and mocks before real network/audio/PDF integrations.
- Keep external service calls out of unit tests.
- Never commit API keys, App IDs, access keys, generated reports, logs, or local config files.
- Prefer `std::unique_ptr`, `std::shared_ptr`, RAII, and narrow ownership.
- Use `#pragma once`, named namespaces, no `using namespace`, and function-scope `using` only when it improves readability.
- Add comments only for non-obvious decisions, not for literal code restatement.

## When The User Is Learning

Explain the target concept before changing code, but keep the patch moving. After the patch, give one small exercise that reinforces the same concept.

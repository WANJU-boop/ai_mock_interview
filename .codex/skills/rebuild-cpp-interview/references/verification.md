# Verification

## Local Build

Use the commands that exist in the current repository. Preferred order:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build -j
ctest --test-dir build --output-on-failure
```

If a `build.py` wrapper exists:

```bash
python3 build.py --config Debug
```

## Formatting

Use `.clang-format` when present:

```bash
find include src tests -name '*.h' -o -name '*.cpp' | xargs clang-format -i
```

Adjust the command for the actual directories and shell.

## External Service Checks

- Unit tests must not require API keys, network, microphone, speakers, or PDF system libraries.
- Real LLM, PDF, audio, and WebSocket checks should be manual or integration tests that are skipped unless explicitly enabled.
- Keep local secret files ignored by Git.
- Check logs after integration tests to confirm secrets, full resumes, full candidate answers, and auth headers are not printed.
- Keep TLS certificate verification enabled in committed code.

## Git Checks

Before committing:

```bash
git status --short
git diff --stat
git diff
```

Commit only related files. Do not mix unrelated learning experiments with project code.

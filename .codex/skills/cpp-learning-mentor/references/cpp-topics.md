# C++ Topic Map

## Repository Skeleton

- 编译单元 (translation unit)
- 头文件与源文件分离 (header/source separation)
- CMake target, include directory, link library
- Debug 与 Release 构建

## Common Module

- 命名空间 (namespace)
- 枚举类 (enum class)
- 异常 (exception)
- 单例 (singleton) and when to avoid overusing it
- JSON parsing and validation

## Domain Module

- 类与结构体 (class and struct)
- 值语义 (value semantics)
- `std::vector`, `std::string`, `std::optional`
- RAII and deterministic cleanup
- Dependency injection for testability

## Services Module

- 接口 (interface) and adapter pattern
- `std::unique_ptr` and ownership
- Pimpl idiom
- HTTP request boundary
- Binary parsing and endian conversion
- File I/O and UTF-8 text

## Concurrency

- `std::thread`
- `std::atomic`
- `std::mutex`
- `std::condition_variable`
- Shutdown order and avoiding dangling callbacks

## Qt UI

- QObject lifetime
- Signals and slots
- Main-thread UI updates
- Worker thread communication
- Widget state derived from domain state

## Debugging Habits

- Reproduce with the smallest command.
- Read the first compiler error before later cascading errors.
- Add a focused test before changing shared behavior.
- Prefer logs at module boundaries, not inside every line.

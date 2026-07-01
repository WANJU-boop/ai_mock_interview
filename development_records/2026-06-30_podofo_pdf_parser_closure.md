# PoDoFo 真实 PDF 简历解析闭环

## 本次目标

把 PDF 简历解析从 mock 边界推进到真实可用路径：使用 PoDoFo 读取文本型 PDF，把提取出的完整文本作为 `resume_context` 传给现有 LLM 出题流程。

本次按学习项目的外部服务边界原则实现：`session` 层继续只依赖 `IPdfParser`，PoDoFo 只出现在 `services` 层实现中。这样后续即使替换成 OCR、Poppler 或其他 PDF 方案，也不需要大改面试主流程。

## 核心数据流

1. `vcpkg.json` 增加 `podofo`，CMake 通过 `podofo::podofo` 链接真实 PDF 库。
2. `PodofoPdfParser` 接收 `PdfParseRequest::file_path`。
3. 空路径返回空文本，保持“无简历”的普通出题流程。
4. 非空路径先检查文件是否存在且是普通文件。
5. `PoDoFo::PdfMemDocument::Load` 加载 PDF。
6. 通过 `document.GetPages()` 遍历页面，并调用 `PdfPage::ExtractTextTo` 提取文本片段。
7. 每页文本按顺序拼接，页面之间保留换行；本次按需求不做字符数截断。
8. `prepareInterview` 把完整 `resume_context` 放入 `QuestionGenerationRequest`。
9. `HttpLlmClient` 已有逻辑会把 `resume_context` 拼进题目生成 prompt，真实 HTTP provider 会据此生成简历相关问题。

## 关键 C++ 知识点

- 依赖隔离：头文件只暴露 `PodofoPdfParser`，PoDoFo 头文件只在 `.cpp` 中 include，避免第三方库泄漏到上层模块。
- Adapter 模式：`PodofoPdfParser` 把 PoDoFo 的 API 转成项目内部统一的 `IPdfParser` 接口。
- 异常收口：PoDoFo 抛出的 `PdfError` 在 services 层转换成 `std::runtime_error`，`prepareInterview` 再统一转换成中文启动失败信息。
- 外部数据边界：PDF 文本只进入 LLM 私有上下文，不写入日志、不写入报告 JSON。
- 测试隔离：单元测试继续用 mock 和文件缺失边界，不依赖真实 PDF fixture、网络或 API key。

## 验证结果

- 已运行 `clang-format -i` 格式化本次修改的 C++ 文件。
- 已运行 `cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=ON`，沙箱内首次因 vcpkg 根目录锁权限失败，提升权限后配置通过。
- 已运行 `cmake --build build -j`，构建通过；链接阶段仍有既有的 DWARF/visibility warning，不影响目标生成。
- 已运行 `ctest --test-dir build --output-on-failure`，104/104 通过。
- 已用 `config.example.json` 做 CLI smoke test，确认默认空 `resume_path` 下生产入口使用真实 parser 也能正常完成 mock 面试流程。

## 当前限制

- 适用于文本型 PDF 简历；扫描版、截图版或图片型 PDF 可能提取为空，需要后续接 OCR。
- 当前不支持需要密码的加密 PDF。
- 本次按需求不做长度截断；如果简历文本过长，真实 HTTP LLM 可能出现上下文超限、请求体过大或超时，需要后续按模型能力再做提示或分段策略。
- 本次没有真实调用 HTTP LLM，避免使用用户 API key 和网络集成；真实模型验证应使用本地 `config.local.json` 和环境变量手动执行。

## 下一步建议

- 用一份真实文本型简历 PDF 做手动集成测试，确认题目确实围绕简历项目生成。
- 如果遇到扫描版简历，新增 OCR 边界，而不是把 OCR 逻辑塞进 `PodofoPdfParser`。
- 后续可以增加 `PdfParserFactory` 或配置项，在 mock、PoDoFo、OCR parser 之间显式切换。

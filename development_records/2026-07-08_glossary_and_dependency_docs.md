# 术语表与依赖图整理

## 本次目标

为代码解释文档目录补充三份新手友好的速查资料：

1. 项目术语表。
2. 最重要的 5 个文件。
3. 文字版模块依赖图。

## 核心数据流

```text
docs/code_explanations/README.md
  -> 04_glossary.md
  -> 05_key_files.md
  -> 06_module_dependency_graph.md
```

## 关键 C++ 知识点

- 通过 `#include`、CMake target 和构造函数参数理解依赖关系。
- 通过接口类型理解依赖注入，例如 `ILlmClient`、`IRealtimeClient`、`IPdfParser`。
- 通过 adapter pattern 理解供应商协议和业务事件的隔离。
- 通过入口文件和 app/session/services/common 分层建立项目全局心智模型。

## 验证结果

本次只新增和更新 Markdown 文档，不修改 C++ 代码，不需要重新编译。

已运行：

```bash
find docs/code_explanations -type f | sort
git diff --check
```

结果：文档文件已生成，diff 空白检查通过。

## 下一步建议

后续如果继续整理代码解释，可以按模块拆分：

1. `common` 模块导读。
2. `session` 面试领域模型导读。
3. `services` 外部能力边界导读。
4. `app` 和入口流程导读。

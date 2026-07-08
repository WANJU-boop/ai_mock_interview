# 代码解释文档目录整理

## 本次目标

新增统一的代码解释文档目录，把项目整体概览、架构心智模型、Realtime/WebSocket 流程和既有阶段记录整理到一个入口，方便后续快速回忆项目全貌。

## 核心数据流

```text
docs/code_explanations/README.md
  -> 项目整体概览
  -> 架构心智模型
  -> Realtime 与 WebSocket 流程
  -> 既有 docs 和 development_records 链接
```

## 关键 C++ 知识点

- 用模块边界理解 C++ 工程：`common`、`services`、`session`、`app`。
- 用接口隔离理解可测试性：业务层依赖抽象，测试注入 mock/fake。
- 用数据流理解复杂系统：从配置、取题、回答、评分、追问到报告。
- 用 adapter pattern 理解供应商协议和项目内部事件的隔离。

## 验证结果

本次只新增 Markdown 文档，不修改 C++ 代码，不需要重新编译。

已检查：

```bash
find docs/code_explanations -type f | sort
git diff --check
```

结果：文档文件已生成，diff 空白检查通过。

## 下一步建议

后续每完成一个较大的功能闭环，都可以把学习记录继续放入 `development_records/`，并在 `docs/code_explanations/README.md` 中按主题补链接。

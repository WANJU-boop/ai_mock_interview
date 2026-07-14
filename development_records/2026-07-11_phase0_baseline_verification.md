# 阶段 0：基线验证与分支收口

## 本次目标

在进入音频、并发和报告持久化之前，先确认当前主链路可以在独立功能分支上稳定构建。
这一步不改变业务行为；它固定后续四个小闭环共享的起点，避免把已有构建问题误认为新功能回归。

## 当前闭环

```text
配置 JSON
  -> PDF / LLM / realtime mock 边界
  -> DialogOrchestrator
  -> 评分、追问与报告 JSON
  -> 160 个离线测试
```

真实 HTTP、PDF 和 WebSocket 仍然不属于默认测试路径，因而本次验证不读取密钥、不访问网络、
不申请麦克风或扬声器权限。

## 验证结果

执行：

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build -j
ctest --test-dir build --output-on-failure
git diff --check
```

结果：构建成功，160/160 测试通过，空白检查通过。

链接阶段仍会出现 macOS SDK 的 `libiconv` 与已有静态库的 DWARF 警告；它们来自本机工具链或
第三方依赖，未造成链接失败，也不是本阶段代码引入的行为变化。

## 下一步建议

先实现不依赖 PortAudio 的 `IAudioDevice` 与 `FakeAudioDevice`。Fake 要记录采集和播放数据，
这样后续音频格式、停止顺序和 realtime 发送逻辑可以在无硬件环境中确定性测试。

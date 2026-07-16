# Qt 面试界面闭环

## 本次目标

在不改写既有 CLI、LLM、PDF、WebSocket、PortAudio 和报告实现的前提下，增加一个可运行的
Qt 桌面入口。界面既能使用默认 mock 配置完成离线面试，也能选择本地配置进入现有真实
`volc + keep_alive` 语音链路。

## 核心数据流

```text
MainWindow（Qt 主线程）
  -> 创建 QThread、InterviewWorker、DialogCancellationToken
  -> InterviewWorker 在自己的线程加载配置并创建所有外部服务
  -> DialogOrchestrator 同步运行面试状态机
  -> IDialogObserver 收到状态、面试官文本、partial/final transcript
  -> Qt queued signal 回到 MainWindow
  -> 展示会话摘要、实时状态、对话和报告路径
  -> worker 完成后 quit + deleteLater，窗口重新允许启动
```

用户点击停止或运行中关闭窗口时，主线程只调用原子取消令牌。编排器在事件循环安全点读取
请求，并继续由原 worker 按“音频 -> realtime 连接 -> 运行时对象”的所有权顺序收口。

## 关键设计

- `IDialogObserver` 是纯 C++ 接口，不包含 `QObject`、`QString` 或控件类型；现有 CLI 不需要修改。
- observer 已真正注入 Qt worker 创建的 `DialogOrchestrator`，不是悬空抽象。
- partial transcript 只覆盖实时识别标签，final transcript 才进入正式对话、评分和报告。
- `DialogCancellationToken` 只传递原子状态，不允许 UI 线程直接关闭 WebSocket 或 PortAudio。
- 阻塞 LLM 评分仍使用可回收 future；取消期间等待 future 到安全回收点，避免引用已析构 manager。
- worker 在同一线程创建和销毁 LLM、PDF parser、realtime client、audio device 与 audio bridge。
- macOS 生成标准 `.app` bundle，并在 `Info.plist` 声明麦克风用途。
- Qt 6.10 的新 linker 告警参数只在两个 Qt 最终目标上禁用，现有纯 C++ target 不受影响。

## 界面能力

- 选择 `config.example.json` 或本地 `config.local.json`。
- 展示候选人、岗位、题目数和 realtime provider。
- 展示状态、面试官文本、候选人 final transcript 和实时 partial transcript。
- 防止重复启动，支持停止请求和运行中安全关闭。
- 显示本地 JSON 报告路径，并可打开报告目录。
- mock 和真实 `volc + keep_alive` 复用同一 worker/编排入口。

## 验证结果

- `qtbase` 已通过 vcpkg 安装，CMake Debug 配置成功。
- `cmake --build build -j` 成功，生成 `AI_mock_interview_qt.app`。
- 全部离线测试通过：202/202。
- 新增测试覆盖 observer 事件、启动前取消、macOS bundle 配置查找、窗口构造、Qt worker mock
  完整面试和 Qt 平台插件选择。
- 使用本机 Qt 窗口手动完成 3 道 mock 面试：按钮状态、会话摘要、问答文本、完成状态和报告路径
  均正常显示；应用正常退出，退出码为 0。
- 默认测试和手动 mock 检查均未访问网络、麦克风、真实 PDF 或 API key。

## 已知边界与下一步

- Qt 真实语音入口复用此前已经手动验证的 CLI 火山/PortAudio 主链路；本次没有再次使用真实密钥、
  麦克风和网络做 UI 联调，因此换机器后仍需手动确认权限与本地配置。
- 外部 HTTP 或首次 WSS 连接正在阻塞时，取消需要等当前配置的超时返回；当前不会强制终止线程。
- 下一步可进入最终复盘：整理第二轮 backlog、CI 的 Qt 构建缓存和正式 `.app` 打包/签名说明。

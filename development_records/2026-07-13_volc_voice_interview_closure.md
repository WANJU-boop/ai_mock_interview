# 火山语音面试主链路收口

## 本次目标

在不依赖 Qt 界面的前提下，把真实语音面试需要的麦克风采集、火山 ASR/TTS、HTTP LLM 评分、扬声器播放和报告导出串成一条可验证主链路。

## 核心数据流

1. `PortAudioAudioDevice` 以 16 kHz、单声道、320 帧（20ms）生成 PCM 块。
2. `RealtimeAudioBridge` 在 realtime worker 中读取 PCM，通过 `IRealtimeClient` 发送。
3. 火山音频请求使用 `AudioOnlyRequest + TaskRequest + session_id + raw PCM`，不再使用无事件的 sequence frame。
4. adapter 暂存 final `ASRResponse`，直到收到 `ASREnded` 才把候选人回答交给面试状态机。
5. HTTP LLM 评分在可回收的 future 中运行；realtime worker 同时继续发送静音保活包和消费服务端事件。
6. 开场欢迎语和第一题合并为 `SayHello`；收到首轮 `ASREnded` 后，后续问题/追问才使用 `ChatTTSText`，符合火山事件顺序约束。
7. adapter 只转发本地主流程请求的 TTS PCM，过滤火山模型自带的 default 闲聊语音。
8. 播放期间麦克风块被替换为静音，避免扬声器回放被 ASR 重新识别。结束语的本地播放队列清空后才关闭设备和 WSS。
9. Boost.Beast 始终保持一条 `async_read`，完整 WebSocket message 才进入线程安全接收队列；ping/pong 和 TLS 半包不会阻塞面试状态机。

## 配置变化

- 完整语音模式使用 `realtime.dialog.input_mod=keep_alive`。
- 被 Git 忽略的 `config.local.json` 可直写 `llm.api_key`、`realtime.connection.app_id` 和 `realtime.connection.access_key`。
- 直写值为空时仍保留环境变量回退，便于 CI/部署；任何代码和日志都不输出密钥。

## 关键 C++ 知识点

- WebSocket 握手后由单一 `io_context` 线程串行异步操作；同一 stream 同时最多存在一条 `async_read` 和一条 `async_write`。
- 使用 `std::optional` 表示“当前没有事件”，而不是把正常空轮询当异常。
- 使用 `std::future` 隔离阻塞 HTTP，但不让多个线程并发读写同一 WebSocket。
- 用状态和计数器区分“多条 TTS 已入队”、“服务端已结束”和“本地还未播完”。
- 播放缓存上限扩展为 60 秒 PCM；录音每轮只投递一块，避免持续采集让主循环饥饿。
- 使用服务端 PCM 样本数计算播放截止时间，作为个别 PortAudio 设备队列状态不归零时的有界兜底。

## 验证结果

- Debug 构建通过：`cmake --build build -j`。
- 离线单元/集成测试通过：198/198。
- `/Users/luowanju/Documents/resume.pdf` 存在且 PoDoFo 可读取；PDF 自身有 free-object/字体间距警告，但未导致解析失败。
- 真实 TokenPony LLM、火山 WSS、macOS 麦克风和扬声器联调通过。
- 真实运行完成 3 道题：每题均走通 TTS 播放、PCM 上传、`ASRResponse`、`ASREnded`、HTTP LLM 评分和下一题推进。
- 结束语播放完成后正常发送 `FinishSession` / `FinishConnection` 并释放 PortAudio。
- 报告成功保存为 `reports/interview-report-1783995894694-0.json`，包含 3 条问答与评分记录。

## 下一步

后续可直接运行：

```bash
./build/AI_mock_interview_realtime_demo config.local.json
```

`config.local.json` 已被 Git 忽略，仍须避免截图、日志和提交历史暴露其中密钥。首次换到新的终端宿主时，需要重新允许 macOS 麦克风权限。

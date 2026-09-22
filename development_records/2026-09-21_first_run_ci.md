# 首次运行与 CI 依赖闭环

## 本次目标

让首次克隆项目的读者知道如何安装 vcpkg、设置 VCPKG_ROOT、编译和运行默认 Mock。
让 GitHub Actions 真正安装项目依赖，并从应用入口验证报告生成。

## 核心数据流

GitHub checkout → 安装 Linux 系统开发包 → 读取 vcpkg baseline → bootstrap →
CMake manifest 安装 → 编译 → CTest → 默认 realtime Mock demo → 验证三条评分记录。

## 关键工程知识

- Mock 替换的是运行期外部服务，当前 CMake 仍然链接完整第三方依赖。
- vcpkg 管理 C++ 库，Qt 窗口系统、音频等平台开发包仍由系统包管理器安装。
- 固定 baseline 限定依赖来源；缓存只加速编译，不能替代测试。
- Qt 的 Linux 自动测试使用 offscreen，不要求云端存在显示器。
- 除单元测试外，实际运行入口并检查输出报告，能验证配置与文件导出没有断开。

## 验证结果

- 改动前 macOS Debug 构建成功，202 项 CTest 全部通过。
- 最终 Ubuntu 24.04 [CI 运行成功](https://github.com/WANJU-boop/ai_mock_interview/actions/runs/35673504548)：
  提交 `64e1c14`，全部程序编译成功，203/203 CTest 通过（1.11 秒），
  默认 Mock 入口生成一份含三条评分记录的报告，整轮耗时 2 分 14 秒。
- 同版 macOS 构建成功，203/203 CTest 通过（12.92 秒），Qt 测试已能在无图形会话的沙箱运行。
- 文档命令与 CI 使用相同的依赖 baseline 和默认 Mock 配置。
- 第一次云端运行在 spdlog 版本解析阶段失败：vcpkg 的浅克隆不包含 override 引用的
  历史 port tree。已改为完整获取 vcpkg 历史，保留原有版本约束。
- 第二次云端运行进入 Qt 配置后失败，日志明确显示 `X11_SM_FOUND` 为空。
  已在 CI 和安装文档补充 `libsm-dev`，它同时安装依赖的 `libice-dev`。
- CI 在配置成功后立即保存完整依赖缓存；配置失败则用独立 key 保存部分缓存，
  避免不可覆盖的部分缓存占用完整缓存 key，也避免每次重新编译之前成功的库。
- 公开仓库的 Ubuntu runner 提供 4 核、16 GB 内存，依赖并发从 2 调整到 4；
  本地文档仍建议初学者以 2 个项目编译任务起步。
- 第三次云端运行成功编译全部 112 个依赖并保存缓存，随后在项目配置阶段失败：
  小写的 `find_package(openssl CONFIG REQUIRED)` 在 Linux 上找不到实际的
  `OpenSSLConfig.cmake`。改用标准的 `find_package(OpenSSL REQUIRED COMPONENTS SSL Crypto)`，
  通过 CMake 的 FindOpenSSL 模块提供原有的两个链接目标，保持依赖版本不变。
- 第四次云端运行复用全部依赖后进入最终链接，火山文本 demo 出现
  `Logger::GetLogger()` 未定义引用。`services_lib` 的实现调用了 `common_lib`，
  但原 CMake 没有声明这条依赖；现显式关联，由 CMake 排列静态库的链接顺序，
  不依赖 macOS 链接器的宽容行为或在各个可执行程序中手动调整顺序。
- 第五次运行完成全部程序的编译与链接，202 项普通测试通过，Qt 测试报找不到
  `offscreen`。插件已在依赖中构建，但静态 Qt 默认只导入系统平台插件。
  测试目标改用 `qt_import_plugins` 显式导入 offscreen，所有平台统一无显示器测试，
  桌面应用的平台插件不变。

## 下一步

首次运行与跨平台 CI 已形成闭环。Qt 报告展示、真实截图和文本接口验证
也已完成，详见同日的项目展示记录。后续优先考虑 Qt 无密钥文字回答模式，
让读者能在桌面界面亲自回答；真实服务可用性不影响现有无密钥演示。

本次成功运行从功能分支缓存恢复全部 112 个依赖，仅耗时 18 秒。
PR 的独立检查实际未命中该缓存，因此不能保证不同分支或首次合并后的运行同样快。

参考：[Ubuntu libsm-dev](https://packages.ubuntu.com/noble/libsm-dev)、
[GitHub 独立缓存保存](https://github.com/actions/cache/tree/v4/save)、
[GitHub runner 规格](https://docs.github.com/en/actions/reference/runners/github-hosted-runners)、
[CMake FindOpenSSL](https://cmake.org/cmake/help/latest/module/FindOpenSSL.html)、
[Qt 静态插件导入](https://doc.qt.io/qt-6/qt-import-plugins.html)。

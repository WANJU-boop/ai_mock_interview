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
- CI 运行记录会在功能分支推送后核实，不能把 workflow 文件存在视为 CI 成功。
- 文档命令与 CI 使用相同的依赖 baseline 和默认 Mock 配置。
- 第一次云端运行在 spdlog 版本解析阶段失败：vcpkg 的浅克隆不包含 override 引用的
  历史 port tree。已改为完整获取 vcpkg 历史，保留原有版本约束。

## 下一步

核实云端 Linux 首次构建结果，补 Qt 报告展示和真实软件截图。
真实服务可用性单独验证，不影响无密钥演示。

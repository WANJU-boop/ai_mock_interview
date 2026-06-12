#pragma once

namespace interview {
namespace services {

// 当前服务层还没有真实实现，这个占位函数只负责让服务层目标保持非空，
// 这样 CMake 在早期阶段也能稳定生成对应的静态库。
void linkServiceLayerPlaceholder();

}  // namespace services
}  // namespace interview

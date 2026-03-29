#include "common/logger.h"

int main() {
    interview::common::Logger::Init("interview.log", true);
    LOG_INFO("hello spdlog");
    return 0;
}



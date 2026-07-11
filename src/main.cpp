#include "app/cli_interview_app.h"
#include "common/config.h"
#include "common/logger.h"
#include "services/llm/llm_client_factory.h"
#include "services/pdf/podofo/podofo_pdf_parser.h"
#include "session/interview_setup.h"

#include <exception>
#include <iostream>
#include <memory>
#include <string>

int main(int argc, char* argv[]) {
    // 初始化日志系统，方便之后观察状态变化和异常情况。
    interview::common::Logger::Init("interview.log", false);

    const std::string config_path =
        argc > 1 ? argv[1] : interview::common::findDefaultConfigPath(argv[0]);

    try {
        const interview::common::AppConfig config =
            interview::common::loadConfigFromFile(config_path);
        // 入口层只负责组装依赖，具体 provider 解析逻辑收口到 services 层。
        std::unique_ptr<interview::services::ILlmClient> llm_client =
            interview::services::createLlmClient(config.llm);
        // 生产入口使用真实 PoDoFo 解析器；测试仍通过 IPdfParser 注入 mock，避免依赖真实 PDF 文件。
        interview::services::PodofoPdfParser pdf_parser;
        interview::session::PreparedInterview prepared_interview =
            interview::session::prepareInterview(config.interview, *llm_client, pdf_parser);
        // main 只保留初始化和错误码返回，把可测试的主流程交给 app 层函数。
        const std::string report_output_directory =
            config.report.save_json ? config.report.output_directory : "";
        return interview::app::runCliInterview(std::cin, std::cout, prepared_interview,
                                               report_output_directory);
    } catch (const std::exception& error) {
        std::cerr << "面试应用初始化失败：" << error.what() << '\n';
        return 1;
    }
}

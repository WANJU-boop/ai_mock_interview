#include "session/interview_report.h"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

namespace interview {
namespace session {

namespace {

// 评分本身是问答记录的一部分，单独转换能让主记录序列化更清楚。
nlohmann::json buildScoreJson(const ScoreResultRecord& score_result) {
    return nlohmann::json{{"score", score_result.score}, {"feedback", score_result.feedback}};
}

// 每条问答记录保留主问题、主回答、追问信息和最终评分，避免展示层重新理解业务结构。
nlohmann::json buildQuestionAnswerRecordJson(const QuestionAnswerRecord& record) {
    return nlohmann::json{{"question", record.question},
                          {"candidate_answer", record.candidate_answer},
                          {"has_follow_up", record.has_follow_up},
                          {"follow_up_prompt", record.follow_up_prompt},
                          {"follow_up_answer", record.follow_up_answer},
                          {"final_score", buildScoreJson(record.final_score)}};
}

// 同一毫秒内连续结束的面试也需要不同文件名。计数器仅用于进程内去重，不表达业务身份。
std::atomic<unsigned long> g_report_sequence{0};

} // namespace

nlohmann::json buildInterviewReportJson(const DialogSession& session) {
    nlohmann::json records = nlohmann::json::array();
    for (const QuestionAnswerRecord& record : session.getQuestionAnswerRecords()) {
        records.push_back(buildQuestionAnswerRecordJson(record));
    }

    // 返回新的 JSON 对象，不暴露 DialogSession 内部容器，也不修改会话状态。
    return nlohmann::json{{"question_count", session.getQuestionAnswerRecordCount()},
                          {"question_answer_records", records}};
}

std::filesystem::path createInterviewReportPath(const std::string& output_directory) {
    if (output_directory.empty()) {
        throw std::invalid_argument("报告输出目录不能为空。");
    }

    const auto now = std::chrono::system_clock::now().time_since_epoch();
    const auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
    const unsigned long sequence = g_report_sequence.fetch_add(1, std::memory_order_relaxed);
    // 文件名避免使用候选人姓名、岗位或简历名，防止文件浏览器和同步工具泄露额外个人信息。
    return std::filesystem::path(output_directory) /
           ("interview-report-" + std::to_string(milliseconds) + "-" + std::to_string(sequence) +
            ".json");
}

void saveInterviewReportJson(const DialogSession& session,
                             const std::filesystem::path& output_path) {
    if (output_path.empty() || output_path.filename().empty()) {
        throw std::invalid_argument("报告输出文件路径不能为空。");
    }
    if (std::filesystem::exists(output_path)) {
        // 面试报告属于候选人数据；不覆盖比“方便重复运行”更重要。
        throw std::runtime_error("报告输出文件已存在，拒绝覆盖：" + output_path.string());
    }

    const std::filesystem::path parent_directory = output_path.parent_path();
    std::error_code filesystem_error;
    if (!parent_directory.empty()) {
        std::filesystem::create_directories(parent_directory, filesystem_error);
        if (filesystem_error) {
            throw std::runtime_error("无法创建报告输出目录：" + parent_directory.string());
        }
    }

    // 先写同目录临时文件，再 rename 成正式文件。读取方要么看到旧文件（本 API 不覆盖），
    // 要么看到完整 JSON，不会看到写到一半的候选人报告。
    const std::filesystem::path temporary_path = output_path.string() + ".tmp";
    if (std::filesystem::exists(temporary_path)) {
        throw std::runtime_error("报告临时文件已存在，拒绝覆盖：" + temporary_path.string());
    }

    {
        std::ofstream output(temporary_path, std::ios::binary | std::ios::trunc);
        if (!output.is_open()) {
            throw std::runtime_error("无法打开报告临时文件：" + temporary_path.string());
        }
        // 报告正文只写进用户指定文件，不发送给 Logger；日志里至多由 app 层显示路径和错误摘要。
        output << buildInterviewReportJson(session).dump(2) << '\n';
        output.flush();
        if (!output.good()) {
            output.close();
            std::filesystem::remove(temporary_path, filesystem_error);
            throw std::runtime_error("写入报告临时文件失败：" + temporary_path.string());
        }
    }

    std::filesystem::rename(temporary_path, output_path, filesystem_error);
    if (filesystem_error) {
        // rename 失败时只删除本调用刚创建的临时文件，不触碰目标文件或其它会话的文件。
        std::filesystem::remove(temporary_path, filesystem_error);
        throw std::runtime_error("无法完成报告原子写入：" + output_path.string());
    }
}

} // namespace session
} // namespace interview

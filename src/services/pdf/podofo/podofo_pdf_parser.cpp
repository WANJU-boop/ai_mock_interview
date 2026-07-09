#include "services/pdf/podofo/podofo_pdf_parser.h"

// clang-format off
#include <cctype>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

#include <podofo/podofo.h>
// clang-format on

namespace interview {
namespace services {

namespace {

bool endsWithWhitespace(const std::string& text) {
    if (text.empty()) {
        return false;
    }

    const unsigned char last_char = static_cast<unsigned char>(text.back());
    return std::isspace(last_char) != 0;
}

void appendTextEntry(std::string& output, const std::string& entry_text) {
    if (entry_text.empty()) {
        return;
    }

    // PoDoFo 按文本片段返回内容；片段之间补空格，避免英文单词或中文段落被硬拼在一起。
    if (!output.empty() && !endsWithWhitespace(output)) {
        output += ' ';
    }
    output += entry_text;
}

void appendPageText(std::string& output, const std::vector<PoDoFo::PdfTextEntry>& entries) {
    const std::size_t before_page_size = output.size();
    for (const PoDoFo::PdfTextEntry& entry : entries) {
        appendTextEntry(output, entry.Text);
    }

    // 页面之间保留换行，给 LLM 一个基本的段落边界；这里不做长度截断，完整保留提取结果。
    if (output.size() > before_page_size && output.back() != '\n') {
        output += '\n';
    }
}

std::runtime_error makePodofoError(const std::string& context, const PoDoFo::PdfError& error) {
    return std::runtime_error(context + "：" + std::string(error.what()));
}

} // namespace

PdfParseResult PodofoPdfParser::parseResume(const PdfParseRequest& request) {
    if (request.file_path.empty()) {
        // 空路径依旧表示无简历，和 mock 行为保持一致，调用方可以走普通出题流程。
        return {""};
    }

    const std::filesystem::path resume_path(request.file_path);
    std::error_code file_error;
    if (!std::filesystem::exists(resume_path, file_error) ||
        !std::filesystem::is_regular_file(resume_path, file_error)) {
        throw std::runtime_error("PDF 简历文件不存在或不是普通文件：" + request.file_path);
    }

    PoDoFo::PdfMemDocument document;
    try {
        // PoDoFo 在 Load 阶段完成 PDF 结构解析；当前第一版不支持需要密码的加密 PDF。
        document.Load(request.file_path);
    } catch (const PoDoFo::PdfError& error) {
        throw makePodofoError("PoDoFo 加载 PDF 简历失败", error);
    }

    if (document.IsEncrypted()) {
        throw std::runtime_error("PDF 简历是加密文件，当前版本暂不支持解析。");
    }

    std::string extracted_text;
    const PoDoFo::PdfPageCollection& pages = document.GetPages();
    const unsigned page_count = pages.GetCount();
    for (unsigned page_index = 0; page_index < page_count; ++page_index) {
        std::vector<PoDoFo::PdfTextEntry> entries;
        try {
            // ExtractTextTo 只提取已有文字层；扫描版或截图版 PDF 通常会得到空结果，需要后续 OCR。
            pages.GetPageAt(page_index).ExtractTextTo(entries);
        } catch (const PoDoFo::PdfError& error) {
            throw makePodofoError("PoDoFo 提取第 " + std::to_string(page_index + 1) + " 页文本失败",
                                  error);
        }
        appendPageText(extracted_text, entries);
    }

    return {extracted_text};
}

} // namespace services
} // namespace interview

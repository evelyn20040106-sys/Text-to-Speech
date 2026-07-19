#pragma once // 防止头文件被重复包含
#include <string> // 提供 std::wstring 支持

/**
 * @brief 读取 UTF-8 编码的 TXT 文件，返回 UTF-16 宽字符串
 *
 * 功能说明：
 * - 以二进制方式读取文件内容
 * - 自动跳过文件开头的 UTF-8 BOM（EF BB BF）
 * - 将 UTF-8 字节序列转换为 UTF-16（wstring）
 * - 失败时返回空字符串并输出错误信息
 */
class FileReader
{
public:
    /**
     * @brief 读取指定路径的文本文件全部内容
     * @param filePath  要读取的 TXT 文件完整路径（宽字符，支持中文）
     * @return std::wstring  文件内容的 UTF-16 宽字符串，失败返回空字符串
     *
     * 使用 Windows API CreateFileW / ReadFile 读取文件，
     * 支持包含中文的空格和特殊字符的路径。
     */
    static std::wstring ReadAllText(const std::wstring& filePath);
};

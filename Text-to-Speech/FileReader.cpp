// 引入 FileReader 类的头文件
#include "FileReader.h"
// 用于存储读取到的原始字节数据
#include <vector>
// 用于输出错误信息到 stderr
#include <iostream>
// Windows API 头文件，提供 CreateFileW / ReadFile 等函数
#include <Windows.h>

/**
 * @brief 读取 UTF-8 文本文件，返回 UTF-16 宽字符串
 * @param filePath  文件路径（宽字符 UTF-16）
 * @return std::wstring  UTF-16 编码的文件内容，失败则返回空字符串
 *
 * 实现步骤：
 * 1. 用 CreateFileW 打开文件（支持中文路径）
 * 2. 获取文件大小，分配缓冲区
 * 3. 用 ReadFile 读取全部字节
 * 4. 跳过开头的 UTF-8 BOM（如果有）
 * 5. 用 MultiByteToWideChar 将 UTF-8 转为 UTF-16
 */
std::wstring FileReader::ReadAllText(const std::wstring& filePath)
{
    /* ---- 步骤 1：打开文件 ---- */
    // 以只读方式打开，允许共享读取，文件必须已存在
    HANDLE hFile = CreateFileW(
        filePath.c_str(),      // 文件路径（宽字符）
        GENERIC_READ,          // 只读访问
        FILE_SHARE_READ,       // 允许其他进程同时读取
        nullptr,               // 安全属性：默认
        OPEN_EXISTING,         // 只打开已存在的文件
        FILE_ATTRIBUTE_NORMAL, // 普通文件属性
        nullptr                // 模板文件：无
    );
    // 检查文件是否成功打开
    if (hFile == INVALID_HANDLE_VALUE)
    {
        // 获取并显示 Windows 错误码，方便用户排查路径问题
        DWORD err = GetLastError();
        std::wcerr << L"\n[Error] Cannot open file (code=" << err << L"): "
                   << filePath << std::endl;
        return L""; // 返回空字符串表示失败
    }

    /* ---- 步骤 2：获取文件大小 ---- */
    DWORD fileSize = GetFileSize(hFile, nullptr);
    // 空文件直接返回
    if (fileSize == 0)
    {
        CloseHandle(hFile); // 关闭文件句柄
        std::wcerr << L"\n[Error] File is empty" << std::endl;
        return L"";
    }

    /* ---- 步骤 3：读取全部文件内容到缓冲区 ---- */
    // 多分配一个字节用于存放字符串终止符 '\0'
    std::vector<char> buffer(fileSize + 1);
    DWORD bytesRead = 0; // 实际读取到的字节数
    // 从文件读取全部数据
    if (!ReadFile(hFile, buffer.data(), fileSize, &bytesRead, nullptr))
    {
        CloseHandle(hFile);
        std::wcerr << L"\n[Error] Failed to read file" << std::endl;
        return L"";
    }
    CloseHandle(hFile); // 关闭文件，不再需要
    // 在末尾添加字符串终止符，确保缓冲区可安全用作 C 字符串
    buffer[bytesRead] = '\0';

    /* ---- 步骤 4：跳过 UTF-8 BOM ---- */
    // UTF-8 BOM 是 3 个字节：EF BB BF
    int offset = 0; // 从缓冲区哪个位置开始处理
    // 检查文件头三个字节是否是 BOM
    if (bytesRead >= 3 &&
        static_cast<unsigned char>(buffer[0]) == 0xEF &&
        static_cast<unsigned char>(buffer[1]) == 0xBB &&
        static_cast<unsigned char>(buffer[2]) == 0xBF)
    {
        offset = 3; // 跳过 BOM，从第 4 个字节开始
    }

    /* ---- 步骤 5：将 UTF-8 转换为 UTF-16 ---- */
    // 第一次调用：计算转换后所需的宽字符数
    int wlen = MultiByteToWideChar(
        CP_UTF8,                              // 源编码：UTF-8
        0,                                    // 标志：默认
        buffer.data() + offset,               // 源数据（跳过 BOM 后）
        static_cast<int>(bytesRead - offset), // 源数据长度
        nullptr,                              // 输出缓冲区：null 表示只计算长度
        0                                     // 输出缓冲区大小：0
    );
    // 转换失败时 wlen <= 0
    if (wlen <= 0)
    {
        std::wcerr << L"\n[Error] Failed to convert UTF-8 to UTF-16" << std::endl;
        return L"";
    }

    // 分配足够容纳转换结果的宽字符串
    std::wstring result(wlen, L'\0');
    // 第二次调用：实际执行编码转换
    MultiByteToWideChar(
        CP_UTF8,                              // 源编码：UTF-8
        0,                                    // 标志：默认
        buffer.data() + offset,               // 源数据
        static_cast<int>(bytesRead - offset), // 源数据长度
        &result[0],                           // 输出缓冲区
        wlen                                  // 缓冲区大小
    );
    return result; // 返回转换后的宽字符串
}

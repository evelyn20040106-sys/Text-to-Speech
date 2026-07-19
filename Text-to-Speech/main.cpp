// 提供 std::cout / std::cin / std::cerr 等控制台输入输出
#include <iostream>
// 提供 std::string / std::wstring 支持
#include <string>
// Windows API 头文件，提供 SetConsoleOutputCP / GetConsoleCP 等
#include <Windows.h>
// 引入文件读取模块，用于读取 UTF-8 TXT 文件
#include "FileReader.h"
// 引入 TTS 引擎模块，用于文字转语音
#include "TtsEngine.h"

/**
 * @brief 将窄字符串（控制台代码页编码）转换为宽字符串（UTF-16）
 * @param input  控制台输入的窄字符串（系统默认代码页，如 GBK）
 * @return std::wstring  转换后的 UTF-16 宽字符串

 * 使用 GetConsoleCP() 获取控制台当前代码页（中文 Windows 通常为 936 = GBK），
 * 然后通过 MultiByteToWideChar 将窄字符串转换为宽字符串。
 * 这样可以正确处理用户输入的中文路径。
 */
static std::wstring ToWide(const std::string& input)
{
    // 空字符串直接返回空宽字符串
    if (input.empty()) return L"";

    // 获取控制台输入代码页（如 936 = GBK / 65001 = UTF-8）
    UINT cp = GetConsoleCP();
    // 获取失败时回退到系统默认 ANSI 代码页
    if (cp == 0) cp = CP_ACP;

    // 第一次调用 MultiByteToWideChar：计算转换后所需的宽字符数
    int len = MultiByteToWideChar(
        cp,                          // 源编码：控制台代码页
        0,                           // 标志：默认
        input.c_str(),               // 源窄字符串
        (int)input.size(),           // 源字符串长度
        nullptr,                     // 不输出，只计算长度
        0                            // 输出缓冲区大小为 0
    );
    // 转换失败（如无效编码）则返回空字符串
    if (len <= 0) return L"";

    // 分配足够的缓冲区并执行实际转换
    std::wstring result(len, L'\0');
    MultiByteToWideChar(
        cp,                          // 源编码
        0,                           // 标志
        input.c_str(),               // 源窄字符串
        (int)input.size(),           // 源字符串长度
        &result[0],                  // 输出缓冲区
        len                          // 缓冲区大小
    );
    return result;
}

/**
 * @brief 从控制台读取一行用户输入
 * @return std::string  用户输入的窄字符串（控制台代码页编码）
 *
 * 使用 std::getline 而非 std::wcin，因为 wcin 对中文输入支持不稳定。
 * 得到窄字符串后再由 ToWide() 转换为宽字符串。
 */
static std::string ReadLine()
{
    std::string line;           // 用于存储用户输入的窄字符串
    std::getline(std::cin, line); // 从标准输入读取一行
    return line;
}

/**
 * @brief 程序主入口
 * @return int  0=正常退出, 1=出错退出
 *
 * 执行流程：
 * 1. 设置控制台输出为 UTF-8，以正确显示中文字符
 * 2. 提示用户输入 TXT 文件路径和 WAV 输出路径
 * 3. 调用 FileReader::ReadAllText 读取 TXT 文件内容
 * 4. 初始化 TtsEngine 语音引擎
 * 5. 设置语速和音量，调用 SpeakToFile 转换
 * 6. 输出转换结果
 */
int main()
{
    // 设置控制台输出代码页为 UTF-8，使 std::cout 能正确输出中文
    SetConsoleOutputCP(CP_UTF8);
    // 注意：不修改输入代码页，保持系统默认（如 GBK 936）
    // 以便 MultiByteToWideChar 能正确转码用户输入的中文路径

    // 显示程序标题和用法说明
    std::cout << "========================================" << std::endl;
    std::cout << "      TTS - TXT to Speech Converter     " << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "Example:" << std::endl;
    std::cout << "  Input  -> C:\\path\\to\\text.txt" << std::endl;
    std::cout << "  Output -> C:\\path\\to\\speech.wav" << std::endl;
    std::cout << "----------------------------------------" << std::endl;

    /* ---- 获取用户输入的文件路径 ---- */
    // 用窄字符输入 + 显式转码的方式，避免 wcin 对中文支持不稳定的问题
    std::wstring inputFile;  // 输入的 TXT 文件路径
    std::wstring outputFile; // 输出的 WAV 文件路径

    // 提示用户输入 TXT 文件路径
    std::cout << "Input  TXT file path: ";
    inputFile = ToWide(ReadLine()); // 读取并转码为宽字符串

    // 提示用户输入 WAV 输出路径
    std::cout << "Output WAV file path: ";
    outputFile = ToWide(ReadLine()); // 读取并转码为宽字符串

    // 检查输出路径是否看起来像目录（没有文件扩展名）
    // 如果用户只输入了目录路径，提醒他们补全文件名
    if (outputFile.find(L'.') == std::wstring::npos)
    {
        std::cout << "[Warning] Output path has no file extension." << std::endl;
        std::wcout << L"  Did you mean " << outputFile << L"\\output.wav ?" << std::endl;
    }

    /* ---- 读取 TXT 文件 ---- */
    std::cout << "\nReading file..." << std::endl;
    std::wstring text = FileReader::ReadAllText(inputFile);
    // 检查文件是否读取成功（返回空字符串表示失败）
    if (text.empty())
    {
        std::cerr << "[Error] Failed to read text from file" << std::endl;
        std::cout << "\nPress Enter to exit..." << std::endl;
        std::cin.get(); // 等待用户按 Enter 退出
        return 1;
    }
    // 显示读取的文本长度（字符数）
    std::wcout << L"Loaded " << text.length() << L" characters" << std::endl;

    /* ---- 初始化 TTS 引擎 ---- */
    TtsEngine engine; // 创建引擎实例（此时 COM 尚未初始化）
    if (!engine.Initialize()) // 初始化 COM 和 SAPI 语音引擎
    {
        std::cerr << "[Error] Failed to initialize TTS engine" << std::endl;
        std::cout << "\nPress Enter to exit..." << std::endl;
        std::cin.get();
        return 1;
    }

    /* ---- 配置语音参数 ---- */
    engine.SetRate(0);     // 设置语速为正常（范围 -10 ~ 10）
    engine.SetVolume(100); // 设置音量为最大（范围 0 ~ 100）

    /* ---- 执行文字转语音并保存文件 ---- */
    std::cout << "\nConverting text to speech..." << std::endl;
    if (engine.SpeakToFile(text, outputFile))
    {
        // 转换成功
        std::cout << "\nConversion complete!" << std::endl;
        std::wcout << L"Output file: " << outputFile << std::endl;
    }
    else
    {
        // 转换失败
        std::cerr << "\n[Error] Conversion failed" << std::endl;
    }

    /* ---- 等待用户按 Enter 退出 ---- */
    std::cout << "\nPress Enter to exit..." << std::endl;
    std::cin.get(); // 消耗缓冲区中残留的换行符
    return 0;       // 程序正常退出
}

// 压制 C++17 弃用警告（SAPI 使用了一些旧式 COM API）
#define _SILENCE_ALL_CXX17_DEPRECATION_WARNINGS 1
// 压制 MS 扩展弃用警告
#define _SILENCE_ALL_MS_EXT_DEPRECATION_WARNINGS 1
// 禁用 C4996 警告（安全 CRT 函数相关）
#pragma warning(disable : 4996)

// 引入引擎类头文件
#include "TtsEngine.h"
// 提供 std::wcerr / std::wcout 等宽字符控制台输出
#include <iostream>
// SAPI 语音 API 主头文件，提供 ISpVoice 接口
#include <sapi.h>
// SAPI 辅助函数，提供 SPBindToFile 等
#include <sphelper.h>
// 提供 std::wstring 支持
#include <string>
// 提供 std::vector 用于动态缓冲区
#include <vector>

// 链接 ole32.lib（COM 基础库，提供 CoInitializeEx / CoUninitialize）
#pragma comment(lib, "ole32.lib")
// 链接 sapi.lib（SAPI 语音库）
#pragma comment(lib, "sapi.lib")

/**
 * @brief 构造函数：初始化成员变量
 *
 * 将 initialized_ 设为 false 表示 COM 尚未初始化，
 * pVoice_ 和 pStream_ 均置为 nullptr，表示尚未创建任何对象。
 */
TtsEngine::TtsEngine()
    : initialized_(false)    // COM 未初始化
    , pVoice_(nullptr)       // 语音引擎指针置空
    , pStream_(nullptr)      // 输出流指针置空
{
    // 构造函数体为空，实际初始化在 Initialize() 中完成
}

/**
 * @brief 析构函数：释放所有资源
 *
 * 先调用 Cleanup() 释放语音引擎和流对象，
 * 如果之前初始化了 COM，则调用 CoUninitialize() 卸载 COM 库。
 */
TtsEngine::~TtsEngine()
{
    Cleanup();               // 释放语音引擎和流对象
    // 如果之前成功初始化了 COM，则需要卸载
    if (initialized_)
    {
        ::CoUninitialize();  // 卸载当前线程的 COM 库
    }
}

/**
 * @brief 选择一个可用的语音（优先中文，其次任意语音）
 * @param pVoice  ISpVoice 语音引擎对象指针
 * @return bool   true=成功选择了语音, false=无可用语音
 *
 * 遍历系统中已安装的所有 TTS 语音，
 * 优先选择语言 ID 为 804（简体中文）的语音，
 * 如果没有中文语音则回退到任意可用语音。
 */
static bool SelectVoice(ISpVoice* pVoice)
{
    // 用于枚举语音的 COM 对象指针
    ISpObjectTokenCategory* pCategory = nullptr; // 语音分类（如 "Voices"）
    IEnumSpObjectTokens* pEnum = nullptr;         // 语音令牌枚举器

    // 创建语音令牌分类对象，用于获取系统中所有已安装的语音列表
    HRESULT hr = ::CoCreateInstance(
        CLSID_SpObjectTokenCategory, // COM 类 ID：语音令牌分类
        nullptr,                     // 聚合：无
        CLSCTX_ALL,                  // 上下文：所有（进程内/进程外）
        IID_ISpObjectTokenCategory,  // 接口 ID
        reinterpret_cast<void**>(&pCategory) // 输出指针
    );
    // 创建失败则直接返回 false
    if (FAILED(hr))
        return false;

    // 设置分类为语音（Voices），准备枚举语音
    hr = pCategory->SetId(SPCAT_VOICES, FALSE);
    // 枚举所有语音令牌，枚举条件为空（获取所有语音）
    if (FAILED(hr) || FAILED(pCategory->EnumTokens(nullptr, nullptr, &pEnum)))
    {
        pCategory->Release(); // 释放分类对象
        return false;
    }

    // 获取系统中语音的总数
    ULONG count = 0;
    pEnum->GetCount(&count);

    // 如果系统没有安装任何语音，给出警告并返回失败
    if (count == 0)
    {
        pEnum->Release();      // 释放枚举器
        pCategory->Release();  // 释放分类对象
        std::wcerr << L"[Warning] No TTS voices found" << std::endl;
        return false;
    }

    // 遍历所有语音，优先选择中文语音（语言 ID = 804）
    bool selected = false;              // 是否已成功选中语音
    ISpObjectToken* pFallback = nullptr; // 回退用语音（第一个非中文语音）

    for (ULONG i = 0; i < count; i++)
    {
        // 获取下一个语音令牌
        ISpObjectToken* pToken = nullptr;
        if (FAILED(pEnum->Next(1, &pToken, nullptr)))
            break; // 获取失败则停止遍历

        // 读取该语音的语言属性
        LPWSTR lang = nullptr;
        if (SUCCEEDED(pToken->GetStringValue(L"Language", &lang)))
        {
            // 语言 ID 格式：低 8 位是主语言，0x0804 = 简体中文
            if (std::wstring(lang) == L"804")
            {
                // 找到中文语音，设置为当前语音
                HRESULT hr = pVoice->SetVoice(pToken);
                if (SUCCEEDED(hr))
                {
                    selected = true;              // 选中成功
                    ::CoTaskMemFree(lang);        // 释放语言字符串内存
                    pToken->Release();            // 释放令牌（SetVoice 已持有引用）
                    break;                        // 停止遍历
                }
            }
            ::CoTaskMemFree(lang); // 释放语言字符串内存
        }

        // 保存第一个非中文语音作为回退选项
        if (!pFallback)
            pFallback = pToken; // 第一次遇到非中文语音，暂存
        else
            pToken->Release();  // 已有回退语音，释放当前令牌
    }

    // 如果没有选中中文语音，但有回退语音，则使用回退语音
    if (!selected && pFallback)
    {
        selected = SUCCEEDED(pVoice->SetVoice(pFallback));
        pFallback->Release(); // 释放回退语音令牌
    }

    // 释放枚举器和分类对象
    pEnum->Release();
    pCategory->Release();
    return selected;
}

/**
 * @brief 初始化 COM 和 SAPI 语音引擎
 * @return bool  true=初始化成功, false=失败
 *
 * 执行步骤：
 * 1. CoInitializeEx 初始化 COM 库（多线程模式）
 * 2. CoCreateInstance 创建 SAPI 语音对象
 * 3. SelectVoice 选择中文语音
 */
bool TtsEngine::Initialize()
{
    // 初始化 COM 库，使用多线程模式（COINIT_MULTITHREADED）
    HRESULT hr = ::CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(hr))
    {
        // COM 初始化失败，打印错误信息
        std::cerr << "[Error] COM initialization failed" << std::endl;
        return false;
    }
    initialized_ = true; // 标记 COM 已初始化，析构时需卸载

    // 创建 SAPI 语音引擎对象（ISpVoice 接口）
    hr = ::CoCreateInstance(
        CLSID_SpVoice,          // SAPI 语音引擎的类 ID
        nullptr,                // 聚合：无
        CLSCTX_ALL,             // 上下文：所有
        IID_ISpVoice,           // 请求 ISpVoice 接口
        reinterpret_cast<void**>(&pVoice_) // 输出语音引擎指针
    );
    if (FAILED(hr))
    {
        // SAPI 创建失败，打印 HRESULT 错误码
        std::wcerr << L"[Error] Failed to create SAPI voice engine (0x"
                   << std::hex << hr << L")" << std::endl;
        return false;
    }

    // 选择系统中安装的语音（优先中文）作为当前语音
    SelectVoice(pVoice_);

    return true;
}

/**
 * @brief 裁掉 WAV 音频末尾的杂音/SAPI 元数据
 * @param wavPath  WAV 文件路径
 *
 * SAPI 在写入完音频数据后，有时会在文件末尾附加一些
 * 非音频的元数据或噪声。此函数从文件末尾开始反向扫描，
 * 找到最后一长段静音的起始位置，将文件截断到该位置。
 *
 * 静音判定阈值：|采样值| < 80
 * 最小连续静音采样数：400（约 25ms@16kHz）
 * 至少保留 1 秒音频数据不被裁掉。
 */
static void TrimTrailingNoise(const std::wstring& wavPath)
{
    // 以读写方式打开 WAV 文件
    HANDLE hFile = ::CreateFileW(
        wavPath.c_str(),        // 文件路径
        GENERIC_READ | GENERIC_WRITE, // 读写权限
        0,                      // 不共享
        nullptr,                // 默认安全属性
        OPEN_EXISTING,          // 必须已存在
        FILE_ATTRIBUTE_NORMAL,  // 普通文件
        nullptr                 // 无模板
    );
    if (hFile == INVALID_HANDLE_VALUE)
        return; // 打不开文件就不处理

    // 获取文件大小，小于 46 字节不可能是有效 WAV
    DWORD fileSize = ::GetFileSize(hFile, nullptr);
    if (fileSize < 46)
    {
        ::CloseHandle(hFile);
        return;
    }

    // 读取 WAV 头部 46 字节（包含 RIFF + fmt + data 头）
    char header[46];
    DWORD read = 0;
    if (!::ReadFile(hFile, header, 46, &read, nullptr) || read < 46)
    {
        ::CloseHandle(hFile);
        return;
    }

    // 验证 RIFF/WAVE 签名
    if (memcmp(header, "RIFF", 4) != 0 || memcmp(header + 8, "WAVE", 4) != 0)
    {
        ::CloseHandle(hFile);
        return;
    }

    // 扫描所有 chunk，定位 "data" chunk 的位置和大小
    DWORD dataOffset = 12; // 从偏移 12 开始（跳过 RIFF 头）
    DWORD dataSize = 0;
    while (dataOffset < fileSize - 8) // 至少还有 8 字节才能构成一个 chunk 头
    {
        // 定位到当前 chunk 开始位置
        ::SetFilePointer(hFile, dataOffset, nullptr, FILE_BEGIN);
        // 读取 chunk 头（4 字节 ID + 4 字节大小）
        char chunk[8];
        if (!::ReadFile(hFile, chunk, 8, &read, nullptr) || read < 8)
            break; // 读取失败则停止扫描

        // 解析 chunk 大小（小端序）
        DWORD chunkSize;
        memcpy(&chunkSize, chunk + 4, 4);

        // 如果找到 "data" chunk，记录其位置和大小
        if (memcmp(chunk, "data", 4) == 0)
        {
            dataOffset += 8;    // 跳过 data chunk 头，指向音频数据开始
            dataSize = chunkSize; // 音频数据大小
            break;
        }

        // 跳到下一个 chunk（对齐到 2 字节边界）
        dataOffset += 8 + chunkSize + (chunkSize % 2);
    }

    // 如果没有 data chunk 或大小为 0，无法处理
    if (dataSize == 0)
    {
        ::CloseHandle(hFile);
        return;
    }

    // 反向扫描参数：静音阈值和最小连续静音数
    const int silenceThreshold = 80;     // |采样值| < 80 视为静音
    const int minSilenceSamples = 400;   // 连续 400 个静音采样（约 25ms）

    // 每次反向读取的块大小（4KB）
    const DWORD scanBlock = 4096;
    // 音频数据的结束位置（字节偏移）
    DWORD audioEnd = dataOffset + dataSize;

    long long trimPos = audioEnd;  // 当前扫描位置（从末尾开始）
    int consecutiveSilent = 0;     // 连续静音采样计数

    // 分配读取缓冲区（可容纳 scanBlock/2 个 16 位采样）
    std::vector<short> buf(scanBlock / 2);

    // 从末尾向前扫描，至少保留 1 秒（16000 字节）的音频
    while (trimPos > (long long)dataOffset + 16000)
    {
        // 计算本次要读取的起始位置
        DWORD readStart = (DWORD)(trimPos - scanBlock);
        if (readStart < dataOffset)
            readStart = dataOffset; // 不能越过数据开始位置

        // 计算本次实际要读取的字节数
        DWORD bytesToRead = (DWORD)(trimPos - readStart);
        if (bytesToRead < 2)
            break; // 不足一个采样，停止

        // 定位文件指针并读取音频数据
        DWORD samplesToRead = bytesToRead / 2;
        ::SetFilePointer(hFile, readStart, nullptr, FILE_BEGIN);
        DWORD actualRead = 0;
        if (!::ReadFile(hFile, buf.data(), samplesToRead * 2, &actualRead, nullptr))
            break;

        // 从后向前遍历当前块中的每个采样
        DWORD samplesRead = actualRead / 2;
        for (int i = (int)samplesRead - 1; i >= 0; i--)
        {
            int absVal = abs(buf[i]); // 采样绝对值
            if (absVal < silenceThreshold)
            {
                consecutiveSilent++; // 静音，计数增加
            }
            else
            {
                consecutiveSilent = 0; // 非静音，重置计数
            }

            // 如果连续静音达到阈值，确定裁切点
            if (consecutiveSilent >= minSilenceSamples)
            {
                // 裁切位置：从 dataOffset 到当前采样（含）的字节数
                DWORD newDataSize = (DWORD)(readStart + i * 2 - dataOffset);
                newDataSize += 2; // 包含当前采样，不对齐问题
                // 新的文件总大小
                DWORD newFileSize = dataOffset + newDataSize;

                // 只有节省至少 50KB 时才裁切，避免误裁自然停顿
                if (newFileSize < fileSize - 51200)
                {
                    DWORD written2 = 0;

                    // 更新 data chunk 大小
                    ::SetFilePointer(hFile, dataOffset - 4, nullptr, FILE_BEGIN);
                    ::WriteFile(hFile, &newDataSize, 4, &written2, nullptr);

                    // 更新 RIFF chunk 大小
                    DWORD newRiffSize = newFileSize - 8;
                    ::SetFilePointer(hFile, 4, nullptr, FILE_BEGIN);
                    ::WriteFile(hFile, &newRiffSize, 4, &written2, nullptr);

                    // 截断文件到新大小
                    ::SetFilePointer(hFile, newFileSize, nullptr, FILE_BEGIN);
                    ::SetEndOfFile(hFile);
                }
                // 完成处理后关闭文件并返回
                ::CloseHandle(hFile);
                return;
            }
        }
        // 移动到下一块继续扫描
        trimPos = readStart;
    }

    // 没有找到需要裁切的尾部杂音，关闭文件
    ::CloseHandle(hFile);
}

/**
 * @brief 裁掉 WAV 音频开头的静音
 * @param wavPath  WAV 文件路径
 *
 * SAPI 生成的音频开头通常含有约 135ms 的静音（语音引擎初始化延迟），
 * 这会导致听感上第一个字不清晰。此函数从开头开始正向扫描，
 * 找到第一个有效语音采样后，裁掉前面的静音，仅保留约 10ms 的过渡。
 *
 * 实现方式：
 * 1. 扫描找到第一个 |采样值| > 80 的位置
 * 2. 向前保留 160 个采样（~10ms）作为过渡
 * 3. 读取裁切后的音频数据，覆盖写入原位置
 * 4. 更新文件头并截断文件
 */
static void TrimLeadingSilence(const std::wstring& wavPath)
{
    // 以读写方式打开 WAV 文件
    HANDLE hFile = ::CreateFileW(
        wavPath.c_str(),
        GENERIC_READ | GENERIC_WRITE,
        0,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr
    );
    if (hFile == INVALID_HANDLE_VALUE)
        return;

    // 获取文件大小，小于 46 字节不可能是有效 WAV
    DWORD fileSize = ::GetFileSize(hFile, nullptr);
    if (fileSize < 46)
    {
        ::CloseHandle(hFile);
        return;
    }

    // 扫描所有 chunk，找到 "data" chunk
    DWORD dataOffset = 12; // 跳过 RIFF 头
    DWORD dataSize = 0;
    DWORD read = 0;
    while (dataOffset < fileSize - 8)
    {
        ::SetFilePointer(hFile, dataOffset, nullptr, FILE_BEGIN);
        char chunk[8];
        if (!::ReadFile(hFile, chunk, 8, &read, nullptr) || read < 8)
            break;

        DWORD chunkSize;
        memcpy(&chunkSize, chunk + 4, 4);

        if (memcmp(chunk, "data", 4) == 0)
        {
            dataOffset += 8; // 指向音频数据的实际起始位置
            dataSize = chunkSize;
            break;
        }
        // 跳到下一个 chunk（对齐到 2 字节）
        dataOffset += 8 + chunkSize + (chunkSize % 2);
    }

    // 如果没有数据或数据太少，直接返回
    if (dataSize == 0 || dataSize < 200)
    {
        ::CloseHandle(hFile);
        return;
    }

    // 扫描参数：静音阈值和过渡采样数
    const int silenceThreshold = 80;  // 与 TrimTrailingNoise 保持一致
    const int guardSamples = 160;     // 保留约 10ms 的过渡静音（16000Hz）

    // 读取开头的一块音频数据（最多 64KB）
    const DWORD scanBlock = 65536;
    DWORD scanSize = min(dataSize, scanBlock);
    std::vector<short> buf(scanSize / 2);

    DWORD bytesToRead = min(dataSize, scanBlock);
    ::SetFilePointer(hFile, dataOffset, nullptr, FILE_BEGIN);
    DWORD actualRead = 0;
    if (!::ReadFile(hFile, buf.data(), bytesToRead, &actualRead, nullptr))
    {
        ::CloseHandle(hFile);
        return;
    }

    // 正向扫描，找到第一个有效语音采样
    DWORD samplesRead = actualRead / 2;
    DWORD firstVoiceSample = samplesRead; // 默认：整个块都是静音

    for (DWORD i = 0; i < samplesRead; i++)
    {
        if (abs(buf[i]) > silenceThreshold)
        {
            firstVoiceSample = i; // 找到第一个非静音采样
            break;
        }
    }

    // 如果整个块都没有检测到语音，不处理
    if (firstVoiceSample >= samplesRead)
    {
        ::CloseHandle(hFile);
        return;
    }

    // 保留一小段过渡静音，使语音起始不那么突兀
    if (firstVoiceSample > guardSamples)
        firstVoiceSample -= guardSamples; // 向前偏移保留过渡
    else
        firstVoiceSample = 0; // 不足过渡则从开头开始保留

    // 如果裁切的静音少于 800 个采样（50ms），不值得处理
    if (firstVoiceSample < 800)
    {
        ::CloseHandle(hFile);
        return;
    }

    // 计算裁切量和新的文件大小
    DWORD trimBytes = firstVoiceSample * 2;           // 要删除的字节数
    DWORD newDataSize = dataSize - trimBytes;           // 新的音频数据大小
    DWORD newFileSize = dataOffset + newDataSize;       // 新的文件大小
    DWORD remainingBytes = dataSize - trimBytes;        // 剩余的音频数据字节数

    // 分配缓冲区并读取裁切后剩余的音频数据
    std::vector<char> audioData(remainingBytes);
    ::SetFilePointer(hFile, dataOffset + trimBytes, nullptr, FILE_BEGIN);
    DWORD actualRead2 = 0;
    if (!::ReadFile(hFile, audioData.data(), remainingBytes, &actualRead2, nullptr) ||
        actualRead2 != remainingBytes)
    {
        ::CloseHandle(hFile);
        return;
    }

    // 将剩余的音频数据覆盖写入原 dataOffset 位置
    ::SetFilePointer(hFile, dataOffset, nullptr, FILE_BEGIN);
    DWORD written = 0;
    if (!::WriteFile(hFile, audioData.data(), remainingBytes, &written, nullptr) ||
        written != remainingBytes)
    {
        ::CloseHandle(hFile);
        return;
    }

    // 更新 WAV 文件头中的 data chunk 大小
    ::SetFilePointer(hFile, dataOffset - 4, nullptr, FILE_BEGIN);
    ::WriteFile(hFile, &newDataSize, 4, &written, nullptr);

    // 更新 RIFF chunk 大小（文件总大小 - 8）
    DWORD newRiffSize = newFileSize - 8;
    ::SetFilePointer(hFile, 4, nullptr, FILE_BEGIN);
    ::WriteFile(hFile, &newRiffSize, 4, &written, nullptr);

    // 截断文件到新的总大小
    ::SetFilePointer(hFile, newFileSize, nullptr, FILE_BEGIN);
    ::SetEndOfFile(hFile);

    ::CloseHandle(hFile);
}

/**
 * @brief 修复 SAPI 写入 WAV 文件后的文件头
 * @param wavPath  WAV 文件路径
 * @return bool  true=修复成功, false=修复失败
 *
 * SAPI 有时会在 WAV 文件中出现两个问题：
 * 1. RIFF chunk size 和 data chunk size 被设为 0
 * 2. 在有效 WAV 数据之后附加了无用元数据
 *
 * 此函数扫描文件中的所有有效 chunk，计算正确的大小，
 * 并在必要时截断文件以去除多余数据。
 */
static bool FixWavHeader(const std::wstring& wavPath)
{
    // 以读写方式打开 WAV 文件
    HANDLE hFile = ::CreateFileW(
        wavPath.c_str(),
        GENERIC_READ | GENERIC_WRITE,
        0,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr
    );
    if (hFile == INVALID_HANDLE_VALUE)
        return false;

    // 获取文件大小，至少 12 字节（RIFF 头）
    DWORD fileSize = ::GetFileSize(hFile, nullptr);
    if (fileSize < 12)
    {
        ::CloseHandle(hFile);
        return false;
    }

    bool success = false; // 标记修复是否成功

    // 读取前 12 字节，验证 RIFF/WAVE 签名
    char header[12];
    DWORD read = 0;
    if (::ReadFile(hFile, header, 12, &read, nullptr) && read == 12)
    {
        // 检查是否以 "RIFF" 开头且含有 "WAVE" 标识
        if (memcmp(header, "RIFF", 4) == 0 && memcmp(header + 8, "WAVE", 4) == 0)
        {
            // 从偏移 12 开始扫描所有有效 chunk
            ::SetFilePointer(hFile, 12, nullptr, FILE_BEGIN);
            char chunk[8];          // chunk 头缓冲区
            DWORD lastChunkEnd = 12; // 最后一个有效 chunk 的结束位置
            DWORD dataOffset = 0;    // "data" chunk 的数据开始位置
            DWORD rawDataChunkSize = 0; // 原始 data chunk 头中记录的大小

            // 循环读取每个 chunk 的头
            while (::ReadFile(hFile, chunk, 8, &read, nullptr) && read == 8)
            {
                // 解析 chunk 大小（小端序 4 字节）
                DWORD chunkSize;
                memcpy(&chunkSize, chunk + 4, 4);

                // 获取当前文件指针位置（刚读完 8 字节 chunk 头之后）
                DWORD currentPos = ::SetFilePointer(hFile, 0, nullptr, FILE_CURRENT);

                // 处理 "data" chunk 的特殊情况
                if (memcmp(chunk, "data", 4) == 0)
                {
                    dataOffset = currentPos;      // 记录 audio 数据偏移
                    rawDataChunkSize = chunkSize; // 记录原始大小

                    // SAPI 有时将 data chunk size 写为 0，但实际数据存在
                    if (chunkSize == 0)
                    {
                        // 用文件剩余字节数作为实际数据大小
                        chunkSize = fileSize - currentPos;
                    }
                }

                // 计算当前 chunk 的结束位置（对齐到 2 字节边界）
                DWORD chunkEnd = currentPos + chunkSize + (chunkSize % 2);
                lastChunkEnd = chunkEnd; // 更新"最后 chunk 结束位置"

                // 跳过当前 chunk 的全部数据，定位到下一个 chunk 开始
                ::SetFilePointer(hFile, chunkEnd, nullptr, FILE_BEGIN);
            }

            // 如果 data chunk 原始大小为 0，需要将正确的大小写回文件头
            if (dataOffset > 0 && rawDataChunkSize == 0)
            {
                DWORD actualDataSize = lastChunkEnd - dataOffset;
                ::SetFilePointer(hFile, dataOffset - 4, nullptr, FILE_BEGIN);
                DWORD written = 0;
                ::WriteFile(hFile, &actualDataSize, 4, &written, nullptr);
            }

            // 计算有效的 RIFF 大小（从偏移 8 到最后一个 chunk 结束）
            DWORD validRiffSize = lastChunkEnd - 8;

            // 情况 A：文件末尾有额外垃圾数据
            if (validRiffSize + 8 < fileSize)
            {
                // 更新 RIFF chunk size
                DWORD riffSize = validRiffSize;
                ::SetFilePointer(hFile, 4, nullptr, FILE_BEGIN);
                DWORD written = 0;
                ::WriteFile(hFile, &riffSize, 4, &written, nullptr);

                // 更新 data chunk size
                if (dataOffset > 0)
                {
                    DWORD dataSize = lastChunkEnd - dataOffset;
                    ::SetFilePointer(hFile, dataOffset - 4, nullptr, FILE_BEGIN);
                    ::WriteFile(hFile, &dataSize, 4, &written, nullptr);
                }

                // 截断文件到有效数据的结尾
                ::SetFilePointer(hFile, validRiffSize + 8, nullptr, FILE_BEGIN);
                ::SetEndOfFile(hFile);
                success = true;
            }
            else // 情况 B：没有垃圾数据，只需修正大小
            {
                // 更新 RIFF 大小为文件实际大小 - 8
                DWORD riffSize = fileSize - 8;
                ::SetFilePointer(hFile, 4, nullptr, FILE_BEGIN);
                DWORD written = 0;
                ::WriteFile(hFile, &riffSize, 4, &written, nullptr);

                // 重新扫描找到 data chunk，更新其大小
                ::SetFilePointer(hFile, 12, nullptr, FILE_BEGIN);
                char chunk2[8];
                while (::ReadFile(hFile, chunk2, 8, &read, nullptr) && read == 8)
                {
                    DWORD chunkSize;
                    memcpy(&chunkSize, chunk2 + 4, 4);

                    if (memcmp(chunk2, "data", 4) == 0)
                    {
                        // data chunk 大小 = 文件剩余字节数
                        DWORD currentPos = ::SetFilePointer(hFile, 0, nullptr, FILE_CURRENT);
                        DWORD dataSize = fileSize - currentPos;
                        // 回退 4 字节写入正确大小
                        ::SetFilePointer(hFile, -4, nullptr, FILE_CURRENT);
                        ::WriteFile(hFile, &dataSize, 4, &written, nullptr);
                        break;
                    }

                    // 跳过非 data chunk
                    DWORD skip = chunkSize + (chunkSize % 2);
                    ::SetFilePointer(hFile, skip, nullptr, FILE_CURRENT);
                }
                success = true;
            }
        }
    }

    // 关闭文件并返回修复结果
    ::CloseHandle(hFile);
    return success;
}

/**
 * @brief 将文本转换为语音并保存为 WAV 文件
 * @param text           要朗读的文本（UTF-16 宽字符串）
 * @param outputWavPath  输出 WAV 文件的路径
 * @return bool  true=转换成功, false=失败
 *
 * 完整处理流程：
 * 1. SPBindToFile — 创建 WAV 文件，指定 PCM 16kHz 16bit 单声道格式
 * 2. pVoice_->SetOutput — 将语音输出路由到文件流
 * 3. pVoice_->Speak — 执行语音合成
 * 4. FixWavHeader — 修复 SAPI 可能写错的 RIFF/data chunk size
 * 5. TrimTrailingNoise — 去除 SAPI 附加的尾部杂音
 * 6. TrimLeadingSilence — 去除 SAPI 开头的静音延迟
 * 7. FixWavHeader — 再次修复文件头（因裁切后大小改变）
 */
bool TtsEngine::SpeakToFile(const std::wstring& text, const std::wstring& outputWavPath)
{
    // 检查语音引擎是否已初始化
    if (!pVoice_)
    {
        std::wcerr << L"[Error] Voice engine not initialized" << std::endl;
        return false;
    }

    /* ---- 步骤 1：设置 WAV 格式参数 ---- */
    // WAVEFORMATEX 结构体描述音频格式
    WAVEFORMATEX wfx = {};
    wfx.wFormatTag      = WAVE_FORMAT_PCM;  // PCM 编码（无压缩）
    wfx.nChannels       = 1;                // 单声道
    wfx.nSamplesPerSec  = 16000;            // 16kHz 采样率
    wfx.wBitsPerSample  = 16;               // 16 位量化
    // 每个采样帧的字节数 = 通道数 × 位深 / 8
    wfx.nBlockAlign     = wfx.nChannels * wfx.wBitsPerSample / 8;
    // 每秒字节数 = 采样率 × 块对齐
    wfx.nAvgBytesPerSec = wfx.nSamplesPerSec * wfx.nBlockAlign;

    // 绑定 SAPI 输出到 WAV 文件（指定 PCM 格式）
    HRESULT hr = ::SPBindToFile(
        outputWavPath.c_str(), // 输出文件路径
        SPFM_CREATE_ALWAYS,    // 总是创建新文件（覆盖已有）
        &pStream_,             // 输出流对象
        &SPDFID_WaveFormatEx,  // WAV 格式标识
        &wfx,                  // 格式参数
        SPFEI_ALL_EVENTS       // 接收所有事件
    );
    if (FAILED(hr))
    {
        // 用指定格式失败了，改用系统默认格式重试
        std::wcout << L"  (retrying with default format...)" << std::endl;
        hr = ::SPBindToFile(
            outputWavPath.c_str(),
            SPFM_CREATE_ALWAYS,
            &pStream_,
            nullptr,  // 无指定格式 ID
            nullptr,  // 无格式参数（使用系统默认）
            SPFEI_ALL_EVENTS
        );
    }
    if (FAILED(hr))
    {
        // 重试仍然失败，报告错误
        std::wcerr << L"[Error] Failed to create output file (0x"
                   << std::hex << hr << L")" << std::endl;
        return false;
    }

    /* ---- 步骤 2：路由语音输出到文件 ---- */
    hr = pVoice_->SetOutput(pStream_, TRUE);
    if (FAILED(hr))
    {
        std::wcerr << L"[Error] Failed to set voice output (0x"
                   << std::hex << hr << L")" << std::endl;
        // 清理已创建的流对象
        pStream_->Close();
        pStream_->Release();
        pStream_ = nullptr;
        return false;
    }

    /* ---- 步骤 3：执行语音合成 ---- */
    // SPF_DEFAULT = 同步朗读，函数直到朗读完毕才返回
    hr = pVoice_->Speak(text.c_str(), SPF_DEFAULT, nullptr);
    if (FAILED(hr))
    {
        std::wcerr << L"[Error] Speech synthesis failed (0x"
                   << std::hex << hr << L")" << std::endl;
        if (pStream_)
        {
            pStream_->Close();
            pStream_->Release();
            pStream_ = nullptr;
        }
        return false;
    }

    // 等待语音引擎完成所有处理（包括音频数据写入）
    pVoice_->WaitUntilDone(INFINITE);

    /* ---- 步骤 4：关闭流，刷新数据到文件 ---- */
    if (pStream_)
    {
        pStream_->Close();    // 关闭流，确保所有数据写入文件
        pStream_->Release();  // 释放 COM 引用
        pStream_ = nullptr;
    }

    /* ---- 步骤 5-8：后处理 ---- */
    // 修复 SAPI 可能写错的文件头（RIFF/data size 可能为 0）
    if (!FixWavHeader(outputWavPath))
    {
        std::wcerr << L"[Warning] Could not verify/fix WAV header" << std::endl;
    }

    // 裁掉 SAPI 附加在文件末尾的杂音/元数据
    TrimTrailingNoise(outputWavPath);

    // 裁掉 SAPI 在开头插入的约 135ms 静音
    TrimLeadingSilence(outputWavPath);

    // 裁切后文件头已变化，再次修复确保正确
    FixWavHeader(outputWavPath);

    return true;
}

/**
 * @brief 设置语速
 * @param rate  语速值（-10 ~ 10），0 为正常语速
 */
void TtsEngine::SetRate(int rate)
{
    if (pVoice_) // 确保语音引擎已初始化
    {
        pVoice_->SetRate(rate); // 调用 SAPI 设置语速
    }
}

/**
 * @brief 设置音量
 * @param volume  音量值（0 ~ 100），100 为最大音量
 */
void TtsEngine::SetVolume(WORD volume)
{
    if (pVoice_) // 确保语音引擎已初始化
    {
        pVoice_->SetVolume(volume); // 调用 SAPI 设置音量
    }
}

/**
 * @brief 释放语音引擎和流对象
 *
 * 清理顺序：
 * 1. 先关闭并释放流对象（确保音频数据已写入文件）
 * 2. 再释放语音引擎对象
 * 3. 所有指针置为 nullptr
 */
void TtsEngine::Cleanup()
{
    // 先释放流（如果存在），确保音频数据刷新到文件
    if (pStream_)
    {
        pStream_->Close();    // 关闭流
        pStream_->Release();  // 释放 COM 引用
        pStream_ = nullptr;   // 指针置空
    }
    // 再释放语音引擎
    if (pVoice_)
    {
        pVoice_->Release(); // 释放 COM 引用
        pVoice_ = nullptr;  // 指针置空
    }
}

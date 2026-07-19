#pragma once // 防止头文件被重复包含
#include <string>   // 提供 std::wstring 支持
#include <Windows.h> // 提供 WORD 等 Windows 数据类型

/**
 * @brief TTS 引擎封装类
 *
 * 封装 Windows SAPI（Speech API）的语音合成功能，
 * 将文本转换为语音并直接保存为 WAV 文件。
 *
 * 主要流程：
 * 1. Initialize()  — 初始化 COM 和 SAPI 语音引擎
 * 2. SpeakToFile() — 将文本转语音并写入 WAV 文件
 * 3. SetRate()     — 调节语速
 * 4. SetVolume()   — 调节音量
 */
class TtsEngine
{
public:
    /**
     * @brief 构造函数：初始化成员变量
     *
     * 将 initialized_ 设为 false，语音引擎和流指针均置为 nullptr。
     * 真正的初始化工作在 Initialize() 中完成。
     */
    TtsEngine();

    /**
     * @brief 析构函数：释放所有资源
     *
     * 调用 Cleanup() 释放语音引擎和流对象，
     * 如果 COM 已初始化则调用 CoUninitialize() 卸载 COM。
     */
    ~TtsEngine();

    /**
     * @brief 初始化 COM 和 SAPI 语音引擎
     * @return bool  true=成功, false=失败
     *
     * 执行步骤：
     * 1. CoInitializeEx(nullptr, COINIT_MULTITHREADED) — 初始化 COM
     * 2. CoCreateInstance(CLSID_SpVoice) — 创建 SAPI 语音对象
     * 3. SelectVoice() — 自动选择中文语音
     */
    bool Initialize();

    /**
     * @brief 将文本转换为语音并保存到 WAV 文件
     * @param text           要朗读的文本内容（UTF-16 宽字符串）
     * @param outputWavPath  输出的 WAV 文件路径
     * @return bool  true=成功, false=失败
     *
     * 执行步骤：
     * 1. SPBindToFile — 创建 WAV 文件输出流
     * 2. pVoice_->SetOutput — 将语音输出路由到文件流
     * 3. pVoice_->Speak — 执行语音合成
     * 4. FixWavHeader — 修复 SAPI 可能写错的文件头
     * 5. TrimTrailingNoise — 去除尾部杂音
     * 6. TrimLeadingSilence — 去除开头静音
     */
    bool SpeakToFile(const std::wstring& text, const std::wstring& outputWavPath);

    /**
     * @brief 设置语速
     * @param rate  语速值，范围 -10 到 10，0 为正常语速
     *
     * 底层调用 ISpVoice::SetRate()。
     * 负数减慢语速，正数加快语速。
     */
    void SetRate(int rate);

    /**
     * @brief 设置音量
     * @param volume  音量值，范围 0 到 100，100 为最大音量
     *
     * 底层调用 ISpVoice::SetVolume()。
     * 0 为静音，100 为最大音量。
     */
    void SetVolume(WORD volume);

private:
    /** @brief COM 是否已成功初始化 */
    bool initialized_;
    /** @brief SAPI 语音引擎对象指针 */
    struct ISpVoice* pVoice_;
    /** @brief SAPI 输出流对象指针（绑定到 WAV 文件） */
    struct ISpStream* pStream_;

    /**
     * @brief 释放语音引擎和流对象
     *
     * 先关闭并释放流对象（刷新音频数据到文件），
     * 再释放语音引擎对象，最后将所有指针置 nullptr。
     */
    void Cleanup();
};

/**
 * @file DocumentRecovery.h
 * @brief 文档自动保存与崩溃恢复（恢复文件存储 + 自动保存调度）
 *
 * 设计约束：
 *   - 恢复文件与用户文档完全分离，自动保存绝不覆盖用户文件
 *   - 恢复文件沿用 .apr/.apr3d 的魔数 + CRC 完整性校验，损坏必须如实上报
 *   - 不依赖 Qt / GUI / GPU，时间由调用方以单调毫秒推进，便于确定性测试
 *   - ABI：PIMPL 隐藏实现，全部为非虚方法，头文件不含 STL 容器
 */
#pragma once

#include "FileIO/FileIOAPI.h"
#include "FileIO/AprSerializer.h"

#include <cstddef>
#include <cstdint>

namespace Fio
{
    /// 恢复文件检查结果（纯 POD，跨 DLL 安全）
    struct FILEIO_API RecoveryFileInfo
    {
        bool exists = false;    ///< 恢复文件是否存在
        bool intact = false;    ///< 内容是否通过魔数/长度/CRC 校验
        bool is3D = false;      ///< 是否为 3D 文档恢复文件
        char originalPath[1024] = { 0 };  ///< 对应的用户文档路径（未命名文档为空）
        char savedAt[64] = { 0 };         ///< 恢复文件写入时间（epoch 秒）
        char recoveryPath[1024] = { 0 };  ///< 恢复文件自身路径
        char errorMessage[512] = { 0 };   ///< intact=false 时的具体原因
    };

    // ----
    // ---- RecoveryStore —— 恢复文件落盘 / 检查 / 载入 / 清理 ----
    // ----
    class FILEIO_API RecoveryStore
    {
    public:
        RecoveryStore();
        ~RecoveryStore();

        RecoveryStore(const RecoveryStore&) = delete;
        RecoveryStore& operator=(const RecoveryStore&) = delete;

        /// 设置恢复文件目录（UTF-8）。空表示与用户文档同目录。
        void setRecoveryDirectory(const char* directoryUtf8);

        /// 读取当前恢复目录（返回实际写入长度）
        size_t recoveryDirectory(char* buffer, size_t bufferSize) const;

        /// 计算文档对应的恢复文件路径（不落盘）。
        /// @param documentKey 文档绝对路径；未命名文档请传稳定的会话 key
        /// @param is3D        true → .apr3d 恢复文件，false → .apr
        bool recoveryPathFor(const char* documentKey, bool is3D, char* buffer, size_t bufferSize) const;

        /// 计算恢复文件路径并确保父目录存在（供工作台用自身快照写出恢复文件前调用）
        bool prepareRecoveryPath(const char* documentKey, bool is3D, char* buffer, size_t bufferSize);

        /// 为已由工作台写出的恢复文件补写 manifest（原始路径/时间/格式）。
        /// 供无法直接提供 AprDocument 的 2D/3D 场景快照路径使用。
        SerializeResult writeRecoveryManifest(const char* documentKey, bool is3D);

        /// 写入恢复文件（原子写 + fsync，另写 manifest 记录原始路径/时间/格式）
        SerializeResult writeRecovery(const char* documentKey, bool is3D, const AprDocument& doc);

        /// 检查恢复文件是否可用：存在性 + manifest + 内容完整性
        SerializeResult inspectRecovery(const char* recoveryPath, RecoveryFileInfo& info) const;

        /// 载入恢复文件内容
        SerializeResult loadRecovery(const char* recoveryPath, AprDocument& doc) const;

        /// 清理恢复文件、manifest 与可能的备份副本；返回是否已彻底不存在
        bool discardRecovery(const char* recoveryPath) const;

        /// 枚举目录下全部恢复文件（启动时检测残留），visitor 收到恢复文件路径
        void forEachRecovery(const char* directoryUtf8, void (*visitor)(const char* path, void* ctx), void* ctx) const;

        /// 平台默认恢复目录（用户数据目录下 April/Recovery），返回是否成功
        static bool defaultRecoveryDirectory(char* buffer, size_t bufferSize);

    private:
        struct Impl;
        Impl* m_impl;
    };

    // ----
    // ---- AutoSaveScheduler —— 自动保存触发判定 ----
    // ----
    //
    // 只负责「此刻是否应该写恢复文件」的判定，不做任何 I/O，便于确定性测试。
    // 生产侧由 QTimer 周期调用 tick()，传入单调毫秒；写盘失败不清零计时。
    class FILEIO_API AutoSaveScheduler
    {
    public:
        AutoSaveScheduler();
        ~AutoSaveScheduler();

        AutoSaveScheduler(const AutoSaveScheduler&) = delete;
        AutoSaveScheduler& operator=(const AutoSaveScheduler&) = delete;

        /// 设置自动保存间隔（秒）；小于 5 秒按 5 秒处理，避免写盘风暴
        void setIntervalSeconds(int seconds);
        int intervalSeconds() const;

        void setEnabled(bool enabled);
        bool enabled() const;

        /// 推进时钟并判断是否应写恢复文件。
        /// 仅当「已启用 + 有未保存修改 + 距上次触发达到间隔」时返回 true。
        /// 无修改时会推进基准时间，避免长时间空闲后首次变脏立刻写盘。
        bool tick(long long nowMilliseconds, bool hasUnsavedChanges);

        /// 手动保存成功 / 恢复文件写入成功后重置计时
        void reset(long long nowMilliseconds);

    private:
        struct Impl;
        Impl* m_impl;
    };
}  // namespace Fio

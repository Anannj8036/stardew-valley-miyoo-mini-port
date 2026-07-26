/**
 * Miyoo Mini SigmaStar MI_AO playback backend for OpenAL Soft.
 *
 * This file is independently implemented against the SigmaStar MI_AO API.
 * It is distributed under OpenAL Soft's LGPL-2.0-or-later terms.
 */

#include "config.h"

#include "backends/miao.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <exception>
#include <functional>
#include <mutex>
#include <thread>

#include <mi_ao.h>

#include "AL/alc.h"
#include "albyte.h"
#include "alcmain.h"
#include "almalloc.h"
#include "alu.h"
#include "core/logging.h"
#include "threads.h"
#include "vector.h"

namespace {

constexpr char MiaoDeviceName[] = "Miyoo MI_AO";
constexpr MI_AUDIO_DEV MiaoDevice{0};
constexpr MI_AO_CHN MiaoChannel{0};
constexpr uint32_t MiaoSampleRate{48000};
constexpr uint32_t MiaoChannels{2};
constexpr uint32_t MiaoBytesPerSample{2};
constexpr uint32_t MiaoFrameBytes{MiaoChannels * MiaoBytesPerSample};
constexpr uint32_t MiaoMinimumPeriodFrames{128};
constexpr uint32_t MiaoMaximumPeriodFrames{2048};
constexpr uint32_t MiaoMinimumBufferPeriods{2};
constexpr uint32_t MiaoMaximumBufferPeriods{4};
constexpr MI_S32 MiaoSendTimeoutMs{5};
constexpr uint32_t MiaoFatalRecoveryThreshold{3};
constexpr uint32_t MiaoBackpressureRecoveryThreshold{20};
constexpr uint32_t MiaoQueueStallRecoveryMs{500};
constexpr uint32_t MiaoStartupAttempts{5};
constexpr uint32_t MiaoStartupRetryMs{20};
constexpr uint32_t MiaoRecoveryBackoffMs[]{20, 50, 100};

struct SvmmMiaoOutputStats {
    uint32_t abi_version;
    uint32_t struct_size;
    uint32_t active;
    uint32_t sample_rate;
    uint32_t period_frames;
    uint32_t buffer_frames;
    uint32_t target_queue_bytes;
    uint32_t hardware_total_bytes;
    uint32_t hardware_free_bytes;
    uint32_t hardware_busy_bytes;
    uint32_t mixer_cycles;
    uint32_t mixer_heartbeat_age_ms;
    uint32_t mixer_gap_last_ms;
    uint32_t mixer_gap_max_ms;
    uint32_t output_operations;
    uint32_t output_frames;
    uint32_t output_heartbeat_age_ms;
    uint32_t output_gap_last_ms;
    uint32_t output_gap_max_ms;
    uint32_t query_calls;
    uint32_t query_errors;
    uint32_t queue_waits;
    uint32_t queue_wait_ms;
    uint32_t send_calls;
    uint32_t send_retries;
    uint32_t send_nobuf;
    uint32_t send_busy;
    uint32_t send_errors;
    uint32_t recoveries;
    uint32_t recovery_failures;
    uint32_t clears;
    uint32_t channel_restarts;
    uint32_t consecutive_errors_peak;
    uint32_t disconnects;
};

struct SvmmMiaoOutputTelemetry {
#define SVMM_MIAO_ATOMIC(name) std::atomic<uint32_t> name{0}
    SVMM_MIAO_ATOMIC(active);
    SVMM_MIAO_ATOMIC(sample_rate);
    SVMM_MIAO_ATOMIC(period_frames);
    SVMM_MIAO_ATOMIC(buffer_frames);
    SVMM_MIAO_ATOMIC(target_queue_bytes);
    SVMM_MIAO_ATOMIC(hardware_total_bytes);
    SVMM_MIAO_ATOMIC(hardware_free_bytes);
    SVMM_MIAO_ATOMIC(hardware_busy_bytes);
    SVMM_MIAO_ATOMIC(mixer_cycles);
    SVMM_MIAO_ATOMIC(mixer_heartbeat_ms);
    SVMM_MIAO_ATOMIC(mixer_gap_last_ms);
    SVMM_MIAO_ATOMIC(mixer_gap_max_ms);
    SVMM_MIAO_ATOMIC(output_operations);
    SVMM_MIAO_ATOMIC(output_frames);
    SVMM_MIAO_ATOMIC(output_heartbeat_ms);
    SVMM_MIAO_ATOMIC(output_gap_last_ms);
    SVMM_MIAO_ATOMIC(output_gap_max_ms);
    SVMM_MIAO_ATOMIC(query_calls);
    SVMM_MIAO_ATOMIC(query_errors);
    SVMM_MIAO_ATOMIC(queue_waits);
    SVMM_MIAO_ATOMIC(queue_wait_ms);
    SVMM_MIAO_ATOMIC(send_calls);
    SVMM_MIAO_ATOMIC(send_retries);
    SVMM_MIAO_ATOMIC(send_nobuf);
    SVMM_MIAO_ATOMIC(send_busy);
    SVMM_MIAO_ATOMIC(send_errors);
    SVMM_MIAO_ATOMIC(recoveries);
    SVMM_MIAO_ATOMIC(recovery_failures);
    SVMM_MIAO_ATOMIC(clears);
    SVMM_MIAO_ATOMIC(channel_restarts);
    SVMM_MIAO_ATOMIC(consecutive_errors_peak);
    SVMM_MIAO_ATOMIC(disconnects);
#undef SVMM_MIAO_ATOMIC
};

SvmmMiaoOutputTelemetry SvmmMiaoOutput;

uint32_t miaoNowMs() noexcept
{
    const auto now = std::chrono::steady_clock::now().time_since_epoch();
    return static_cast<uint32_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(now).count());
}

void updateMaximum(std::atomic<uint32_t> &maximum, uint32_t value) noexcept
{
    uint32_t current{maximum.load(std::memory_order_relaxed)};
    while(current < value && !maximum.compare_exchange_weak(current, value,
        std::memory_order_relaxed, std::memory_order_relaxed)) { }
}

void recordGap(std::atomic<uint32_t> &heartbeat,
    std::atomic<uint32_t> &last, std::atomic<uint32_t> &maximum,
    uint32_t now) noexcept
{
    const uint32_t previous{heartbeat.exchange(now, std::memory_order_relaxed)};
    if(previous == 0)
        return;
    const uint32_t gap{now - previous};
    last.store(gap, std::memory_order_relaxed);
    updateMaximum(maximum, gap);
}

void recordMixerCycle() noexcept
{
    recordGap(SvmmMiaoOutput.mixer_heartbeat_ms,
        SvmmMiaoOutput.mixer_gap_last_ms, SvmmMiaoOutput.mixer_gap_max_ms,
        miaoNowMs());
    SvmmMiaoOutput.mixer_cycles.fetch_add(1, std::memory_order_relaxed);
}

void recordOutput(uint32_t frames) noexcept
{
    recordGap(SvmmMiaoOutput.output_heartbeat_ms,
        SvmmMiaoOutput.output_gap_last_ms, SvmmMiaoOutput.output_gap_max_ms,
        miaoNowMs());
    SvmmMiaoOutput.output_operations.fetch_add(1, std::memory_order_relaxed);
    SvmmMiaoOutput.output_frames.fetch_add(frames, std::memory_order_relaxed);
}

void resetTelemetry(uint32_t periodFrames, uint32_t bufferFrames,
    uint32_t targetQueueBytes) noexcept
{
#define SVMM_MIAO_RESET(name) SvmmMiaoOutput.name.store(0, std::memory_order_relaxed)
    SVMM_MIAO_RESET(active);
    SVMM_MIAO_RESET(hardware_total_bytes);
    SVMM_MIAO_RESET(hardware_free_bytes);
    SVMM_MIAO_RESET(hardware_busy_bytes);
    SVMM_MIAO_RESET(mixer_cycles);
    SVMM_MIAO_RESET(mixer_heartbeat_ms);
    SVMM_MIAO_RESET(mixer_gap_last_ms);
    SVMM_MIAO_RESET(mixer_gap_max_ms);
    SVMM_MIAO_RESET(output_operations);
    SVMM_MIAO_RESET(output_frames);
    SVMM_MIAO_RESET(output_heartbeat_ms);
    SVMM_MIAO_RESET(output_gap_last_ms);
    SVMM_MIAO_RESET(output_gap_max_ms);
    SVMM_MIAO_RESET(query_calls);
    SVMM_MIAO_RESET(query_errors);
    SVMM_MIAO_RESET(queue_waits);
    SVMM_MIAO_RESET(queue_wait_ms);
    SVMM_MIAO_RESET(send_calls);
    SVMM_MIAO_RESET(send_retries);
    SVMM_MIAO_RESET(send_nobuf);
    SVMM_MIAO_RESET(send_busy);
    SVMM_MIAO_RESET(send_errors);
    SVMM_MIAO_RESET(recoveries);
    SVMM_MIAO_RESET(recovery_failures);
    SVMM_MIAO_RESET(clears);
    SVMM_MIAO_RESET(channel_restarts);
    SVMM_MIAO_RESET(consecutive_errors_peak);
    SVMM_MIAO_RESET(disconnects);
#undef SVMM_MIAO_RESET
    SvmmMiaoOutput.sample_rate.store(MiaoSampleRate, std::memory_order_relaxed);
    SvmmMiaoOutput.period_frames.store(periodFrames, std::memory_order_relaxed);
    SvmmMiaoOutput.buffer_frames.store(bufferFrames, std::memory_order_relaxed);
    SvmmMiaoOutput.target_queue_bytes.store(
        targetQueueBytes, std::memory_order_relaxed);
}

bool isBackpressure(MI_S32 result) noexcept
{
    return result == MI_AO_ERR_NOBUF || result == MI_AO_ERR_BUF_FULL ||
        result == MI_AO_ERR_BUSY;
}

uint32_t queueWaitMilliseconds(uint32_t busyBytes, uint32_t periodBytes,
    uint32_t targetQueueBytes) noexcept
{
    if(busyBytes + periodBytes <= targetQueueBytes)
        return 0;
    const uint32_t excess{busyBytes + periodBytes - targetQueueBytes};
    const uint64_t micros{
        (static_cast<uint64_t>(excess) * 1000000ull +
            MiaoSampleRate*MiaoFrameBytes - 1) /
        (MiaoSampleRate*MiaoFrameBytes)};
    return std::max(1u, std::min(5u,
        static_cast<uint32_t>((micros + 999u) / 1000u)));
}

struct MiaoPlayback final : public BackendBase {
    explicit MiaoPlayback(ALCdevice *device) noexcept : BackendBase{device} { }
    ~MiaoPlayback() override;

    int mixerProc();
    void open(const char *name) override;
    bool reset() override;
    void start() override;
    void stop() override;
    ClockLatency getClockLatency() override;

    bool queryState(MI_AO_ChnState_t &state);
    MI_S32 enableChannelLocked();
    MI_S32 configureHardwareLocked(MI_AO_ChnState_t &state);
    void shutdownHardwareLocked();
    bool recoverChannel(uint32_t recoveryStage);
    bool recoverAfterFault(uint32_t &recoveryStage);
    void shutdownHardware();
    bool noteFailure(uint32_t &consecutive, uint32_t threshold,
        uint32_t &recoveryStage);

    al::vector<al::byte> mMixData;
    std::atomic<bool> mKillNow{true};
    std::thread mThread;
    std::mutex mApiMutex;
    bool mDeviceEnabled{false};
    bool mChannelEnabled{false};
    uint32_t mPeriodFrames{512};
    uint32_t mBufferFrames{2048};
    uint32_t mPeriodBytes{2048};
    uint32_t mTargetQueueBytes{8192};

    DEF_NEWDEL(MiaoPlayback)
};

MiaoPlayback::~MiaoPlayback()
{
    stop();
    shutdownHardware();
}

bool MiaoPlayback::queryState(MI_AO_ChnState_t &state)
{
    std::memset(&state, 0, sizeof(state));
    SvmmMiaoOutput.query_calls.fetch_add(1, std::memory_order_relaxed);
    const MI_S32 result{MI_AO_QueryChnStat(MiaoDevice, MiaoChannel, &state)};
    if(result != MI_SUCCESS)
    {
        SvmmMiaoOutput.query_errors.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    SvmmMiaoOutput.hardware_total_bytes.store(
        state.u32ChnTotalNum, std::memory_order_relaxed);
    SvmmMiaoOutput.hardware_free_bytes.store(
        state.u32ChnFreeNum, std::memory_order_relaxed);
    SvmmMiaoOutput.hardware_busy_bytes.store(
        state.u32ChnBusyNum, std::memory_order_relaxed);
    return true;
}

MI_S32 MiaoPlayback::enableChannelLocked()
{
    MI_S32 result{MI_AO_EnableChn(MiaoDevice, MiaoChannel)};
    mChannelEnabled = result == MI_SUCCESS;
    if(result == MI_SUCCESS)
        result = MI_AO_SetMute(MiaoDevice, FALSE);
    return result;
}

MI_S32 MiaoPlayback::configureHardwareLocked(MI_AO_ChnState_t &state)
{
    MI_AUDIO_Attr_t attr{};
    attr.eSamplerate = E_MI_AUDIO_SAMPLE_RATE_48000;
    attr.eBitwidth = E_MI_AUDIO_BIT_WIDTH_16;
    attr.eWorkmode = E_MI_AUDIO_MODE_I2S_MASTER;
    attr.eSoundmode = E_MI_AUDIO_SOUND_MODE_STEREO;
    attr.u32PtNumPerFrm = mPeriodFrames;
    attr.u32ChnCnt = MiaoChannels;

    MI_S32 result{MI_AO_SetPubAttr(MiaoDevice, &attr)};
    if(result == MI_SUCCESS)
    {
        result = MI_AO_Enable(MiaoDevice);
        mDeviceEnabled = result == MI_SUCCESS;
    }
    if(result == MI_SUCCESS)
        result = enableChannelLocked();
    if(result == MI_SUCCESS)
    {
        result = MI_AO_ClearChnBuf(MiaoDevice, MiaoChannel);
        if(result == MI_SUCCESS)
            SvmmMiaoOutput.clears.fetch_add(1, std::memory_order_relaxed);
    }
    if(result == MI_SUCCESS && !queryState(state))
        result = MI_AO_ERR_SYS_NOTREADY;
    return result;
}

void MiaoPlayback::shutdownHardwareLocked()
{
    if(mChannelEnabled)
    {
        (void)MI_AO_DisableChn(MiaoDevice, MiaoChannel);
        mChannelEnabled = false;
    }
    if(mDeviceEnabled)
    {
        (void)MI_AO_Disable(MiaoDevice);
        mDeviceEnabled = false;
    }
}

bool MiaoPlayback::recoverChannel(uint32_t recoveryStage)
{
    SvmmMiaoOutput.recoveries.fetch_add(1, std::memory_order_relaxed);
    if(recoveryStage == 0 &&
        MI_AO_ClearChnBuf(MiaoDevice, MiaoChannel) == MI_SUCCESS)
    {
        SvmmMiaoOutput.clears.fetch_add(1, std::memory_order_relaxed);
        return true;
    }

    if(recoveryStage <= 1)
    {
        MI_S32 result{MI_SUCCESS};
        if(mChannelEnabled)
        {
            result = MI_AO_DisableChn(MiaoDevice, MiaoChannel);
            mChannelEnabled = false;
        }
        if(result == MI_SUCCESS)
            result = enableChannelLocked();
        if(result == MI_SUCCESS)
            result = MI_AO_ClearChnBuf(MiaoDevice, MiaoChannel);
        if(result == MI_SUCCESS)
        {
            SvmmMiaoOutput.clears.fetch_add(1, std::memory_order_relaxed);
            SvmmMiaoOutput.channel_restarts.fetch_add(1, std::memory_order_relaxed);
            return true;
        }
    }

    if(recoveryStage <= 2)
    {
        shutdownHardwareLocked();
        MI_AO_ChnState_t state{};
        const MI_S32 result{configureHardwareLocked(state)};
        if(result == MI_SUCCESS && state.u32ChnTotalNum >= mPeriodBytes)
        {
            SvmmMiaoOutput.channel_restarts.fetch_add(1, std::memory_order_relaxed);
            return true;
        }
        shutdownHardwareLocked();
    }

    SvmmMiaoOutput.recovery_failures.fetch_add(1, std::memory_order_relaxed);
    SvmmMiaoOutput.disconnects.fetch_add(1, std::memory_order_relaxed);
    return false;
}

bool MiaoPlayback::recoverAfterFault(uint32_t &recoveryStage)
{
    {
        std::lock_guard<std::mutex> lock{mApiMutex};
        if(!recoverChannel(recoveryStage))
            return false;
    }
    const uint32_t delayIndex{std::min<uint32_t>(recoveryStage,
        sizeof(MiaoRecoveryBackoffMs)/sizeof(MiaoRecoveryBackoffMs[0]) - 1)};
    ++recoveryStage;
    std::this_thread::sleep_for(
        std::chrono::milliseconds{MiaoRecoveryBackoffMs[delayIndex]});
    return true;
}

bool MiaoPlayback::noteFailure(uint32_t &consecutive, uint32_t threshold,
    uint32_t &recoveryStage)
{
    ++consecutive;
    updateMaximum(SvmmMiaoOutput.consecutive_errors_peak, consecutive);
    if(consecutive < threshold)
        return true;
    if(!recoverAfterFault(recoveryStage))
        return false;
    consecutive = 0;
    return true;
}

int MiaoPlayback::mixerProc()
{
    SetRTPriority();
    althrd_setname(MIXER_THREAD_NAME);

    const size_t frameStep{mDevice->channelsFromFmt()};
    bool pending{false};
    uint32_t consecutiveFailures{0};
    uint32_t recoveryStage{0};
    uint32_t queueWaitStartedMs{0};
    while(!mKillNow.load(std::memory_order_acquire) &&
        mDevice->Connected.load(std::memory_order_acquire))
    {
        MI_AO_ChnState_t state{};
        bool queryOk;
        {
            std::lock_guard<std::mutex> lock{mApiMutex};
            queryOk = queryState(state);
        }
        if(!queryOk)
        {
            queueWaitStartedMs = 0;
            if(!noteFailure(consecutiveFailures, MiaoFatalRecoveryThreshold,
                recoveryStage))
            {
                mDevice->handleDisconnect("MI_AO query and recovery failed");
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds{2});
            continue;
        }

        const uint32_t waitMs{queueWaitMilliseconds(
            state.u32ChnBusyNum, mPeriodBytes, mTargetQueueBytes)};
        if(waitMs != 0)
        {
            const uint32_t now{miaoNowMs()};
            SvmmMiaoOutput.queue_waits.fetch_add(1, std::memory_order_relaxed);
            SvmmMiaoOutput.queue_wait_ms.fetch_add(waitMs, std::memory_order_relaxed);
            if(queueWaitStartedMs == 0)
                queueWaitStartedMs = now;
            else if(now - queueWaitStartedMs >= MiaoQueueStallRecoveryMs)
            {
                TRACE("MI_AO queue stalled busy=%u target=%u for %u ms; recovering\n",
                    state.u32ChnBusyNum, mTargetQueueBytes,
                    now - queueWaitStartedMs);
                if(!recoverAfterFault(recoveryStage))
                {
                    mDevice->handleDisconnect("MI_AO queue stall recovery failed");
                    break;
                }
                queueWaitStartedMs = 0;
                consecutiveFailures = 0;
                continue;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds{waitMs});
            continue;
        }
        queueWaitStartedMs = 0;

        if(!pending)
        {
            recordMixerCycle();
            mDevice->renderSamples(mMixData.data(), mPeriodFrames, frameStep);
            pending = true;
        }

        MI_AUDIO_Frame_t frame{};
        frame.eBitwidth = E_MI_AUDIO_BIT_WIDTH_16;
        frame.eSoundmode = E_MI_AUDIO_SOUND_MODE_STEREO;
        frame.apVirAddr[0] = mMixData.data();
        frame.u32Len = mPeriodBytes;

        MI_S32 result;
        {
            std::lock_guard<std::mutex> lock{mApiMutex};
            SvmmMiaoOutput.send_calls.fetch_add(1, std::memory_order_relaxed);
            result = MI_AO_SendFrame(
                MiaoDevice, MiaoChannel, &frame, MiaoSendTimeoutMs);
        }
        if(result == MI_SUCCESS)
        {
            recordOutput(mPeriodFrames);
            pending = false;
            consecutiveFailures = 0;
            recoveryStage = 0;
            continue;
        }

        SvmmMiaoOutput.send_retries.fetch_add(1, std::memory_order_relaxed);
        if(result == MI_AO_ERR_NOBUF || result == MI_AO_ERR_BUF_FULL)
            SvmmMiaoOutput.send_nobuf.fetch_add(1, std::memory_order_relaxed);
        else if(result == MI_AO_ERR_BUSY)
            SvmmMiaoOutput.send_busy.fetch_add(1, std::memory_order_relaxed);
        else
            SvmmMiaoOutput.send_errors.fetch_add(1, std::memory_order_relaxed);

        const uint32_t threshold{isBackpressure(result)
            ? MiaoBackpressureRecoveryThreshold : MiaoFatalRecoveryThreshold};
        if(!noteFailure(consecutiveFailures, threshold, recoveryStage))
        {
            mDevice->handleDisconnect("MI_AO send and recovery failed: %#x",
                static_cast<unsigned int>(result));
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }
    return 0;
}

void MiaoPlayback::open(const char *name)
{
    if(name == nullptr)
        name = MiaoDeviceName;
    else if(std::strcmp(name, MiaoDeviceName) != 0)
        throw al::backend_exception{al::backend_error::NoDevice,
            "Device name \"%s\" not found", name};
    mDevice->DeviceName = name;
}

bool MiaoPlayback::reset()
{
    stop();
    shutdownHardware();

    const uint32_t requestedPeriod{std::max(1u, mDevice->UpdateSize)};
    uint32_t bufferPeriods{std::max(1u,
        (mDevice->BufferSize + requestedPeriod - 1) / requestedPeriod)};
    mPeriodFrames = std::max(MiaoMinimumPeriodFrames,
        std::min(MiaoMaximumPeriodFrames,
            (requestedPeriod + MiaoMinimumPeriodFrames - 1) &
                ~(MiaoMinimumPeriodFrames - 1)));
    bufferPeriods = std::max(MiaoMinimumBufferPeriods,
        std::min(MiaoMaximumBufferPeriods, bufferPeriods));
    mBufferFrames = mPeriodFrames * bufferPeriods;
    mPeriodBytes = mPeriodFrames * MiaoFrameBytes;
    mTargetQueueBytes = mBufferFrames * MiaoFrameBytes;
    resetTelemetry(mPeriodFrames, mBufferFrames, mTargetQueueBytes);

    mDevice->Frequency = MiaoSampleRate;
    mDevice->FmtChans = DevFmtStereo;
    mDevice->FmtType = DevFmtShort;
    mDevice->UpdateSize = mPeriodFrames;
    mDevice->BufferSize = mBufferFrames;
    setDefaultChannelOrder();
    mMixData.resize(mPeriodBytes);

    std::unique_lock<std::mutex> lock{mApiMutex};
    MI_AO_ChnState_t state{};
    MI_S32 result{MI_AO_ERR_SYS_NOTREADY};
    for(uint32_t attempt{0}; attempt < MiaoStartupAttempts; ++attempt)
    {
        state = {};
        result = configureHardwareLocked(state);
        if(result == MI_SUCCESS && state.u32ChnTotalNum >= mPeriodBytes)
            break;
        shutdownHardwareLocked();
        if(attempt + 1 < MiaoStartupAttempts)
        {
            lock.unlock();
            std::this_thread::sleep_for(
                std::chrono::milliseconds{MiaoStartupRetryMs});
            lock.lock();
        }
    }
    if(result != MI_SUCCESS || state.u32ChnTotalNum < mPeriodBytes)
    {
        ERR("MI_AO initialization failed: result=%#x capacity=%u\n",
            static_cast<unsigned int>(result), state.u32ChnTotalNum);
        SvmmMiaoOutput.disconnects.fetch_add(1, std::memory_order_relaxed);
        return false;
    }

    TRACE("SVMM MI_AO configured rate=%u period=%u buffer=%u target=%u capacity=%u\n",
        MiaoSampleRate, mPeriodFrames, mBufferFrames, mTargetQueueBytes,
        state.u32ChnTotalNum);
    return true;
}

void MiaoPlayback::start()
{
    if(!mDeviceEnabled || !mChannelEnabled)
        throw al::backend_exception{al::backend_error::DeviceError,
            "MI_AO is not configured"};
    try {
        mKillNow.store(false, std::memory_order_release);
        SvmmMiaoOutput.active.store(1, std::memory_order_release);
        mThread = std::thread{std::mem_fn(&MiaoPlayback::mixerProc), this};
    }
    catch(std::exception &error) {
        SvmmMiaoOutput.active.store(0, std::memory_order_release);
        mKillNow.store(true, std::memory_order_release);
        throw al::backend_exception{al::backend_error::DeviceError,
            "Failed to start MI_AO mixer thread: %s", error.what()};
    }
}

void MiaoPlayback::stop()
{
    mKillNow.store(true, std::memory_order_release);
    if(mThread.joinable())
        mThread.join();
    SvmmMiaoOutput.active.store(0, std::memory_order_release);

    std::lock_guard<std::mutex> lock{mApiMutex};
    if(mChannelEnabled && MI_AO_ClearChnBuf(MiaoDevice, MiaoChannel) == MI_SUCCESS)
        SvmmMiaoOutput.clears.fetch_add(1, std::memory_order_relaxed);
}

void MiaoPlayback::shutdownHardware()
{
    std::lock_guard<std::mutex> lock{mApiMutex};
    shutdownHardwareLocked();
}

ClockLatency MiaoPlayback::getClockLatency()
{
    ClockLatency result{};
    result.ClockTime = GetDeviceClockTime(mDevice);
    MI_AO_ChnState_t state{};
    std::lock_guard<std::mutex> lock{mApiMutex};
    if(queryState(state))
    {
        result.Latency = std::chrono::seconds{state.u32ChnBusyNum / MiaoFrameBytes};
        result.Latency /= MiaoSampleRate;
    }
    return result;
}

} // namespace

extern "C" ALC_API ALCint ALC_APIENTRY
alsoft_svmm_get_miao_stats(ALCvoid *buffer, ALCsizei size)
{
    if(buffer == nullptr ||
        size < static_cast<ALCsizei>(sizeof(SvmmMiaoOutputStats)))
        return 0;

    SvmmMiaoOutputStats stats{};
    stats.abi_version = 3;
    stats.struct_size = sizeof(stats);
#define SVMM_MIAO_LOAD(name) \
    stats.name = SvmmMiaoOutput.name.load(std::memory_order_relaxed)
    SVMM_MIAO_LOAD(active);
    SVMM_MIAO_LOAD(sample_rate);
    SVMM_MIAO_LOAD(period_frames);
    SVMM_MIAO_LOAD(buffer_frames);
    SVMM_MIAO_LOAD(target_queue_bytes);
    SVMM_MIAO_LOAD(hardware_total_bytes);
    SVMM_MIAO_LOAD(hardware_free_bytes);
    SVMM_MIAO_LOAD(hardware_busy_bytes);
    SVMM_MIAO_LOAD(mixer_cycles);
    SVMM_MIAO_LOAD(mixer_gap_last_ms);
    SVMM_MIAO_LOAD(mixer_gap_max_ms);
    SVMM_MIAO_LOAD(output_operations);
    SVMM_MIAO_LOAD(output_frames);
    SVMM_MIAO_LOAD(output_gap_last_ms);
    SVMM_MIAO_LOAD(output_gap_max_ms);
    SVMM_MIAO_LOAD(query_calls);
    SVMM_MIAO_LOAD(query_errors);
    SVMM_MIAO_LOAD(queue_waits);
    SVMM_MIAO_LOAD(queue_wait_ms);
    SVMM_MIAO_LOAD(send_calls);
    SVMM_MIAO_LOAD(send_retries);
    SVMM_MIAO_LOAD(send_nobuf);
    SVMM_MIAO_LOAD(send_busy);
    SVMM_MIAO_LOAD(send_errors);
    SVMM_MIAO_LOAD(recoveries);
    SVMM_MIAO_LOAD(recovery_failures);
    SVMM_MIAO_LOAD(clears);
    SVMM_MIAO_LOAD(channel_restarts);
    SVMM_MIAO_LOAD(consecutive_errors_peak);
    SVMM_MIAO_LOAD(disconnects);
#undef SVMM_MIAO_LOAD

    const uint32_t now{miaoNowMs()};
    const uint32_t mixerHeartbeat{
        SvmmMiaoOutput.mixer_heartbeat_ms.load(std::memory_order_relaxed)};
    const uint32_t outputHeartbeat{
        SvmmMiaoOutput.output_heartbeat_ms.load(std::memory_order_relaxed)};
    if(mixerHeartbeat != 0)
    {
        const uint32_t age{now - mixerHeartbeat};
        stats.mixer_heartbeat_age_ms = age <= 0x7fffffffu ? age : 0;
    }
    if(outputHeartbeat != 0)
    {
        const uint32_t age{now - outputHeartbeat};
        stats.output_heartbeat_age_ms = age <= 0x7fffffffu ? age : 0;
    }
    std::memcpy(buffer, &stats, sizeof(stats));
    return 1;
}

BackendFactory &MiaoBackendFactory::getFactory()
{
    static MiaoBackendFactory factory{};
    return factory;
}

bool MiaoBackendFactory::init()
{ return true; }

bool MiaoBackendFactory::querySupport(BackendType type)
{ return type == BackendType::Playback; }

std::string MiaoBackendFactory::probe(BackendType type)
{
    std::string names;
    if(type == BackendType::Playback)
        names.append(MiaoDeviceName, sizeof(MiaoDeviceName));
    return names;
}

BackendPtr MiaoBackendFactory::createBackend(ALCdevice *device, BackendType type)
{
    if(type == BackendType::Playback)
        return BackendPtr{new MiaoPlayback{device}};
    return nullptr;
}

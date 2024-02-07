/*
 * Copyright (C) 2011, 2012 Apple Inc. All Rights Reserved.
 * Copyright (C) 2014 Raspberry Pi Foundation. All Rights Reserved.
 * Copyright (C) 2018 Igalia S.L.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY APPLE INC. ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL APPLE INC. OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "config.h"
#include <wtf/MemoryPressureHandler.h>

#include <fnmatch.h>
#include <malloc.h>
#include <unistd.h>
#include <wtf/MainThread.h>
#include <wtf/MemoryFootprint.h>
#include <wtf/Threading.h>
#include <wtf/linux/CurrentProcessMemoryStatus.h>
#include <wtf/text/WTFString.h>

#include <vector>

#define LOG_CHANNEL_PREFIX Log

namespace WTF {

// Disable memory event reception for a minimum of s_minimumHoldOffTime
// seconds after receiving an event. Don't let events fire any sooner than
// s_holdOffMultiplier times the last cleanup processing time. Effectively
// this is 1 / s_holdOffMultiplier percent of the time.
// If after releasing the memory we don't free at least s_minimumBytesFreedToUseMinimumHoldOffTime,
// we wait longer to try again (s_maximumHoldOffTime).
// These value seems reasonable and testing verifies that it throttles frequent
// low memory events, greatly reducing CPU usage.
static const Seconds s_minimumHoldOffTime { 1_s };
static const Seconds s_maximumHoldOffTime { 1_s };
static const size_t s_minimumBytesFreedToUseMinimumHoldOffTime = 1 * MB;
static const unsigned s_holdOffMultiplier = 20;

static const Seconds s_memoryUsagePollerInterval { 1_s };
static size_t s_pollMaximumProcessMemoryCriticalLimit = 0;
static size_t s_pollMaximumProcessMemoryNonCriticalLimit = 0;
static size_t s_pollMaximumProcessGPUMemoryCriticalLimit = 0;
static size_t s_pollMaximumProcessGPUMemoryNonCriticalLimit = 0;
static size_t s_initialGPUMemory = 0;
static const Seconds s_gfxPopupInterval { 60_s };
static MonotonicTime s_lastGFXPopupTime = MonotonicTime::nan();

static const char* s_processStatus = "/proc/self/status";
static const char* s_cmdline = "/proc/self/cmdline";
static const char* s_GPUMemoryUsedFile = "/sys/bus/pci/drivers/vmwgfx/0000:00:02.0/memory_accounting/kernel/used_memory";

static inline String nextToken(FILE* file)
{
    if (!file)
        return String();

    static const unsigned bufferSize = 128;
    char buffer[bufferSize] = {0, };
    unsigned index = 0;
    while (index < bufferSize) {
        int ch = fgetc(file);
        if (ch == EOF || (isASCIISpace(ch) && index)) // Break on non-initial ASCII space.
            break;
        if (!isASCIISpace(ch)) {
            buffer[index] = ch;
            index++;
        }
    }

    return String(buffer);
}

bool readToken(const char* filename, const char* key, size_t fileUnits, size_t &result)
{
    FILE* file = fopen(filename, "r");
    if (!file)
        return false;

    bool validValue = false;
    String token;
    do {
        token = nextToken(file);
        if (token.isEmpty())
            break;

        if (!key) {
            result = token.toUInt64(&validValue) * fileUnits;
            break;
        }

        if (token == key) {
            result = nextToken(file).toUInt64(&validValue) * fileUnits;
            break;
        }
    } while (!token.isEmpty());

    fclose(file);
    return validValue;
}

// Read GPU memory value (KB) from Ubuntu VM vmwgfx kernel driver file
bool read_vmwgfx_used_memory(size_t &value)
{
    FILE* file = fopen(s_GPUMemoryUsedFile, "r");
    if (!file)
        return false;

    bool success = false;

    value = nextToken(file).toUInt64(&success);
    fclose(file);

    return success;
}

// Used GPU mem in bytes from s_GPUMemoryUsedFile
// Get actual amount used by comparing with the initial GFX value.
size_t GetUsedGpuRam()
{
    size_t value = 0;
    if (read_vmwgfx_used_memory(value))
    {
        value = value - s_initialGPUMemory; // Subtract gfx mem that was in use at launch
        value *= 1024; // Convert KB to bytes for comparison to gpu limit vars
    }
    else
        WTFLogAlways("MemoryPressureHandler: FAILED to read the GFX memory usage from: %s", s_GPUMemoryUsedFile);
    return value;
}

// Used GPU mem in bytes from s_GPUMemoryUsedFile
// Get actual amount used by comparing with the initial GFX value.
size_t MemoryPressureHandler::usedGfxMemory()
{
    return GetUsedGpuRam();
}

// Used GPU mem converted to a percentage of the limit set by WPE_POLL_MAX_MEMORY_GPU
int MemoryPressureHandler::usedGfxPercent()
{
    int percent = 0;
    size_t value = GetUsedGpuRam();

    if (value && s_pollMaximumProcessGPUMemoryCriticalLimit)
        percent = (int) ((double)value / s_pollMaximumProcessGPUMemoryCriticalLimit * 100);

    return percent;
}

static String getProcessName()
{
    FILE* file = fopen(s_cmdline, "r");
    if (!file)
        return String();

    String result = nextToken(file);
    fclose(file);

    return result;
}

// Used WebProcess mem in bytes
bool MemoryPressureHandler::usedWebProcMemory(size_t& value)
{
    // Method applies only to the WebProcess
    if (fnmatch("*WPEWebProcess", getProcessName().utf8().data(), 0))
        return false;
    
    value = 0;
    readToken(s_processStatus, "VmRSS:", KB, value);
    return true;
}

// Used WebProcess mem as a percentage of s_pollMaximumProcessMemoryCriticalLimit
bool MemoryPressureHandler::usedWebProcPercent(int& percent)
{
    percent = 0;
    size_t value = 0;

    if (usedWebProcMemory(value) && s_pollMaximumProcessMemoryCriticalLimit) {
        percent = (int) ((double)value / s_pollMaximumProcessMemoryCriticalLimit * 100);
        return true;
    }
    return false;
}


// Diagnostics API start

void MemoryPressureHandler::addRAMImage(String uniqueID, float memoryMB)
{
    String copy = String(uniqueID);
    fprintf(stdout, "\n\nlejraee addRAMImage() UID: %s  mem: %fMB\n\n", copy.utf8().data(), memoryMB);
    fflush(stdout);
    m_ramImages[copy.utf8().data()] = memoryMB;
}

void MemoryPressureHandler::addGFXImage(String uniqueID, float memoryMB)
{
    String copy = String(uniqueID);
    fprintf(stdout, "\n\nlejraee addGFXImage() UID: %s  mem: %fMB\n\n", copy.utf8().data(), memoryMB);
    fflush(stdout);
    m_gfxImages[copy.utf8().data()] = memoryMB;
}

bool MemoryPressureHandler::ramImagesEstimate(float& ramEstimate)
{
    // Method applies only to the WebProcess
    if (fnmatch("*WPEWebProcess", getProcessName().utf8().data(), 0))
        return false;
    
    ramEstimate = 0.0f;
    for (const auto& item : m_ramImages) {
        // fprintf(stdout, "\n\nlejraee RAM UID: %s  mem: %fMB\n\n", item.first.c_str(), item.second);
        // fflush(stdout);
        if (m_gfxImages.count(item.first)) {
            fprintf(stdout, "\n\nlejraee RAM UID: %s  FOUND IN GFX so NOT adding to RAM total\n\n", item.first.c_str());
            fflush(stdout);
        } else
            ramEstimate += item.second;
    }
    return true;
}

bool MemoryPressureHandler::gfxImagesEstimate(float& gfxEstimate)
{
    // Method applies only to the WebProcess
    if (fnmatch("*WPEWebProcess", getProcessName().utf8().data(), 0))
        return false;

    gfxEstimate = 0.0f;
    for (const auto& item : m_gfxImages) {
        // fprintf(stdout, "\n\nlejraee GFX UID: %s  mem: %fMB\n\n", item.first.c_str(), item.second);
        // fflush(stdout);
        gfxEstimate += item.second;
    }
    return true;
}

void logGFXImages()
{
    float ramEstimate = 0.0f; 
    float gfxEstimate = 0.0f;
    if (MemoryPressureHandler::singleton().ramImagesEstimate(ramEstimate) && MemoryPressureHandler::singleton().gfxImagesEstimate(gfxEstimate)) {
        fprintf(stdout, "\n\nlejraee ramEstimate: %fMB  gfxEstimate: %fMB\n\n", ramEstimate, gfxEstimate);
        fflush(stdout);
    }
}

// Diagnostics API end


// Initialises the process memory limit vars in bytes
static bool initializeProcessMemoryLimits(size_t &criticalLimit, size_t &nonCriticalLimit)
{
    static bool initialized = false;
    static bool success = false;

    if (initialized)
        return success;

    initialized = true;

    // Syntax: Case insensitive, process name, wildcard (*), unit multipliers (M=Mb, K=Kb, <empty>=bytes).
    // Example: WPE_POLL_MAX_MEMORY='WPEWebProcess:500M,*Process:150M'
    String processName(getProcessName().convertToLowercaseWithoutLocale());
    String s(getenv("WPE_POLL_MAX_MEMORY"));
    if (!s.isEmpty()) {
        Vector<String> entries = s.split(',');
        for (const String& entry : entries) {
            Vector<String> keyvalue = entry.split(':');
            if (keyvalue.size() != 2)
                continue;
            String key = "*"+keyvalue[0].stripWhiteSpace().convertToLowercaseWithoutLocale();
            String value = keyvalue[1].stripWhiteSpace().convertToLowercaseWithoutLocale();
            size_t units = 1;
            if (value.endsWith('k'))
                units = 1024;
            else if (value.endsWith('m'))
                units = 1024 * 1024;
            if (units != 1)
                value = value.substring(0, value.length()-1);
            bool ok = false;
            size_t size = size_t(value.toUInt64(&ok));
            if (!ok)
                continue;

            if (!fnmatch(key.utf8().data(), processName.utf8().data(), 0)) {
                criticalLimit = size * units;
                nonCriticalLimit = criticalLimit * 0.95; //0.75;
                success = true;
                return true;
            }
        }
    }

    success = false;
    return false;
}

// Initialises the gpu memory limit vars in bytes
static bool initializeProcessGPUMemoryLimits(size_t &criticalLimit, size_t &nonCriticalLimit)
{
    static bool initialized = false;
    static bool success = false;

    if (initialized)
        return success;

    initialized = true;

    // Syntax: Case insensitive, unit multipliers (M=Mb, K=Kb, <empty>=bytes).
    // Example: WPE_POLL_MAX_MEMORY_GPU='150M'

    // GPU memory limit applies only to the WebProcess.
    if (fnmatch("*WPEWebProcess", getProcessName().utf8().data(), 0))
        return false;

    // Ensure that the limit is defined.
    if (!getenv("WPE_POLL_MAX_MEMORY_GPU"))
        return false;
    
    bool ok = false;
    if (getenv("WEBKIT_INITIAL_GFX_VALUE")) {
        String initial(getenv("WEBKIT_INITIAL_GFX_VALUE"));
        String initialGFX = initial.stripWhiteSpace().convertToLowercaseWithoutLocale();
        size_t initialGFXsize = size_t(initialGFX.toUInt64(&ok));

        if (!ok)
            WTFLogAlways("MemoryPressureHandler: ERROR Failed to read the initial GFX memory at launch! This should be done by launch-wpewebkit script");
        
        s_initialGPUMemory = initialGFXsize;
    }

    String s(getenv("WPE_POLL_MAX_MEMORY_GPU"));
    String value = s.stripWhiteSpace().convertToLowercaseWithoutLocale();
    size_t units = 1;
    if (value.endsWith('k'))
        units = 1024;
    else if (value.endsWith('m'))
        units = 1024 * 1024;
    if (units != 1)
        value = value.substring(0, value.length()-1);
    ok = false;
    size_t size = size_t(value.toUInt64(&ok));

    // Ensure that the string can be converted to size_t.
    if (!ok)
        return false;

    criticalLimit = size * units;
    nonCriticalLimit = criticalLimit * 0.95;
    success = true;
    return true;
}

struct MemoryPressureHandler::MemoryUsagePollerThreadContext
    : public ThreadSafeRefCounted<MemoryPressureHandler::MemoryUsagePollerThreadContext>
{
    void stop()
    {
        LockHolder locker(m_lock);
        m_shouldStop = true;
        m_condition.notifyAll();
    }

    // returns false when should stop polling
    bool sleep(const Seconds timeout)
    {
       LockHolder locker(m_lock);
       return !m_condition.waitFor(m_lock, timeout, [this]() { return m_shouldStop; });
    }

    Lock m_lock;
    bool m_shouldStop { false };
    Condition m_condition;
};

MemoryPressureHandler::MemoryUsagePoller::MemoryUsagePoller()
{
    m_context = adoptRef(new MemoryPressureHandler::MemoryUsagePollerThreadContext());
    m_thread = Thread::create("WTF: MemoryPressureHandler", [this, context = m_context] {
        do {

            // TEMP
            // logGFXImages();
            //


            bool underMemoryPressure = false;
            bool critical = false;
            bool synchronous = false;
            bool gpuCritical = false;
            size_t value = 0;
            size_t value_swap = 0;

            if (s_pollMaximumProcessMemoryCriticalLimit) {
                if (readToken(s_processStatus, "VmRSS:", KB, value) && readToken(s_processStatus, "VmSwap:", KB, value_swap)) {
                    if (value + value_swap > s_pollMaximumProcessMemoryNonCriticalLimit) {
                        underMemoryPressure = true;
                        critical = value + value_swap > s_pollMaximumProcessMemoryCriticalLimit;
                        synchronous = value + value_swap > s_pollMaximumProcessMemoryCriticalLimit * 1.05;
                        WTFLogAlways("MemoryPressureHandler: PROCESS Memory under pressure! %luMB (%s)", value/MB, critical ? "critical" : "non-critical");
                    }
                }
            }

            if (s_pollMaximumProcessGPUMemoryCriticalLimit) {
                size_t value = GetUsedGpuRam();
                if (value) {
                    if (value > s_pollMaximumProcessGPUMemoryNonCriticalLimit) {
                        //underMemoryPressure = true;
                        gpuCritical = value > s_pollMaximumProcessGPUMemoryCriticalLimit;
                        int gpuPercent = (int) ((double)value / s_pollMaximumProcessGPUMemoryCriticalLimit * 100);
                        WTFLogAlways("MemoryPressureHandler: GPU Memory under pressure! %luMB %d%% (%s)", value/MB, gpuPercent, gpuCritical ? "critical" : "non-critical");

                        char msg[256];
                        sprintf(msg, "/usr/bin/notify-send MemoryPressureHandler \"GPU Memory under pressure! %luMB %d%% (%s) %s", value/MB, gpuPercent, gpuCritical ? "critical" : "non-critical", "\"");
                        // Only one GFX warning popup every minute
                        MonotonicTime currentTime = MonotonicTime::now();
                        if (std::isnan(s_lastGFXPopupTime) || (currentTime >= (s_lastGFXPopupTime + s_gfxPopupInterval))) {
                            s_lastGFXPopupTime = currentTime;
                            system(msg);
                        }
                    }
                }
            }

            if (underMemoryPressure) {
                callOnMainThread([critical, synchronous] {
                    MemoryPressureHandler::singleton().triggerMemoryPressureEvent(critical, synchronous);
                });
                return;
            }

            if (!context->sleep(s_memoryUsagePollerInterval))
                return;
        } while (true);
    });
}

MemoryPressureHandler::MemoryUsagePoller::~MemoryUsagePoller()
{
    m_context->stop();
    if (m_thread)
        m_thread->detach();
}



const char* MemoryPressureHandler::getInitialGFX()
{
    FILE* file = fopen(s_GPUMemoryUsedFile, "r");
    if (!file)
        return "";

    const char* value = nextToken(file).utf8().data();
    fclose(file);
    
    return value;
}

void MemoryPressureHandler::triggerMemoryPressureEvent(bool isCritical, bool isSynchronous)
{
    if (!m_installed)
        return;

    if (ReliefLogger::loggingEnabled())
        LOG(MemoryPressure, "Got memory pressure notification (%s, %s) ", isCritical ? "critical" : "non-critical", isSynchronous ? "synchronous" : "non-synchronous");

    setUnderMemoryPressure(true);

    if (isMainThread())
        respondToMemoryPressure(isCritical ? Critical::Yes : Critical::No, isSynchronous ? Synchronous::Yes : Synchronous::No);
    else
        RunLoop::main().dispatch([this, isCritical, isSynchronous] {
            respondToMemoryPressure(isCritical ? Critical::Yes : Critical::No, isSynchronous ? Synchronous::Yes : Synchronous::No);
        });

    if (ReliefLogger::loggingEnabled() && isUnderMemoryPressure())
        LOG(MemoryPressure, "System is no longer under memory pressure.");

    setUnderMemoryPressure(false);
}

void MemoryPressureHandler::install()
{
    if (m_installed || m_holdOffTimer.isActive())
        return;

    // If the per process limits are not defined, we don't create the memory poller.
    if (initializeProcessMemoryLimits(s_pollMaximumProcessMemoryCriticalLimit, s_pollMaximumProcessMemoryNonCriticalLimit) |
        initializeProcessGPUMemoryLimits(s_pollMaximumProcessGPUMemoryCriticalLimit, s_pollMaximumProcessGPUMemoryNonCriticalLimit))
        m_memoryUsagePoller = std::make_unique<MemoryUsagePoller>();

    m_installed = true;
}

void MemoryPressureHandler::uninstall()
{
    if (!m_installed)
        return;

    m_holdOffTimer.stop();

    m_memoryUsagePoller = nullptr;

    m_installed = false;
}

void MemoryPressureHandler::holdOffTimerFired()
{
    install();
}

void MemoryPressureHandler::holdOff(Seconds seconds)
{
    m_holdOffTimer.startOneShot(seconds);
}

static size_t processMemoryUsage()
{
    ProcessMemoryStatus memoryStatus;
    currentProcessMemoryStatus(memoryStatus);
    return (memoryStatus.resident - memoryStatus.shared);
}

void MemoryPressureHandler::respondToMemoryPressure(Critical critical, Synchronous synchronous)
{
    uninstall();

    MonotonicTime startTime = MonotonicTime::now();
    int64_t processMemory = processMemoryUsage();
    releaseMemory(critical, synchronous);
    int64_t bytesFreed = processMemory - processMemoryUsage();
    Seconds holdOffTime = s_maximumHoldOffTime;
    if (bytesFreed > 0 && static_cast<size_t>(bytesFreed) >= s_minimumBytesFreedToUseMinimumHoldOffTime)
        holdOffTime = (MonotonicTime::now() - startTime) * s_holdOffMultiplier;
    holdOff(std::max(holdOffTime, s_minimumHoldOffTime));
}

void MemoryPressureHandler::platformReleaseMemory(Critical)
{
#if HAVE(MALLOC_TRIM)
    malloc_trim(0);
#endif
}

Optional<MemoryPressureHandler::ReliefLogger::MemoryUsage> MemoryPressureHandler::ReliefLogger::platformMemoryUsage()
{
    return MemoryUsage {processMemoryUsage(), memoryFootprint()};
}

} // namespace WTF

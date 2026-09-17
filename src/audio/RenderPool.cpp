#include "RenderPool.h"

#include <algorithm>

#if defined (__x86_64__) || defined (_M_X64) || defined (__i386__) || defined (_M_IX86)
 #include <immintrin.h>
 #define SIGNALPATCH_X86 1
#endif

#if defined (__linux__)
 #include <pthread.h>
 #include <sched.h>
#elif defined (_WIN32)
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #include <windows.h>
#endif

namespace signalpatch
{
namespace
{
    inline void spinPause() noexcept
    {
       #if SIGNALPATCH_X86
        _mm_pause();
       #else
        std::this_thread::yield();
       #endif
    }

    // Recursive filters fed silence decay into denormals, which cost ten times
    // a normal float. The callback thread has this set by the host or the
    // engine; a helper has to set it for itself.
    inline void flushDenormalsOnThisThread() noexcept
    {
       #if SIGNALPATCH_X86
        _MM_SET_FLUSH_ZERO_MODE (_MM_FLUSH_ZERO_ON);
        _MM_SET_DENORMALS_ZERO_MODE (_MM_DENORMALS_ZERO_ON);
       #endif
    }

    int currentRealtimePriority() noexcept
    {
       #if defined (__linux__)
        int policy = 0;
        sched_param parameters {};
        if (pthread_getschedparam (pthread_self(), &policy, &parameters) == 0
            && (policy == SCHED_FIFO || policy == SCHED_RR))
            return parameters.sched_priority;
       #endif
        return 0;
    }
} // namespace

RenderPool::RenderPool() = default;

RenderPool::~RenderPool()
{
    setHelperCount (0);
}

bool RenderPool::makeRealtime (int priority) noexcept
{
   #if defined (__linux__)
    sched_param parameters {};
    parameters.sched_priority = std::clamp (priority > 0 ? priority : 70,
                                            sched_get_priority_min (SCHED_FIFO), sched_get_priority_max (SCHED_FIFO));
    return pthread_setschedparam (pthread_self(), SCHED_FIFO, &parameters) == 0;
   #elif defined (_WIN32)
    (void) priority;
    return SetThreadPriority (GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL) != 0;
   #else
    (void) priority;
    return false;
   #endif
}

void RenderPool::refreshUsable() noexcept
{
    bool all = ! helpers.empty();
    for (const auto& helper : helpers)
        all = all && (allowWithoutRealtime || helper->realtime.load (std::memory_order_relaxed));
    usable.store (all, std::memory_order_release);
}

void RenderPool::setHelperCount (int helperCount)
{
    helperCount = std::max (0, helperCount);
    if (helperCount == static_cast<int> (helpers.size()))
        return;

    // Stop everything, then start afresh: this happens when a setting changes, not while playing.
    usable.store (false, std::memory_order_release);
    stopping.store (true, std::memory_order_release);
    epoch.fetch_add (1, std::memory_order_release);
    epoch.notify_all();
    for (auto& helper : helpers)
        if (helper->thread.joinable())
            helper->thread.join();
    helpers.clear();
    stopping.store (false, std::memory_order_release);

    for (int index = 0; index < helperCount; ++index)
    {
        auto helper = std::make_unique<Helper>();
        auto* raw = helper.get();
        raw->thread = std::thread ([this, raw] { helperMain (*raw); });
        helpers.push_back (std::move (helper));
    }
    for (auto& helper : helpers) // each reports whether it got realtime scheduling
        while (! helper->started.load (std::memory_order_acquire))
            std::this_thread::yield();
    refreshUsable();
}

void RenderPool::helperMain (Helper& self)
{
    flushDenormalsOnThisThread();
    self.realtime.store (makeRealtime (0), std::memory_order_release);
    self.started.store (true, std::memory_order_release);

    auto seen = epoch.load (std::memory_order_acquire);
    for (;;)
    {
        epoch.wait (seen, std::memory_order_acquire);
        seen = epoch.load (std::memory_order_acquire);
        if (stopping.load (std::memory_order_acquire))
            return;

        // One step below the callback, so the callback can always preempt a spinning helper.
        if (const auto wanted = callbackPriority.load (std::memory_order_relaxed) - 1;
            wanted > 0 && wanted != self.priority && self.realtime.load (std::memory_order_relaxed))
        {
            makeRealtime (wanted);
            self.priority = wanted;
        }

        inside.fetch_add (1, std::memory_order_acq_rel);
        if (open.load (std::memory_order_acquire))
            work();
        inside.fetch_sub (1, std::memory_order_acq_rel);
    }
}

void RenderPool::work() noexcept
{
    const auto& job = current;
    int idleSpins = 0;
    while (remaining.load (std::memory_order_acquire) > 0 && open.load (std::memory_order_acquire))
    {
        bool ran = false;
        for (int task = 0; task < job.taskCount; ++task)
        {
            if (job.state[task].load (std::memory_order_relaxed) != ready)
                continue;
            auto expected = static_cast<std::uint8_t> (ready);
            if (! job.state[task].compare_exchange_strong (expected, taken, std::memory_order_acq_rel))
                continue;

            job.run (job.context, task);

            for (int edge = job.successorOffsets[task]; edge < job.successorOffsets[task + 1]; ++edge)
            {
                const auto next = job.successors[edge];
                if (job.pending[next].fetch_sub (1, std::memory_order_acq_rel) == 1)
                    job.state[next].store (ready, std::memory_order_release);
            }
            remaining.fetch_sub (1, std::memory_order_acq_rel);
            ran = true;
        }
        if (ran)
        {
            idleSpins = 0;
            continue;
        }
        spinPause();
        if (++idleSpins >= 8192)
        {
            // Let a thread of the same priority that has real work have this core.
            idleSpins = 0;
            std::this_thread::yield();
        }
    }
}

void RenderPool::run (const Job& job) noexcept
{
    if (callbackPriority.load (std::memory_order_relaxed) == 0)
        callbackPriority.store (std::max (1, currentRealtimePriority()), std::memory_order_relaxed);

    current = job;
    for (int task = 0; task < job.taskCount; ++task)
    {
        job.pending[task].store (job.indegree[task], std::memory_order_relaxed);
        job.state[task].store (job.indegree[task] == 0 ? ready : waiting, std::memory_order_relaxed);
    }
    remaining.store (job.taskCount, std::memory_order_release);
    open.store (true, std::memory_order_release);
    epoch.fetch_add (1, std::memory_order_release);
    epoch.notify_all();

    work();

    open.store (false, std::memory_order_release);
    while (inside.load (std::memory_order_acquire) != 0)
        spinPause();
}
} // namespace signalpatch

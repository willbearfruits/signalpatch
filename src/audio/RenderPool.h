#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <thread>
#include <vector>

namespace signalpatch
{
// Realtime helper threads for the audio callback. A render plan whose graph
// has independent heavy branches hands its nodes out as tasks: every node
// carries a count of unfinished sources, a finished node releases the nodes
// it feeds, and whoever is free takes the next ready one. The callback
// thread works alongside the helpers and returns only when every helper has
// left the block, so a plan can be retired safely afterwards.
//
// Between blocks the helpers sleep on a futex (std::atomic::wait). Inside a
// block a helper with nothing to take spins with a pause instruction: a
// block is at most a few milliseconds and a sleep would cost more than it
// saves.
//
// Nothing here allocates or locks once the threads exist.
class RenderPool
{
public:
    /** What a plan gives the pool for one block. The callback fills it in
        before open() and the helpers only read it. */
    struct Job
    {
        void* context = nullptr;
        /** Runs node taskIndex. Called from the callback thread and from helpers. */
        void (*run) (void* context, int taskIndex) noexcept = nullptr;
        int taskCount = 0;
        const int* indegree = nullptr;                 // [taskCount]
        const int* successorOffsets = nullptr;          // [taskCount + 1] into successors
        const int* successors = nullptr;
        std::atomic<int>* pending = nullptr;           // [taskCount] scratch owned by the plan
        std::atomic<std::uint8_t>* state = nullptr;    // [taskCount] scratch owned by the plan
    };

    RenderPool();
    ~RenderPool();

    RenderPool (const RenderPool&) = delete;
    RenderPool& operator= (const RenderPool&) = delete;

    /** Message thread. helperCount 0 stops every helper. Helpers that cannot
        get realtime scheduling are not used (see isUsable). */
    void setHelperCount (int helperCount);
    [[nodiscard]] int getHelperCount() const noexcept { return static_cast<int> (helpers.size()); }
    /** True when there are helpers and all of them run with realtime priority.
        A helper under the ordinary scheduler can be preempted for milliseconds
        while the callback waits for it, which is worse than not using it. */
    [[nodiscard]] bool isUsable() const noexcept { return usable.load (std::memory_order_relaxed); }
    /** For tests: use the helpers whatever their scheduling class. */
    void setUsableWithoutRealtime (bool allow) noexcept { allowWithoutRealtime = allow; refreshUsable(); }

    /** Audio thread: runs the whole job and returns when it is finished and no helper is inside it. */
    void run (const Job& job) noexcept;

private:
    enum : std::uint8_t { waiting = 0, ready = 1, taken = 2 };

    struct Helper;
    void helperMain (Helper& self);
    void work() noexcept;
    void refreshUsable() noexcept;
    /** Gives the calling thread realtime scheduling; priority <= 0 picks a default. */
    static bool makeRealtime (int priority) noexcept;

    struct Helper
    {
        std::thread thread;
        std::atomic<bool> realtime { false };
        std::atomic<bool> started { false };
        int priority = 0; // what this helper last asked for
    };

    std::vector<std::unique_ptr<Helper>> helpers;
    std::atomic<std::uint32_t> epoch { 0 };   // bumped per block; helpers sleep on it
    std::atomic<bool> open { false };         // tasks may be taken
    std::atomic<bool> stopping { false };
    std::atomic<int> inside { 0 };            // helpers currently between their entry and exit of a block
    std::atomic<int> remaining { 0 };         // tasks not finished yet
    std::atomic<bool> usable { false };
    std::atomic<int> callbackPriority { 0 };  // the callback's realtime priority, once it has run a block
    bool allowWithoutRealtime = false;
    Job current;                               // written by the callback while closed
};
} // namespace signalpatch

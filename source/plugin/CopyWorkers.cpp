#include "CopyWorkers.h"

#include <juce_audio_basics/juce_audio_basics.h>

#if JUCE_WINDOWS
 #ifndef NOMINMAX
  #define NOMINMAX
 #endif
 #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
 #endif
 #include <windows.h>
#elif JUCE_MAC || JUCE_IOS
 #include <dispatch/dispatch.h>
 #include <pthread.h>
#else
 #include <cerrno>
 #include <pthread.h>
 #include <sched.h>
 #include <semaphore.h>
#endif

#if JUCE_INTEL
 #include <immintrin.h>
#elif JUCE_ARM && JUCE_MSVC
 #include <intrin.h>
#endif

#include <array>
#include <climits>
#include <mutex>
#include <system_error>

namespace mperoll
{

// The ticket must be lock-free, or taking an index could wait for a lock on the audio thread.
static_assert (std::atomic<uint64_t>::is_always_lock_free);

namespace
{
    // The index a ticket holds while run() replaces a batch's job and count: above any count.
    constexpr uint64_t closedIndex = 0xffffffffu;

    /** Tells the CPU this is a spin-wait, which spares the other half of a hyper-threaded core. */
    inline void pauseCpu() noexcept
    {
       #if JUCE_INTEL
        _mm_pause();
       #elif JUCE_ARM && JUCE_MSVC
        __yield();
       #elif JUCE_ARM
        __asm__ __volatile__ ("yield");
       #endif
    }

   #if JUCE_WINDOWS
    using AvSetMmThreadCharacteristicsFn = HANDLE (WINAPI*) (LPCWSTR, LPDWORD);
    using AvRevertMmThreadCharacteristicsFn = BOOL (WINAPI*) (HANDLE);

    template <typename Function>
    Function getFunction (HMODULE module, const char* name)
    {
        // Through void (*)(), which compilers take as standing for any function type.
        return reinterpret_cast<Function> (reinterpret_cast<void (*)()> (GetProcAddress (module, name)));
    }

    /** The multimedia class scheduler (MMCSS), from avrt.dll where Windows has it: only Windows' own
        copy, never one that happens to sit in the host's folder. Loaded once and never unloaded:
        it's part of Windows, and threads may be in it when the plugin goes away.
    */
    struct Mmcss
    {
        Mmcss()
        {
            if (auto* module = LoadLibraryExW (L"avrt.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32))
            {
                join = getFunction<AvSetMmThreadCharacteristicsFn> (module, "AvSetMmThreadCharacteristicsW");
                leave = getFunction<AvRevertMmThreadCharacteristicsFn> (module, "AvRevertMmThreadCharacteristics");
            }
        }

        AvSetMmThreadCharacteristicsFn join = nullptr;
        AvRevertMmThreadCharacteristicsFn leave = nullptr;
    };

    const Mmcss& getMmcss()
    {
        static const Mmcss mmcss;
        return mmcss;
    }

    // This worker's place in the "Pro Audio" task, given back when the worker ends.
    thread_local HANDLE mmcssTask = nullptr;

    void leaveMmcss() noexcept
    {
        if (mmcssTask != nullptr && getMmcss().leave != nullptr)
            getMmcss().leave (mmcssTask);

        mmcssTask = nullptr;
    }
   #endif
}

//==============================================================================
/** Counts the wake-ups owed to the workers. release() takes no lock (it's one atomic step, plus a
    call into the system when a worker is asleep), so the audio thread may call it.
*/
class CopyWorkers::Semaphore
{
public:
    Semaphore()
    {
       #if JUCE_WINDOWS
        handle = CreateSemaphoreW (nullptr, 0, LONG_MAX, nullptr);
       #elif JUCE_MAC || JUCE_IOS
        handle = dispatch_semaphore_create (0);
       #else
        created = sem_init (&handle, 0, 0) == 0;
       #endif
    }

    ~Semaphore()
    {
       #if JUCE_WINDOWS
        if (handle != nullptr)
            CloseHandle (handle);
       #elif JUCE_MAC || JUCE_IOS
        if (handle != nullptr)
            dispatch_release (handle);
       #else
        if (created)
            sem_destroy (&handle);
       #endif
    }

    bool isValid() const noexcept
    {
       #if JUCE_WINDOWS || JUCE_MAC || JUCE_IOS
        return handle != nullptr;
       #else
        return created;
       #endif
    }

    void release (int count) noexcept
    {
        if (count <= 0)
            return;

       #if JUCE_WINDOWS
        ReleaseSemaphore (handle, count, nullptr);
       #elif JUCE_MAC || JUCE_IOS
        for (int i = 0; i < count; ++i)
            dispatch_semaphore_signal (handle);
       #else
        for (int i = 0; i < count; ++i)
            sem_post (&handle);
       #endif
    }

    void wait() noexcept
    {
       #if JUCE_WINDOWS
        WaitForSingleObject (handle, INFINITE);
       #elif JUCE_MAC || JUCE_IOS
        dispatch_semaphore_wait (handle, DISPATCH_TIME_FOREVER);
       #else
        // A signal handler running on this thread interrupts the wait: wait again.
        while (sem_wait (&handle) != 0 && errno == EINTR)
        {
        }
       #endif
    }

private:
   #if JUCE_WINDOWS
    HANDLE handle = nullptr;
   #elif JUCE_MAC || JUCE_IOS
    dispatch_semaphore_t handle = nullptr;
   #else
    sem_t handle {};
    bool created = false;
   #endif

    JUCE_DECLARE_NON_COPYABLE (Semaphore)
};

//==============================================================================
/** A worker thread. A juce::Thread rather than a std::thread for the stack size it lets us ask
    for: a synth gets as much room as on a host's audio thread, where a std::thread would have the
    program's default (1 MB on Windows, 512 KB on macOS) and a synth that recurses deeply could
    overflow it. Started at normal priority (juce::Thread's real-time start would raise FL's whole
    process) and raised from inside.
*/
class CopyWorkers::Worker final : public juce::Thread
{
public:
    Worker (CopyWorkers& o, int i)
        : Thread ("MPE Roll copies", 8 * 1024 * 1024), owner (o), index (i)
    {
    }

    void run() override     { owner.workerLoop (index); }

private:
    CopyWorkers& owner;
    const int index;
};

CopyWorkers::CopyWorkers (int numWorkers, bool atAudioPriority)
    : wakeUp (std::make_unique<Semaphore>()), audioPriority (atAudioPriority)
{
    // Workers that couldn't sleep would spin: without the semaphore, the caller does it all.
    if (! wakeUp->isValid())
        return;

    threads.reserve ((size_t) juce::jmax (0, numWorkers));

    for (int i = 0; i < numWorkers; ++i)
    {
        auto worker = std::make_unique<Worker> (*this, i);

        // The system won't start another thread: make do with the ones there are.
        if (! worker->startThread())
            break;

        threads.push_back (std::move (worker));
    }
}

CopyWorkers::~CopyWorkers()
{
    quit = true;
    wakeUp->release ((int) threads.size());

    for (auto& worker : threads)
        worker->waitForThreadToExit (-1);
}

int CopyWorkers::suggestedWorkers()
{
    return juce::jlimit (1, 7, juce::SystemStats::getNumPhysicalCpus() - 1);
}

std::shared_ptr<CopyWorkers> CopyWorkers::getShared (bool atAudioPriority)
{
    static std::mutex lock;
    static std::array<std::weak_ptr<CopyWorkers>, 2> shared;

    const std::scoped_lock sl (lock);
    auto& slot = shared[atAudioPriority ? 1 : 0];

    if (auto existing = slot.lock())
        return existing;

    auto made = std::make_shared<CopyWorkers> (suggestedWorkers(), atAudioPriority);
    slot = made;
    return made;
}

void CopyWorkers::run (int count, Job job, void* context) noexcept
{
    finish (begin (count, job, context, false));
}

CopyWorkers::Batch CopyWorkers::begin (int count, Job job, void* context, bool callerBusy) noexcept
{
    Batch b { juce::jmax (0, count), job, context, false };

    // One job, no workers, or the workers busy with another MPE Roll's block: all done here, by finish().
    if (count <= 1 || threads.empty() || taken.exchange (true, std::memory_order_acquire))
        return b;

    // The last batch's ticket is closed before its job and count are replaced: see work().
    ticket.store (((uint64_t) batch << 32) | closedIndex, std::memory_order_relaxed);

    batchJob.store (job, std::memory_order_relaxed);
    batchContext.store (context, std::memory_order_relaxed);
    batchCount.store (count, std::memory_order_release);
    remaining.store (count, std::memory_order_relaxed);

    ++batch;
    ticket.store ((uint64_t) batch << 32, std::memory_order_release);

    // As many as there are jobs, less the one the caller takes straight away if it isn't busy first.
    wakeUp->release (juce::jmin (getNumWorkers(), callerBusy ? count : count - 1));
    b.shared = true;
    return b;
}

void CopyWorkers::finish (const Batch& b) noexcept
{
    if (! b.shared)
    {
        for (int i = 0; i < b.count; ++i)
        {
            juce::ScopedNoDenormals noDenormals;
            b.job (b.context, i);
        }

        return;
    }

    work (batch);

    // Jobs the workers took may still be running. Spinning keeps this wait as short as it can be;
    // after a while the core is offered to other threads between looks, in case one of them is
    // a worker waiting for it.
    for (int spins = 0; remaining.load (std::memory_order_acquire) > 0;)
    {
        if (spins < 4000)
        {
            ++spins;
            pauseCpu();
        }
        else
        {
            std::this_thread::yield();
        }
    }

    taken.store (false, std::memory_order_release);
}

void CopyWorkers::workerLoop (int index)
{
    juce::Thread::setCurrentThreadName ("MPE Roll copies");

    if (audioPriority)
        raisePriority (index);

    for (;;)
    {
        wakeUp->wait();

        if (quit.load (std::memory_order_acquire))
            break;

        work ((uint32_t) (ticket.load (std::memory_order_acquire) >> 32));
    }

   #if JUCE_WINDOWS
    leaveMmcss();
   #endif
}

void CopyWorkers::work (uint32_t batchNumber) noexcept
{
    auto current = ticket.load (std::memory_order_acquire);

    for (;;)
    {
        const auto index = (uint32_t) (current & 0xffffffffu);

        // A worker may wake late: after the batch it was woken for has finished, or while a later
        // one is being set up or run. The batch number it brought comes from the ticket, and the
        // compare-and-swap below only takes an index while the ticket still holds that number, so
        // it can't take one of a later batch.
        //
        // Nor one past the end of its own: the count it reads may already be the next batch's,
        // and the ticket of a finished batch still holds its number. That's why run() closes the
        // ticket before replacing the count: a thread that sees the new count (an acquire, after
        // run()'s release) also sees the closed ticket, and its compare-and-swap fails.
        //
        // So when the compare-and-swap succeeds, the ticket held this batch and an index below
        // its count. The batch can't finish until that index has run, so run() is still waiting
        // in it, and the job and context read below are this batch's.
        if ((uint32_t) (current >> 32) != batchNumber
            || index >= (uint32_t) batchCount.load (std::memory_order_acquire))
            return;

        if (! ticket.compare_exchange_weak (current, current + 1, std::memory_order_acq_rel, std::memory_order_acquire))
            continue;

        {
            juce::ScopedNoDenormals noDenormals;
            batchJob.load (std::memory_order_relaxed) (batchContext.load (std::memory_order_relaxed), (int) index);
        }

        remaining.fetch_sub (1, std::memory_order_release);
        current = ticket.load (std::memory_order_acquire);
    }
}

void CopyWorkers::raisePriority (int index)
{
    juce::ignoreUnused (index);

   #if JUCE_WINDOWS
    SetThreadPriority (GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);

    if (const auto& mmcss = getMmcss(); mmcss.join != nullptr)
    {
        DWORD taskIndex = 0;
        mmcssTask = mmcss.join (L"Pro Audio", &taskIndex);
    }
   #elif JUCE_MAC || JUCE_IOS
    pthread_set_qos_class_self_np (QOS_CLASS_USER_INTERACTIVE, 0);
   #else
    // Real-time scheduling needs a permission (rtprio) many systems don't give: then the workers
    // simply stay at normal priority. Middling, so they don't take over from the host's own.
    const auto lowest = sched_get_priority_min (SCHED_FIFO);
    const auto highest = sched_get_priority_max (SCHED_FIFO);

    sched_param param {};
    param.sched_priority = lowest + (highest - lowest) / 2;
    pthread_setschedparam (pthread_self(), SCHED_FIFO, &param);
   #endif
}

} // namespace mperoll

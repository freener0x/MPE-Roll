#pragma once

#include <juce_core/juce_core.h>

#include <array>
#include <atomic>
#include <memory>
#include <thread>
#include <vector>

namespace mperoll
{

/** Spreads the copies of a synth over CPU cores, for one copy per note.

    run() hands out the indices 0..count-1: the calling thread (the audio thread) and a few worker
    threads of MPE Roll's own each take the next index not yet taken and process it, until none
    are left; then the caller waits for the ones still being processed. The caller works too, so
    a block never waits for a worker that hasn't woken up: at worst the caller does it all itself,
    as if there were no workers.

    The workers run at the priority of audio threads: on Windows time-critical, in the "Pro Audio"
    class of the multimedia scheduler (MMCSS) where it's available, and never by raising FL
    Studio's whole process to real-time (as juce::Thread's real-time threads would); on Linux
    SCHED_FIFO where allowed; on macOS the user-interactive QoS. Denormals are flushed to zero
    around each job. They sleep on a semaphore between blocks, so they cost nothing while idle.

    Workers for offline rendering only (an audio export) stay at normal priority, so they don't
    take the cores from the audio threads playing live meanwhile.

    Every MPE Roll in the process shares one set (getShared()), so two of them in copy mode don't
    start twice as many threads as there are cores. Up to four callers' batches are run at once,
    the workers taking jobs from any of them; a fifth caller at the same moment does its jobs on
    its own thread. Made and deleted on the message thread, never while run() is in progress.
*/
class CopyWorkers
{
public:
    using Job = void (*) (void* context, int index) noexcept;

    /** Starts the threads, at audio priority or, for offline rendering only, at normal priority. */
    explicit CopyWorkers (int numWorkers, bool atAudioPriority = true);

    /** Stops the threads and waits for them. */
    ~CopyWorkers();

    /** Worker threads to use on this computer: one fewer than its physical cores, 1 to 7. */
    static int suggestedWorkers();

    /** The process's workers at this priority (suggestedWorkers() of them), made when first asked
        for and stopped when the last user lets go. Message thread. */
    static std::shared_ptr<CopyWorkers> getShared (bool atAudioPriority);

    int getNumWorkers() const noexcept                  { return (int) threads.size(); }

    /** Calls job (context, i) once for every i in 0..count-1, spread over this thread and the
        workers, and returns once every call has returned. Any thread; while four other threads'
        batches have the workers, the calls are all made on this one. Real-time safe: no
        allocation and no lock; it only signals a semaphore to wake the workers.
    */
    void run (int count, Job job, void* context) noexcept;

    /** run() in two halves, for a caller with something of its own to do meanwhile: begin() sets
        the workers going on the jobs (callerBusy: all of them, the caller being busy first) and
        finish() takes the rest on this thread and waits for the workers. Every begin() must be
        followed by its finish(), on the same thread.
    */
    struct Batch
    {
        int count = 0;
        Job job = nullptr;
        void* context = nullptr;
        int slot = -1;                  // the workers have it (otherwise finish() does it all)
        bool shared = false;
    };

    Batch begin (int count, Job job, void* context, bool callerBusy) noexcept;
    void finish (const Batch&) noexcept;

    static constexpr int numSlots = 4;  // batches run at once

private:
    class Semaphore;
    class Worker;

    /** A batch being run. ticket holds the batch's number in its top 32 bits and the next index to
        hand out in its bottom 32: an index is taken by a compare-and-swap that also checks the
        batch, so a worker that wakes late can never take an index of a later batch. */
    struct Slot
    {
        std::atomic<bool> taken { false };      // a caller has it, until its finish()
        std::atomic<uint64_t> ticket { 0 };
        std::atomic<int> remaining { 0 };
        std::atomic<int> count { 0 };
        std::atomic<Job> job { nullptr };
        std::atomic<void*> context { nullptr };
        uint32_t batch = 0;                     // its caller's
    };

    std::unique_ptr<Semaphore> wakeUp;
    std::vector<std::unique_ptr<Worker>> threads;
    std::atomic<bool> quit { false };
    const bool audioPriority;
    std::array<Slot, numSlots> slots;

    void workerLoop (int index);
    static void work (Slot&, uint32_t batchNumber) noexcept;
    static void raisePriority (int index);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CopyWorkers)
};

} // namespace mperoll

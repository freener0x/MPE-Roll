#pragma once

#include <juce_core/juce_core.h>

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

    One instance per hosted synth; made and deleted on the message thread, never while run() is
    in progress.
*/
class CopyWorkers
{
public:
    using Job = void (*) (void* context, int index) noexcept;

    /** Starts the threads. */
    explicit CopyWorkers (int numWorkers);

    /** Stops the threads and waits for them. */
    ~CopyWorkers();

    /** Worker threads to use on this computer: one fewer than its physical cores, 1 to 7. */
    static int suggestedWorkers();

    int getNumWorkers() const noexcept                  { return (int) threads.size(); }

    /** Calls job (context, i) once for every i in 0..count-1, spread over this thread and the
        workers, and returns once every call has returned. One run at a time. Real-time safe:
        no allocation and no lock; it only signals a semaphore to wake the workers.
    */
    void run (int count, Job job, void* context) noexcept;

private:
    class Semaphore;
    class Worker;

    std::unique_ptr<Semaphore> wakeUp;
    std::vector<std::unique_ptr<Worker>> threads;
    std::atomic<bool> quit { false };

    // The batch being run. ticket holds the batch's number in its top 32 bits and the next index
    // to hand out in its bottom 32: an index is taken by a compare-and-swap that also checks the
    // batch, so a worker that wakes late can never take an index of a later batch.
    std::atomic<uint64_t> ticket { 0 };
    std::atomic<int> remaining { 0 };
    std::atomic<int> batchCount { 0 };
    std::atomic<Job> batchJob { nullptr };
    std::atomic<void*> batchContext { nullptr };
    uint32_t batch = 0;

    void workerLoop (int index);
    void work (uint32_t batchNumber) noexcept;
    static void raisePriority (int index);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CopyWorkers)
};

} // namespace mperoll

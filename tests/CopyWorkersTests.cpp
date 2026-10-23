#include "plugin/CopyWorkers.h"
#include "plugin/CopySafety.h"

#include <juce_core/juce_core.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <set>
#include <stdexcept>
#include <thread>

namespace mperoll
{

class CopyWorkersTests : public juce::UnitTest
{
public:
    CopyWorkersTests() : juce::UnitTest ("CopyWorkers", "MPE Roll") {}

    static constexpr int maxJobs = 64;

    /** What the jobs of one run() leave behind. The flags and thread ids are plain on purpose:
        run() returning must be what makes them safe to read (a thread sanitizer checks that).
    */
    struct Record
    {
        std::array<std::atomic<int>, maxJobs> calls {};
        std::array<bool, maxJobs> finished {};
        std::array<std::thread::id, maxJobs> threads {};
        std::chrono::microseconds jobLength { 20 };
        bool sleep = false;

        void reset()
        {
            for (auto& c : calls)
                c = 0;

            finished.fill (false);
            threads.fill ({});
        }
    };

    static void spinFor (std::chrono::microseconds length)
    {
        const auto end = std::chrono::steady_clock::now() + length;

        while (std::chrono::steady_clock::now() < end)
        {
        }
    }

    static void recordJob (void* context, int index) noexcept
    {
        auto& record = *static_cast<Record*> (context);
        const auto i = (size_t) index;

        record.calls[i].fetch_add (1);
        record.threads[i] = std::this_thread::get_id();

        // Some sleep, the rest keep their core busy, so jobs finish in a jumbled order.
        if (record.sleep || index % 3 == 0)
            std::this_thread::sleep_for (record.jobLength);
        else
            spinFor (record.jobLength);

        record.finished[i] = true;
    }

    /** For back-to-back batches: each job notes the batch it believes it's in. */
    struct Batches
    {
        static constexpr int slots = 16;

        int number = 0;                                 // plain: set before each run()
        int count = 0;
        std::array<std::atomic<int>, slots> calls {};
        std::array<int, slots> ranIn {};
        std::atomic<int> outOfRange { 0 };
    };

    static void batchJob (void* context, int index) noexcept
    {
        auto& batches = *static_cast<Batches*> (context);

        if (index < 0 || index >= batches.count || index >= Batches::slots)
        {
            batches.outOfRange.fetch_add (1);
            return;
        }

        batches.calls[(size_t) index].fetch_add (1);
        batches.ranIn[(size_t) index] = batches.number;
    }

    void runTest() override
    {
        beginTest ("Each index runs exactly once, and run() returns only once all have finished");
        {
            Record record;

            for (const int numWorkers : { 0, 1, 3 })
            {
                CopyWorkers workers (numWorkers);
                expectEquals (workers.getNumWorkers(), numWorkers);

                for (const int count : { 0, 1, 2, 7, 64 })
                {
                    const auto label = juce::String (numWorkers) + " workers, " + juce::String (count) + " jobs";
                    record.reset();
                    workers.run (count, recordJob, &record);

                    int wrongCalls = 0, unfinished = 0, onOtherThreads = 0;

                    for (int i = 0; i < maxJobs; ++i)
                    {
                        const auto slot = (size_t) i;
                        wrongCalls += record.calls[slot].load() != (i < count ? 1 : 0) ? 1 : 0;
                        unfinished += record.finished[slot] != (i < count) ? 1 : 0;
                        onOtherThreads += i < count && record.threads[slot] != std::this_thread::get_id() ? 1 : 0;
                    }

                    expectEquals (wrongCalls, 0, label);
                    expectEquals (unfinished, 0, label);

                    // With nothing to share, the calling thread does it all, without waking anyone.
                    if (count <= 1 || numWorkers == 0)
                        expectEquals (onOtherThreads, 0, label);
                }
            }
        }

        beginTest ("Back-to-back batches never run an index twice or miss one");
        {
            // Tiny jobs in quick succession, so workers often wake after their batch has finished,
            // or while the next is being set up. Counts go up and down, so a late worker could be
            // tempted by an index its own batch didn't have.
            CopyWorkers workers (3);
            Batches batches;
            juce::Random random (20251023);
            int wrong = 0;

            for (int n = 1; n <= 20000; ++n)
            {
                const auto count = 1 + random.nextInt (8);
                batches.number = n;
                batches.count = count;

                for (auto& c : batches.calls)
                    c.store (0, std::memory_order_relaxed);

                workers.run (count, batchJob, &batches);

                for (int i = 0; i < Batches::slots; ++i)
                {
                    const auto slot = (size_t) i;
                    const auto expected = i < count ? 1 : 0;

                    if (batches.calls[slot].load() != expected || (i < count && batches.ranIn[slot] != n))
                        ++wrong;
                }
            }

            expectEquals (wrong, 0);
            expectEquals (batches.outOfRange.load(), 0);
        }

        beginTest ("Jobs are spread over several threads");
        {
            // Jobs of about 1 ms each: the workers have time to wake and take some. On a busy
            // computer they might not get a core in time, so it has a few tries.
            CopyWorkers workers (3);
            Record record;
            record.jobLength = std::chrono::milliseconds (1);
            record.sleep = true;
            int mostThreads = 0;

            for (int attempt = 0; attempt < 10 && mostThreads < 2; ++attempt)
            {
                record.reset();
                workers.run (16, recordJob, &record);

                std::set<std::thread::id> threads;

                for (int i = 0; i < 16; ++i)
                    threads.insert (record.threads[(size_t) i]);

                mostThreads = std::max (mostThreads, (int) threads.size());
            }

            expectGreaterOrEqual (mostThreads, 2);
        }

        beginTest ("Workers stop cleanly, whether they ran anything or not");
        {
            for (int i = 0; i < 20; ++i)
            {
                CopyWorkers workers (3);
            }

            Record record;
            record.jobLength = std::chrono::microseconds (5);

            for (int i = 0; i < 20; ++i)
            {
                record.reset();

                {
                    CopyWorkers workers (3);
                    workers.run (8, recordJob, &record);
                }

                int calls = 0;

                for (auto& c : record.calls)
                    calls += c.load();

                expectEquals (calls, 8);
            }
        }

        beginTest ("The suggested number of workers is 1 to 7");
        {
            const auto suggested = CopyWorkers::suggestedWorkers();
            expectGreaterOrEqual (suggested, 1);
            expectLessOrEqual (suggested, 7);
        }

        beginTest ("Two threads running batches on the same workers at once each get all theirs done");
        {
            // Two MPE Rolls' audio threads sharing the process's workers: whichever finds them
            // taken does its own jobs itself.
            CopyWorkers workers (3);
            std::array<Batches, 2> batches;
            std::array<int, 2> wrong {};
            std::atomic<bool> go { false };

            auto play = [&] (int who)
            {
                auto& mine = batches[(size_t) who];
                juce::Random random (who + 7);

                while (! go.load())
                    std::this_thread::yield();

                for (int n = 1; n <= 5000; ++n)
                {
                    const auto count = 1 + random.nextInt (8);
                    mine.number = n;
                    mine.count = count;

                    for (auto& c : mine.calls)
                        c.store (0, std::memory_order_relaxed);

                    workers.run (count, batchJob, &mine);

                    for (int i = 0; i < Batches::slots; ++i)
                        if (mine.calls[(size_t) i].load() != (i < count ? 1 : 0) || (i < count && mine.ranIn[(size_t) i] != n))
                            ++wrong[(size_t) who];
                }
            };

            std::thread other (play, 1);
            go = true;
            play (0);
            other.join();

            expectEquals (wrong[0] + wrong[1], 0);
            expectEquals (batches[0].outOfRange.load() + batches[1].outOfRange.load(), 0);
        }

        beginTest ("begin() sets the workers going while the caller does something else, and finish() completes it");
        {
            CopyWorkers workers (3);
            Record record;
            record.jobLength = std::chrono::microseconds (200);

            for (const int count : { 0, 1, 2, 9 })
            {
                record.reset();
                const auto batch = workers.begin (count, recordJob, &record, true);
                std::this_thread::sleep_for (std::chrono::milliseconds (2));     // the caller's own work
                workers.finish (batch);

                int wrongCalls = 0, unfinished = 0;

                for (int i = 0; i < maxJobs; ++i)
                {
                    wrongCalls += record.calls[(size_t) i].load() != (i < count ? 1 : 0) ? 1 : 0;
                    unfinished += record.finished[(size_t) i] != (i < count) ? 1 : 0;
                }

                expectEquals (wrongCalls, 0, juce::String (count) + " jobs");
                expectEquals (unfinished, 0, juce::String (count) + " jobs");
            }
        }

        beginTest ("The process's workers are shared while used, and stop when the last user lets go");
        {
            auto first = CopyWorkers::getShared (true);
            auto second = CopyWorkers::getShared (true);
            auto offline = CopyWorkers::getShared (false);
            expect (first != nullptr && first == second, "one set for the whole process");
            expect (offline != first, "and another for offline rendering");
            expectEquals (first->getNumWorkers(), CopyWorkers::suggestedWorkers());

            std::weak_ptr<CopyWorkers> watch = first;
            first.reset();
            second.reset();
            expect (watch.expired(), "gone once nothing uses it");
        }
    }
};

static CopyWorkersTests copyWorkersTests;

//==============================================================================
class CopySafetyTests : public juce::UnitTest
{
public:
    CopySafetyTests() : juce::UnitTest ("CopySafety", "MPE Roll") {}

    static void addOne (void* context)
    {
        ++*static_cast<int*> (context);
    }

    void runTest() override
    {
        beginTest ("call() calls the function and returns true");
        {
            int calls = 0;
            unsigned int code = 0;
            expect (CopySafety::call (addOne, &calls, &code));
            expectEquals (calls, 1);

            expect (CopySafety::call (addOne, &calls));
            expectEquals (calls, 2);
        }

        beginTest ("Exception codes are described in words");
        {
            using CopySafety::describeException;
            expectEquals (describeException (0xc0000005u), juce::String ("an access violation"));
            expectEquals (describeException (0xc00000fdu), juce::String ("a stack overflow"));
            expectEquals (describeException (0xc000001du), juce::String ("an illegal instruction"));
            expectEquals (describeException (0xc0000096u), juce::String ("a privileged instruction"));
            expectEquals (describeException (0xc0000094u), juce::String ("an integer division by zero"));
            expectEquals (describeException (0xc0000006u), juce::String ("an in-page error"));
            expectEquals (describeException (0xc000008cu), juce::String ("an array bounds error"));
            expectEquals (describeException (0xc000008eu), juce::String ("a floating-point error"));
            expectEquals (describeException (0xc00002b4u), juce::String ("a floating-point error"));
            expectEquals (describeException (0xc00002b5u), juce::String ("a floating-point error"));
            expectEquals (describeException (0xe06d7363u), juce::String ("a C++ exception the synth didn't catch"));
            expectEquals (describeException (0x80000003u), juce::String ("exception 0x80000003"));
            expectEquals (describeException (0x12345678u), juce::String ("exception 0x12345678"));
            expectEquals (describeException (0x2au), juce::String ("exception 0x0000002a"));
        }

        beginTest ("Memory figures, and pretending to have less");
        {
           #if JUCE_LINUX || JUCE_WINDOWS || JUCE_MAC
            expectGreaterThan (CopySafety::getAvailableMemoryMB(), (juce::int64) 0);
           #else
            expectEquals (CopySafety::getAvailableMemoryMB(), (juce::int64) -1);
           #endif

           #if JUCE_LINUX || JUCE_WINDOWS
            expectGreaterThan (CopySafety::getProcessMemoryMB(), (juce::int64) 0);
           #else
            expectEquals (CopySafety::getProcessMemoryMB(), (juce::int64) -1);
           #endif

            const auto known = CopySafety::getAvailableMemoryMB() > 0;

            CopySafety::pretendAvailableMemoryMB (123);
            expectEquals (CopySafety::getAvailableMemoryMB(), (juce::int64) 123);

            CopySafety::pretendAvailableMemoryMB (0);
            expectEquals (CopySafety::getAvailableMemoryMB(), (juce::int64) 0);

            CopySafety::pretendAvailableMemoryMB (-1);
            expectEquals (CopySafety::getAvailableMemoryMB(), (juce::int64) -1);

            CopySafety::pretendAvailableMemoryMB (-2);
            expect (known ? CopySafety::getAvailableMemoryMB() > 0 : CopySafety::getAvailableMemoryMB() == -1);
        }

        beginTest ("A C++ exception that escapes inside call() is caught everywhere");
        {
            unsigned int code = 0;
            expect (! CopySafety::call ([] (void*) { throw std::runtime_error ("from the synth"); }, nullptr, &code));
            expectEquals (juce::String::toHexString (code), juce::String ("e06d7363"));

            int calls = 0;
            expect (CopySafety::call (addOne, &calls));
            expectEquals (calls, 1);
        }

        if (CopySafety::canCatchCrashes())
        {
            beginTest ("A crash inside call() is caught, and the program carries on");
            {
                unsigned int code = 0;
                const auto writeToNowhere = [] (void*)
                {
                    int* volatile nowhere = nullptr;
                    *nowhere = 1;
                };

                expect (! CopySafety::call (writeToNowhere, nullptr, &code));
                expectEquals (juce::String::toHexString (code), juce::String ("c0000005"));

                int calls = 0;
                expect (CopySafety::call (addOne, &calls));
                expectEquals (calls, 1);
            }
        }
    }
};

static CopySafetyTests copySafetyTests;

} // namespace mperoll

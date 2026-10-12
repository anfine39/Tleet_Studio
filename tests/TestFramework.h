#pragma once

// Minimal self-contained test harness (no external dependency to download).
// Each TEST registers itself; CHECK records failures with file/line and the
// measured values, and every run writes a JSON report next to the binary.

#include <juce_core/juce_core.h>
#include <functional>
#include <vector>
#include <atomic>

namespace tleet::test
{
    struct TestCase
    {
        const char* name;
        std::function<void()> fn;
    };

    std::vector<TestCase>& registry();
    void recordFailure (const char* file, int line, const juce::String& message);
    void recordMeasurement (const juce::String& key, double value);

    struct Registrar
    {
        Registrar (const char* name, std::function<void()> fn) { registry().push_back ({ name, std::move (fn) }); }
    };

    // Heap allocation counter (enabled only around code under test).
    extern std::atomic<bool> countAllocations;
    extern std::atomic<long> allocationCount;

    struct ScopedAllocationCounter
    {
        ScopedAllocationCounter()  { allocationCount = 0; countAllocations = true; }
        ~ScopedAllocationCounter() { countAllocations = false; }
        long count() const { return allocationCount.load(); }
    };

    juce::File outputDirectory();
}

#define TLEET_CONCAT2(a, b) a##b
#define TLEET_CONCAT(a, b) TLEET_CONCAT2 (a, b)
#define TEST(name) \
    static void TLEET_CONCAT (test_fn_, __LINE__)(); \
    static tleet::test::Registrar TLEET_CONCAT (test_reg_, __LINE__) (name, TLEET_CONCAT (test_fn_, __LINE__)); \
    static void TLEET_CONCAT (test_fn_, __LINE__)()

#define CHECK(cond) \
    do { if (! (cond)) tleet::test::recordFailure (__FILE__, __LINE__, "CHECK(" #cond ")"); } while (0)

#define CHECK_MSG(cond, msg) \
    do { if (! (cond)) tleet::test::recordFailure (__FILE__, __LINE__, juce::String ("CHECK(" #cond ") ") + (msg)); } while (0)

#define CHECK_NEAR(a, b, tol) \
    do { const double va_ = (double) (a), vb_ = (double) (b); \
         if (! (std::abs (va_ - vb_) <= (double) (tol))) \
             tleet::test::recordFailure (__FILE__, __LINE__, juce::String ("CHECK_NEAR(" #a ", " #b ") ") \
                 + juce::String (va_, 6) + " vs " + juce::String (vb_, 6) + " tol " + juce::String ((double) (tol), 6)); } while (0)

#define MEASURE(key, value) tleet::test::recordMeasurement (key, (double) (value))

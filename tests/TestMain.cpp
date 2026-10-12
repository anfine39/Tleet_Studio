#include "TestFramework.h"
#include <juce_events/juce_events.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <cstdlib>
#include <new>

namespace tleet::test
{
    std::atomic<bool> countAllocations { false };
    std::atomic<long> allocationCount { 0 };
    thread_local bool allocationCountingThread = false;

    std::vector<TestCase>& registry()
    {
        static std::vector<TestCase> r;
        return r;
    }

    struct Failure { juce::String test, where, message; };
    static std::vector<Failure> failures;
    static std::vector<std::pair<juce::String, double>> measurements;
    static juce::String currentTest;

    void recordFailure (const char* file, int line, const juce::String& message)
    {
        const auto where = juce::File (file).getFileName() + ":" + juce::String (line);
        failures.push_back ({ currentTest, where, message });
        std::printf ("    FAIL %s  %s\n", where.toRawUTF8(), message.toRawUTF8());
    }

    void recordMeasurement (const juce::String& key, double value)
    {
        measurements.emplace_back (currentTest + " :: " + key, value);
        std::printf ("    %-60s %.6g\n", key.toRawUTF8(), value);
    }

    juce::File outputDirectory()
    {
        auto dir = juce::File::getCurrentWorkingDirectory().getChildFile ("test-output");
        dir.createDirectory();
        return dir;
    }
}

// Global allocation hooks: count heap allocations while a counter is active.
void* operator new (std::size_t size)
{
    if (tleet::test::countAllocations.load (std::memory_order_relaxed) && tleet::test::allocationCountingThread)
        tleet::test::allocationCount.fetch_add (1, std::memory_order_relaxed);
    if (void* p = std::malloc (size == 0 ? 1 : size))
        return p;
    throw std::bad_alloc();
}
void* operator new[] (std::size_t size) { return operator new (size); }
void operator delete (void* p) noexcept { std::free (p); }
void operator delete[] (void* p) noexcept { std::free (p); }
void operator delete (void* p, std::size_t) noexcept { std::free (p); }
void operator delete[] (void* p, std::size_t) noexcept { std::free (p); }

int main (int argc, char** argv)
{
    using namespace tleet::test;
    juce::ScopedJuceInitialiser_GUI gui;

    const juce::String filter = argc > 1 ? juce::String (argv[1]) : juce::String();
    int run = 0, failedTests = 0;

    for (auto& t : registry())
    {
        if (filter.isNotEmpty() && ! juce::String (t.name).containsIgnoreCase (filter))
            continue;

        currentTest = t.name;
        const auto before = failures.size();
        std::printf ("[ RUN  ] %s\n", t.name);
        const auto start = juce::Time::getMillisecondCounterHiRes();
        t.fn();
        const auto ms = juce::Time::getMillisecondCounterHiRes() - start;
        const bool ok = failures.size() == before;
        std::printf ("[ %s ] %s (%.0f ms)\n", ok ? " OK " : "FAIL", t.name, ms);
        ++run;
        if (! ok) ++failedTests;
    }

    // JSON report for CI artifacts
    juce::DynamicObject::Ptr report = new juce::DynamicObject();
    report->setProperty ("testsRun", run);
    report->setProperty ("testsFailed", failedTests);
    juce::Array<juce::var> fails, meas;
    for (auto& f : failures)
    {
        juce::DynamicObject::Ptr o = new juce::DynamicObject();
        o->setProperty ("test", f.test);
        o->setProperty ("where", f.where);
        o->setProperty ("message", f.message);
        fails.add (juce::var (o.get()));
    }
    for (auto& [k, v] : measurements)
    {
        juce::DynamicObject::Ptr o = new juce::DynamicObject();
        o->setProperty ("key", k);
        o->setProperty ("value", v);
        meas.add (juce::var (o.get()));
    }
    report->setProperty ("failures", fails);
    report->setProperty ("measurements", meas);
    outputDirectory().getChildFile ("report.json").replaceWithText (juce::JSON::toString (juce::var (report.get())));

    std::printf ("\n%d tests, %d failed\n", run, failedTests);
    return failedTests == 0 ? 0 : 1;
}

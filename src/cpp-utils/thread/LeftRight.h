#include <atomic>
#include <functional>
#include <mutex>
#include <thread>
#include <cpp-utils/macros.h>
#include <array>

namespace cpputils {

namespace detail {

struct IncrementRAII final {
public:
    explicit IncrementRAII(std::atomic<int32_t> *counter): _counter(counter) {
        ++(*_counter);
    }

    ~IncrementRAII() {
        --(*_counter);
    }
private:
    std::atomic<int32_t> *_counter;

    DISALLOW_COPY_AND_ASSIGN(IncrementRAII);
};

}

// LeftRight wait-free readers synchronization primitive
// https://hal.archives-ouvertes.fr/hal-01207881/document
template <class T>
class LeftRight final {
public:
    LeftRight()
    : _writeMutex()
    , _foregroundCounterIndex{0}
    , _foregroundDataIndex{0}
    , _counters{{{0}, {0}}}
    , _data{{{}, {}}}
    , _inDestruction(false) {}

    ~LeftRight() {
        // from now on, no new readers/writers will be accepted (see asserts in read()/write())
        _inDestruction = true;

        // wait until any potentially running writers are finished
        {
            std::unique_lock<std::mutex> lock(_writeMutex);
        }

        // wait until any potentially running readers are finished
        while (_counters[0].load() != 0 || _counters[1].load() != 0) {
            std::this_thread::yield();
        }
    }

    template <typename F>
    auto read(F&& readFunc) const {
        if(_inDestruction.load()) {
            throw std::logic_error("Issued LeftRight::read() after the destructor started running");
        }

        detail::IncrementRAII _increment_counter(&_counters[_foregroundCounterIndex.load()]); // NOLINT(cppcoreguidelines-pro-bounds-constant-array-index)
        return readFunc(_data[_foregroundDataIndex.load()]); // NOLINT(cppcoreguidelines-pro-bounds-constant-array-index)
    }

    // Throwing from write would result in invalid state
    template <typename F>
    auto write(F&& writeFunc) {
        if(_inDestruction.load()) {
            throw std::logic_error("Issued LeftRight::read() after the destructor started running");
        }

        std::unique_lock<std::mutex> lock(_writeMutex);
        return _write(writeFunc);
    }

private:
    template <class F>
    auto _write(const F& writeFunc) {
        /*
         * Assume, A is in background and B in foreground. In simplified terms, we want to do the following:
         * 1. Write to A (old background)
         * 2. Switch A/B
         * 3. Write to B (new background)
         *
         * More detailed algorithm (explanations on why this is important are below in code):
         * 1. Write to A
         * 2. Switch A/B data pointers
         * 3. Wait until A counter is zero
         * 4. Switch A/B counters
         * 5. Wait until B counter is zero
         * 6. Write to B
         */

        auto localDataIndex = _foregroundDataIndex.load();

        // 1. Write to A
        _callWriteFuncOnBackgroundInstance(writeFunc, localDataIndex);

        // 2. Switch A/B data pointers
        localDataIndex = localDataIndex ^ 1;
        _foregroundDataIndex = localDataIndex;

        /*
         * 3. Wait until A counter is zero
         *
         * In the previous write run, A was foreground and B was background.
         * There was a time after switching _foregroundDataIndex (B to foreground) and before switching _foregroundCounterIndex,
         * in which new readers could have read B but incremented A's counter.
         *
         * In this current run, we just switched _foregroundDataIndex (A back to foreground), but before writing to
         * the new background B, we have to make sure A's counter was zero briefly, so all these old readers are gone.
         */
        auto localCounterIndex = _foregroundCounterIndex.load();
        _waitForBackgroundCounterToBeZero(localCounterIndex);

        /*
         *4. Switch A/B counters
         *
         * Now that we know all readers on B are really gone, we can switch the counters and have new readers
         * increment A's counter again, which is the correct counter since they're reading A.
         */
        localCounterIndex = localCounterIndex ^ 1;
        _foregroundCounterIndex = localCounterIndex;

        /*
         * 5. Wait until B counter is zero
         *
         * This waits for all the readers on B that came in while both data and counter for B was in foreground,
         * i.e. normal readers that happened outside of that brief gap between switching data and counter.
         */
        _waitForBackgroundCounterToBeZero(localCounterIndex);

        // 6. Write to B
        return _callWriteFuncOnBackgroundInstance(writeFunc, localDataIndex);
    }

    template<class F>
    auto _callWriteFuncOnBackgroundInstance(const F& writeFunc, uint8_t localDataIndex) {
        try {
            return writeFunc(_data[localDataIndex ^ 1]); // NOLINT(cppcoreguidelines-pro-bounds-constant-array-index)
        } catch (...) {
            // recover invariant by copying from the foreground instance
            _data[localDataIndex ^ 1] = _data[localDataIndex]; // NOLINT(cppcoreguidelines-pro-bounds-constant-array-index)
            // rethrow
            throw;
        }
    }

    void _waitForBackgroundCounterToBeZero(uint8_t counterIndex) {
        while (_counters[counterIndex ^ 1].load() != 0) { // NOLINT(cppcoreguidelines-pro-bounds-constant-array-index)
            std::this_thread::yield();
        }
    }

    std::mutex _writeMutex;
    std::atomic<uint8_t> _foregroundCounterIndex;
    std::atomic<uint8_t> _foregroundDataIndex;
    mutable std::array<std::atomic<int32_t>, 2> _counters;
    std::array<T, 2> _data;
    std::atomic<bool> _inDestruction;
};

}

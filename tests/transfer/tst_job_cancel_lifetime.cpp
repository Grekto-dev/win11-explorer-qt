// SPDX-License-Identifier: GPL-2.0-or-later
// Deterministic lifetime regression for Fm::Job::requestCancellation().
//
// A QThreadPool worker may finish and be auto-deleted as soon as the
// cancellation is published to its pause gate. Publication must therefore not
// leave the cancelling thread touching the Job (or its gate) afterwards. The
// test interposes pthread_cond_broadcast, which is the last operation of the
// gate publication: after the real broadcast, only on the armed thread, it
// waits for the worker to finish and the Job to be destroyed. Under a
// poisoned allocator (MALLOC_PERTURB_) any later access to freed Job memory
// then crashes (RED) instead of passing by luck; a correct implementation
// retains what it needs before publishing and completes (GREEN).
#include <QCoreApplication>
#include <QThreadPool>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <dlfcn.h>
#include <pthread.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <cstring>
#include <thread>
#include <glib.h>
#include "core/job.h"

namespace {
thread_local bool armedThread = false;
std::atomic<bool> jobDestroyed{false};
std::atomic<bool> destroyedInsidePublication{false};
std::atomic<bool> hookReached{false};

class ProbeJob final : public Fm::Job {
public:
    std::atomic<bool> inCheckpoint{false};
    ~ProbeJob() override { jobDestroyed = true; }
    bool supportsCooperativePause() const override { return true; }
protected:
    void exec() override {
        inCheckpoint = true;
        while(waitWhilePaused())
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
};
}

extern "C" int pthread_cond_broadcast(pthread_cond_t* condition) {
    using Function = int (*)(pthread_cond_t*);
    static Function real = reinterpret_cast<Function>(dlsym(RTLD_NEXT, "pthread_cond_broadcast"));
    const int result = real(condition);
    if(armedThread) {
        armedThread = false;  // Only the first publication of this cancellation.
        hookReached = true;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
        while(!jobDestroyed && std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        if(jobDestroyed) {
            destroyedInsidePublication = true;
            // Let any allocator reuse/poisoning settle before the caller continues.
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    }
    return result;
}

int main(int argc, char** argv) {
    // The regression must be able to go RED by crashing; never leave core dumps.
    const rlimit noCore{0, 0};
    setrlimit(RLIMIT_CORE, &noCore);
    // A piped core_pattern (systemd-coredump) ignores RLIMIT_CORE=0; non-dumpable does not.
    prctl(PR_SET_DUMPABLE, 0);
    // glibc skips poisoning of tcache-recycled chunks. Without both settings a
    // late access to the freed Job could pass by luck, making the test vacuous.
    const char* perturb = std::getenv("MALLOC_PERTURB_");
    const char* tunables = std::getenv("GLIBC_TUNABLES");
    if(!perturb || !*perturb || !tunables || !std::strstr(tunables, "glibc.malloc.tcache_count=0")) {
        std::fprintf(stderr, "FAIL: run with MALLOC_PERTURB_ and GLIBC_TUNABLES=glibc.malloc.tcache_count=0 "
                             "(otherwise freed-memory access is not detectable)\n");
        return 2;
    }
    g_log_set_always_fatal(static_cast<GLogLevelFlags>(G_LOG_LEVEL_CRITICAL | G_LOG_LEVEL_WARNING | G_LOG_FLAG_FATAL));
    QCoreApplication app(argc, argv);
    auto* job = new ProbeJob;  // QThreadPool takes ownership (autoDelete).
    QThreadPool::globalInstance()->start(job);
    const auto ready = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while(!job->inCheckpoint && std::chrono::steady_clock::now() < ready)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    if(!job->inCheckpoint) {
        std::fprintf(stderr, "FAIL: worker never reached its checkpoint\n");
        return 2;
    }
    if(!job->pause()) {
        std::fprintf(stderr, "FAIL: pause request rejected\n");
        return 2;
    }
    const auto acknowledged = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while(!job->isPauseAcknowledged() && std::chrono::steady_clock::now() < acknowledged)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    if(!job->isPauseAcknowledged()) {
        std::fprintf(stderr, "FAIL: worker never acknowledged the pause\n");
        return 2;
    }

    bool accepted = false;
    std::thread canceller([&] {
        armedThread = true;
        accepted = job->requestCancellation();  // 'job' is alive when this call starts.
    });
    canceller.join();
    QThreadPool::globalInstance()->waitForDone(5000);
    if(!hookReached) {
        std::fprintf(stderr, "FAIL: cancellation publication hook was not reached\n");
        return 2;
    }
    if(!accepted) {
        std::fprintf(stderr, "FAIL: cancellation was not accepted\n");
        return 2;
    }
    // The Job MUST have been destroyed inside the publication window: otherwise
    // this run did not exercise the dangerous interleaving and proves nothing.
    // The property under test is that the call then completed without touching
    // the freed Job. A late access crashes (poisoned memory) or aborts on a
    // GLib critical, never reaching this point.
    if(!destroyedInsidePublication) {
        std::fprintf(stderr, "FAIL: the Job outlived the publication window; interleaving not exercised\n");
        return 1;
    }
    std::puts("PASS: cancellation completed after the Job was destroyed mid-publication, with no access to freed state");
    return 0;
}

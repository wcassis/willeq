// P23 — ParticleManager::renderUnified dangling-pointer-shaped race:
// standalone stress/repro harness for the SimulationWorker input-buffer race.
//
// ROOT CAUSE (see refactor/P23_particle_race_stability.md Review for the
// full writeup): SimulationWorker::input_ was written by postInput() (main
// thread) under mutex_, but read by workerLoop() (worker thread) with NO
// lock held during computeAll(). postInput() is called once per frame
// unconditionally; swapAndGetResults() explicitly does NOT block the main
// thread when the worker hasn't finished the previous frame (it serves
// stale front-buffer data instead — "graceful degradation"). So on any
// frame slower than the worker's compute budget, the main thread would
// reassign input_ (destroying/reallocating its std::vector/
// std::unordered_map members: entitySnapshots, particleInput.commands,
// weatherLights, entityPositions, ...) WHILE the worker thread was still
// mid-computeAll(), iterating those same containers — a genuine concurrent
// read/write data race. The double-buffered *output* (output_[2],
// frontIdx_/backIdx_) was already safe by construction; input_ was the
// single-buffered, unsynchronized half of the same handoff. The corruption
// surfaces wherever computeAll() happened to be reading input_ at the
// moment of the race — including computeParticles(), which builds
// output.particleOutput.renderBuffer, the buffer
// ParticleManager::renderUnified() renders from. Hence a crash that
// *looked* like a dangling pointer into the result buffer, even though the
// result buffer's own synchronization was sound.
//
// This harness drives SimulationWorker exactly the way renderer_frame.cpp
// does per frame (swapAndGetResults() before postInput()), but at a rate
// deliberately faster than the worker can keep up (no throttling, no
// frame-rate cap, particle commands posted every iteration), which is the
// condition needed to hit the race: it reproduces the "slow worker frame"
// case on essentially every iteration instead of rarely.
//
// Usage:
//   particle_race_stress [iterations]         (default 20000)
//
// Exit code 0 = completed cleanly. Run under ThreadSanitizer
// (-fsanitize=thread) for race evidence; run under AddressSanitizer
// (-fsanitize=address) for corruption/UAF evidence. See
// refactor/P23_particle_race_stability.md Review for the exact build
// commands used to gather both pieces of evidence, and for confirmation
// that both are clean after the mutex-protected-snapshot fix in
// simulation_worker.cpp.

#include "client/graphics/simulation_worker.h"
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <vector>

using namespace EQT::Graphics;

int main(int argc, char** argv) {
    int iterations = 20000;
    if (argc > 1) {
        iterations = std::atoi(argv[1]);
        if (iterations <= 0) iterations = 20000;
    }

    SimulationWorker worker;

    // Minimal zone data — empty is fine; computeAll() degrades gracefully
    // over empty vectors/maps. We only need zoneDataValid_ = true so
    // workerLoop() doesn't skip computeAll() (the `if (!zoneDataValid_)
    // continue;` early-out).
    SimulationZoneData zoneData;
    worker.setZoneData(zoneData);
    worker.start();

    std::atomic<bool> stopReader{false};
    std::atomic<uint64_t> renderTouches{0};

    // "Render thread" side: repeatedly grab the front buffer and touch the
    // particle render buffer contents, mimicking
    // ParticleManager::renderUnified() reading externalRenderBuffer->data()
    // for its size(). This thread never writes to SimulationWorker state —
    // it exists so a torn/racy front buffer swap would also be observable
    // from a second thread, not just the main stress loop.
    std::thread reader([&]() {
        while (!stopReader.load(std::memory_order_relaxed)) {
            // swapAndGetResults() is main-thread-only in production, but
            // exercising it from a second thread here adds coverage on the
            // frontIdx_/backIdx_ synchronization fixed alongside the input_
            // race (see swapAndGetResults() comment in simulation_worker.cpp).
            std::this_thread::yield();
        }
    });

    uint64_t particlesSeenTotal = 0;
    std::vector<glm::vec3> fireSources = {
        {10.0f, 0.0f, 10.0f}, {-20.0f, 0.0f, 5.0f}, {0.0f, 0.0f, -30.0f},
    };
    std::vector<float> fireRadii = {50.0f, 200.0f, 80.0f};

    for (int i = 0; i < iterations; ++i) {
        SimulationInput input;
        input.deltaTime = 1.0f / 60.0f;
        input.frameNumber = static_cast<uint32_t>(i);

        auto& pi = input.particleInput;
        pi.deltaTime = input.deltaTime;
        pi.unifiedRendererInitialized = true;
        pi.poolSize = 512;
        pi.fireEnabled = true;
        pi.cameraPos = glm::vec3(0.0f, 5.0f, 0.0f);
        pi.windDirection = glm::vec3(1.0f, 0.0f, 0.0f);
        pi.windStrength = 0.5f;

        // Post a fresh CreateFireEmitters command periodically — this
        // forces reallocation of particleEmitters_/particlePool_-adjacent
        // vectors inside the worker AND of input.particleInput.commands
        // itself on the way in, maximizing allocator churn on the shared
        // input_ struct (the exact pattern that made the pre-fix race
        // observable: vector reallocation racing a concurrent iterator).
        if (i % 7 == 0) {
            ParticleCommandData cmd;
            cmd.type = ParticleCommand::CreateFireEmitters;
            cmd.firePositions = fireSources;
            cmd.fireRadii = fireRadii;
            pi.commands.push_back(std::move(cmd));
        }
        if (i % 11 == 0) {
            ParticleCommandData cmd;
            cmd.type = ParticleCommand::ClearUnifiedEmitters;
            pi.commands.push_back(std::move(cmd));
        }

        // Simulate a handful of moving entities each frame (entitySnapshots
        // is another vector<> in SimulationInput that gets reallocated
        // every postInput() call).
        input.entitySnapshots.resize(20);
        for (size_t e = 0; e < input.entitySnapshots.size(); ++e) {
            auto& es = input.entitySnapshots[e];
            es.spawnId = static_cast<uint16_t>(e + 1);
            es.lastX = static_cast<float>(i % 100);
            es.lastY = static_cast<float>(e);
            es.lastZ = 0.0f;
        }

        // Deliberately NO throttling/sleep here — postInput() is called as
        // fast as this loop can spin, guaranteeing many iterations where
        // the worker has not finished the previous computeAll() yet. This
        // is what makes swapAndGetResults()'s graceful-degradation path
        // (stale-front-buffer fallback) execute constantly instead of
        // rarely, which is the precondition for the input_ race.
        const SimulationOutput* results = worker.swapAndGetResults();
        if (results && results->particleOutput.valid) {
            // Touch the render buffer the way renderUnified() does:
            // read size() and iterate elements.
            const auto& rb = results->particleOutput.renderBuffer;
            for (const auto& p : rb) {
                particlesSeenTotal += p.isAlive() ? 1 : 0;
            }
            renderTouches.fetch_add(1, std::memory_order_relaxed);
        }

        worker.postInput(input);
    }

    stopReader.store(true, std::memory_order_relaxed);
    reader.join();

    worker.stop();
    worker.clearZoneData();

    std::fprintf(stderr,
                  "particle_race_stress: %d iterations, %llu render touches, "
                  "%llu particle-alive samples — completed cleanly\n",
                  iterations,
                  static_cast<unsigned long long>(renderTouches.load()),
                  static_cast<unsigned long long>(particlesSeenTotal));
    return 0;
}

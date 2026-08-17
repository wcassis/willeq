// T130: regression test for the TinySoundFont out-of-bounds read fixed in
// third_party/TinySoundFont/tsf.h (see the "WILLEQ T130" markers there).
//
// tsf_voice_render()'s linear-interpolation step reads input[nextPos] where,
// for a non-looping voice, nextPos = pos + 1. On the voice's FINAL sample
// (pos == region->end - 1) that reads one float past the sample pool.
//
// This test constructs that boundary case directly: a single non-looping
// tsf_voice/tsf_region pointed at a sample buffer whose end sits exactly on
// an mmap'd page boundary, with the following page mprotect(PROT_NONE)'d.
// That makes an out-of-bounds read a deterministic SIGSEGV rather than a
// silent heap-slack read an ASAN-less build would never see -- so this test
// catches the regression on a plain (non-ASAN) ctest run too. The render
// call happens in a forked child so a still-broken build fails this test
// with a clean assertion instead of taking the whole test binary down.
//
// tsf_voice_render() is `static` (internal to tsf.h's implementation), so
// this TU includes tsf.h with TSF_IMPLEMENTATION directly, the same pattern
// midi_player.cpp uses, giving direct access to it and the internal structs.

#include <gtest/gtest.h>

#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cstring>

#define TSF_IMPLEMENTATION
#include "TinySoundFont/tsf.h"

namespace {

// Runs `fn` in a forked child and reports whether the child crashed
// (SIGSEGV/SIGBUS, the OOB-read signature) or exited cleanly.
template <typename Fn>
bool ChildSurvives(Fn&& fn) {
    pid_t pid = fork();
    if (pid == 0) {
        // Child: run the code under test, then exit cleanly if it returns.
        fn();
        _exit(0);
    }
    int status = 0;
    waitpid(pid, &status, 0);
    if (WIFSIGNALED(status)) {
        return false;
    }
    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

}  // namespace

// The boundary case: a non-looping voice rendered to its final sample must
// not read past the end of its sample pool.
TEST(TsfOobReadTest, NonLoopingVoiceAtFinalSampleDoesNotReadPastPool) {
    long pageSize = sysconf(_SC_PAGESIZE);
    ASSERT_GT(pageSize, 0);

    // Two pages: the first holds the sample pool (filled entirely, so the
    // pool's logical end sits exactly on the page boundary), the second is
    // made inaccessible so a read at pool_end (the OOB index) faults.
    void* region = mmap(nullptr, static_cast<size_t>(pageSize) * 2,
                         PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS,
                         -1, 0);
    ASSERT_NE(region, MAP_FAILED);
    float* samplePool = static_cast<float*>(region);
    const unsigned int poolCount =
        static_cast<unsigned int>(pageSize) / sizeof(float);
    for (unsigned int i = 0; i < poolCount; ++i) {
        samplePool[i] = 0.5f;
    }
    ASSERT_EQ(mprotect(static_cast<char*>(region) + pageSize, pageSize,
                        PROT_NONE),
              0);

    bool survived = ChildSurvives([&]() {
        tsf synth;
        std::memset(&synth, 0, sizeof(synth));
        synth.fontSamples = samplePool;
        synth.outputmode = TSF_MONO;
        synth.outSampleRate = 44100.0f;
        synth.globalGainDB = 0.0f;

        tsf_region region_def;
        std::memset(&region_def, 0, sizeof(region_def));
        region_def.end = poolCount;      // one-past-last-valid-index
        region_def.loop_start = 0;
        region_def.loop_end = 0;         // non-looping: loopEnd <= loopStart
        region_def.sample_rate = 44100;
        region_def.pitch_keycenter = 60;
        region_def.attenuation = 0.0f;
        region_def.pan = 0.0f;

        tsf_voice voice;
        std::memset(&voice, 0, sizeof(voice));
        voice.region = &region_def;
        voice.loopStart = region_def.loop_start;
        voice.loopEnd = region_def.loop_end;  // loopEnd <= loopStart -> isLooping == false
        // Position the voice on its FINAL sample -- one render call at
        // pitchRatio 1.0 will step it from end-1.5 to end-0.5, landing pos
        // at poolCount-1 and computing nextPos = poolCount (the OOB index)
        // under the unfixed code.
        voice.sourceSamplePosition =
            static_cast<double>(poolCount) - 1.5;
        voice.pitchInputTimecents = 0.0;
        voice.pitchOutputFactor = 1.0;
        voice.noteGainDB = 0.0f;
        voice.panFactorLeft = 1.0f;
        voice.panFactorRight = 1.0f;
        voice.ampenv.level = 1.0f;
        voice.ampenv.segment = TSF_SEGMENT_SUSTAIN;
        voice.ampenv.isAmpEnv = 1;
        voice.modenv.level = 0.0f;
        voice.modenv.segment = TSF_SEGMENT_SUSTAIN;

        float output[8] = {0};
        tsf_voice_render(&synth, &voice, output, 4);
    });

    munmap(region, static_cast<size_t>(pageSize) * 2);

    EXPECT_TRUE(survived)
        << "non-looping voice render crashed on an out-of-bounds read at "
           "its final sample -- the tsf.h clamp (WILLEQ T130) is missing "
           "or broken";
}

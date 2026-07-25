// P09 A-gate micro-benchmark. See zone_bench_merge_probe.h for the four
// primitives measured. Self-contained raw-GLES2 harness (own tiny shader,
// own VBO/IBO objects) sharing ZoneBenchData's already-current EGL context.

#include "zone_bench_merge_probe.h"
#include "zone_bench_loader.h"

#include <chrono>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <vector>

#ifdef EQT_HAS_GLES2
#include <GLES2/gl2.h>

namespace ZoneBench {

namespace {

// One "region" worth of synthetic geometry: a small quad, matched in order
// of magnitude to the real per-draw poly counts from P08's B3 histogram
// (mean 3.0-6.4 polys/draw at the 6 pinned poses) -- 2 triangles/region.
constexpr int kVertsPerRegion = 4;
constexpr int kIndicesPerRegion = 6; // 2 tris

struct Vertex { float x, y, z; };

const char* kVS =
    "attribute vec3 aPosition;\n"
    "uniform vec2 uOffset;\n"
    "void main() {\n"
    "    gl_Position = vec4(aPosition.xy * 0.01 + uOffset, aPosition.z, 1.0);\n"
    "}\n";

const char* kFS =
    "precision mediump float;\n"
    "void main() { gl_FragColor = vec4(0.6, 0.6, 0.6, 1.0); }\n";

GLuint compileShader(GLenum type, const char* src) {
    GLuint sh = glCreateShader(type);
    glShaderSource(sh, 1, &src, nullptr);
    glCompileShader(sh);
    GLint ok = 0;
    glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetShaderInfoLog(sh, sizeof(log), nullptr, log);
        fprintf(stderr, "[merge-probe] shader compile error: %s\n", log);
        glDeleteShader(sh);
        return 0;
    }
    return sh;
}

GLuint linkProgram(GLuint vs, GLuint fs) {
    GLuint prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glBindAttribLocation(prog, 0, "aPosition");
    glLinkProgram(prog);
    GLint ok = 0;
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetProgramInfoLog(prog, sizeof(log), nullptr, log);
        fprintf(stderr, "[merge-probe] program link error: %s\n", log);
        glDeleteProgram(prog);
        return 0;
    }
    return prog;
}

// Builds kMaxRegions worth of quads, laid out on a grid so uOffset keeps
// them all inside the [-1,1] clip-space viewport (cosmetic only -- this
// tool never eyeballs the output, it only times submission).
void buildRegionGeometry(int maxRegions,
                          std::vector<Vertex>& outVerts,
                          std::vector<uint16_t>& outIndices,
                          std::vector<int>& outIndexOffsetPerRegion) {
    outVerts.reserve(static_cast<size_t>(maxRegions) * kVertsPerRegion);
    outIndices.reserve(static_cast<size_t>(maxRegions) * kIndicesPerRegion);
    outIndexOffsetPerRegion.resize(maxRegions);
    for (int r = 0; r < maxRegions; ++r) {
        outIndexOffsetPerRegion[r] = static_cast<int>(outIndices.size());
        uint16_t base = static_cast<uint16_t>(outVerts.size());
        // Unit quad in a small local space; per-instance placement happens
        // via uOffset (separate-vbo path) or is baked as a static grid
        // (shared paths -- offset folded into vertex position instead, see
        // below) so all four primitives draw geometry of identical size.
        outVerts.push_back({0.0f, 0.0f, 0.0f});
        outVerts.push_back({1.0f, 0.0f, 0.0f});
        outVerts.push_back({1.0f, 1.0f, 0.0f});
        outVerts.push_back({0.0f, 1.0f, 0.0f});
        outIndices.push_back(base + 0);
        outIndices.push_back(base + 1);
        outIndices.push_back(base + 2);
        outIndices.push_back(base + 0);
        outIndices.push_back(base + 2);
        outIndices.push_back(base + 3);
    }
}

struct SeparateBuffers {
    std::vector<GLuint> vbo;
    std::vector<GLuint> ibo;
};

SeparateBuffers buildSeparateBuffers(int maxRegions,
                                      const std::vector<Vertex>& verts,
                                      const std::vector<uint16_t>& indices) {
    SeparateBuffers sb;
    sb.vbo.resize(maxRegions);
    sb.ibo.resize(maxRegions);
    glGenBuffers(maxRegions, sb.vbo.data());
    glGenBuffers(maxRegions, sb.ibo.data());
    for (int r = 0; r < maxRegions; ++r) {
        const Vertex* v = &verts[static_cast<size_t>(r) * kVertsPerRegion];
        const uint16_t* baseIdx = &indices[static_cast<size_t>(r) * kIndicesPerRegion];
        // Re-base indices to 0..3 (this region's own small buffer).
        uint16_t local[kIndicesPerRegion];
        uint16_t vbase = static_cast<uint16_t>(r * kVertsPerRegion);
        for (int i = 0; i < kIndicesPerRegion; ++i) local[i] = baseIdx[i] - vbase;

        glBindBuffer(GL_ARRAY_BUFFER, sb.vbo[r]);
        glBufferData(GL_ARRAY_BUFFER, sizeof(Vertex) * kVertsPerRegion, v, GL_STATIC_DRAW);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, sb.ibo[r]);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(local), local, GL_STATIC_DRAW);
    }
    return sb;
}

double nowMs(std::chrono::steady_clock::time_point a, std::chrono::steady_clock::time_point b) {
    return std::chrono::duration<double, std::milli>(b - a).count();
}

struct Result {
    const char* primitive;
    int drawsOrN;
    double cpuMs;
    double gpuMs;
    double totalMs;
    double usPerRegion;
};

void printRow(const Result& r) {
    printf("  %-18s %8d  %10.3f  %10.3f  %10.3f  %10.2f\n",
           r.primitive, r.drawsOrN, r.cpuMs, r.gpuMs, r.totalMs, r.usPerRegion);
}

void csvRow(FILE* f, const Result& r) {
    if (!f) return;
    fprintf(f, "%s,%d,%.4f,%.4f,%.4f,%.2f\n",
            r.primitive, r.drawsOrN, r.cpuMs, r.gpuMs, r.totalMs, r.usPerRegion);
}

} // namespace

void runMergeProbeTest(ZoneBenchData& /*data*/, int numFrames, const char* csvPath) {
    printf("\n=== P09 A-gate: cross-region merge primitive probe ===\n");

    const int drawCounts[] = {1, 10, 50, 100, 200, 500, 1000};
    const int numSweeps = static_cast<int>(sizeof(drawCounts) / sizeof(drawCounts[0]));
    const int maxRegions = drawCounts[numSweeps - 1];

    std::vector<Vertex> verts;
    std::vector<uint16_t> indices;
    std::vector<int> indexOffsetPerRegion;
    buildRegionGeometry(maxRegions, verts, indices, indexOffsetPerRegion);

    GLuint vs = compileShader(GL_VERTEX_SHADER, kVS);
    GLuint fs = compileShader(GL_FRAGMENT_SHADER, kFS);
    if (!vs || !fs) {
        fprintf(stderr, "[merge-probe] shader setup failed, aborting probe\n");
        return;
    }
    GLuint prog = linkProgram(vs, fs);
    if (!prog) {
        fprintf(stderr, "[merge-probe] program link failed, aborting probe\n");
        return;
    }
    glUseProgram(prog);
    GLint offsetLoc = glGetUniformLocation(prog, "uOffset");
    glEnableVertexAttribArray(0);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);

    // --- Primitive 1: separate-vbo (N independent VBO+IBO, full rebind) ---
    SeparateBuffers sep = buildSeparateBuffers(maxRegions, verts, indices);

    // --- Primitive 2: shared-coalesced (one static VBO+IBO, contiguous) ---
    GLuint sharedVbo, sharedIbo;
    glGenBuffers(1, &sharedVbo);
    glGenBuffers(1, &sharedIbo);
    glBindBuffer(GL_ARRAY_BUFFER, sharedVbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(Vertex) * verts.size(), verts.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, sharedIbo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(uint16_t) * indices.size(), indices.data(), GL_STATIC_DRAW);

    // --- Primitive 3/4 scratch: per-"frame" CPU gather target ---
    std::vector<uint16_t> gatherScratch(static_cast<size_t>(maxRegions) * kIndicesPerRegion);
    GLuint streamIbo;
    glGenBuffers(1, &streamIbo);
    // Pre-size the dynamic buffer once (glBufferData at max size), then
    // glBufferSubData per measured iteration -- this is the realistic
    // streamed-IBO usage pattern (orphan-once, sub-update per frame), not
    // a fresh glBufferData(..., GL_DYNAMIC_DRAW) allocation every frame.
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, streamIbo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(uint16_t) * gatherScratch.size(), nullptr, GL_DYNAMIC_DRAW);

    FILE* csv = nullptr;
    if (csvPath && csvPath[0]) {
        csv = fopen(csvPath, "w");
        if (csv) fprintf(csv, "primitive,n,cpu_ms,gpu_ms,total_ms,us_per_region\n");
    }

    std::vector<Result> allResults;

    auto measure = [&](const char* name, int N, auto&& submitFn) {
        // Warmup
        for (int w = 0; w < 5; ++w) {
            glClear(GL_COLOR_BUFFER_BIT);
            submitFn(N);
            glFinish();
        }
        double cpuSum = 0, gpuSum = 0, totalSum = 0;
        for (int f = 0; f < numFrames; ++f) {
            auto t0 = std::chrono::steady_clock::now();
            glClear(GL_COLOR_BUFFER_BIT);
            auto drawStart = std::chrono::steady_clock::now();
            submitFn(N);
            auto drawEnd = std::chrono::steady_clock::now();
            glFinish();
            auto gpuEnd = std::chrono::steady_clock::now();
            cpuSum += nowMs(drawStart, drawEnd);
            gpuSum += nowMs(drawEnd, gpuEnd);
            totalSum += nowMs(t0, gpuEnd);
        }
        Result r{name, N, cpuSum / numFrames, gpuSum / numFrames, totalSum / numFrames,
                  (cpuSum / numFrames) * 1000.0 / N};
        printRow(r);
        csvRow(csv, r);
        allResults.push_back(r);
    };

    printf("  %-18s %8s  %10s  %10s  %10s  %10s\n",
           "primitive", "N", "CPU ms", "GPU ms", "Total ms", "us/region");

    for (int s = 0; s < numSweeps; ++s) {
        int N = drawCounts[s];

        // 1. separate-vbo: N independent bind+attrib+draw sequences.
        measure("separate-vbo", N, [&](int n) {
            for (int r = 0; r < n; ++r) {
                glBindBuffer(GL_ARRAY_BUFFER, sep.vbo[r]);
                glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)0);
                glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, sep.ibo[r]);
                float ox = -0.9f + 0.02f * (r % 90);
                float oy = -0.9f + 0.02f * (r / 90);
                glUniform2f(offsetLoc, ox, oy);
                glDrawElements(GL_TRIANGLES, kIndicesPerRegion, GL_UNSIGNED_SHORT, (void*)0);
            }
        });

        // 2a. shared-coalesced (N draws, ONE bind of the shared buffer,
        //     N glDrawElements at different byte offsets -- no rebind).
        measure("shared-coalesced", N, [&](int n) {
            glBindBuffer(GL_ARRAY_BUFFER, sharedVbo);
            glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)0);
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, sharedIbo);
            glUniform2f(offsetLoc, 0.0f, 0.0f);
            for (int r = 0; r < n; ++r) {
                size_t byteOffset = static_cast<size_t>(indexOffsetPerRegion[r]) * sizeof(uint16_t);
                glDrawElements(GL_TRIANGLES, kIndicesPerRegion, GL_UNSIGNED_SHORT,
                                (void*)byteOffset);
            }
        });

        // 2b. shared-coalesced-1draw: the ideal case for candidate #2 --
        //     the N regions ARE one contiguous index range (as they would
        //     be after a load-time same-material sort into the shared IBO),
        //     so the runtime coalescer emits exactly one glDrawElements.
        measure("shared-1draw", N, [&](int n) {
            glBindBuffer(GL_ARRAY_BUFFER, sharedVbo);
            glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)0);
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, sharedIbo);
            glUniform2f(offsetLoc, 0.0f, 0.0f);
            glDrawElements(GL_TRIANGLES, n * kIndicesPerRegion, GL_UNSIGNED_SHORT, (void*)0);
        });

        // 3. streamed-ibo: CPU-gather N regions' indices (memcpy, rebased
        //    to the shared VBO's vertex numbering -- already 0-based global
        //    since buildRegionGeometry uses global vertex indices), upload
        //    via glBufferSubData into a pre-sized dynamic buffer, one draw.
        measure("streamed-ibo", N, [&](int n) {
            size_t count = static_cast<size_t>(n) * kIndicesPerRegion;
            std::memcpy(gatherScratch.data(), indices.data(), count * sizeof(uint16_t));
            glBindBuffer(GL_ARRAY_BUFFER, sharedVbo);
            glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)0);
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, streamIbo);
            glBufferSubData(GL_ELEMENT_ARRAY_BUFFER, 0, count * sizeof(uint16_t), gatherScratch.data());
            glUniform2f(offsetLoc, 0.0f, 0.0f);
            glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(count), GL_UNSIGNED_SHORT, (void*)0);
        });

        // 4. client-index: same CPU gather, but no GL buffer at all --
        //    glDrawElements against a client-memory pointer (ELEMENT_ARRAY_
        //    BUFFER unbound).
        measure("client-index", N, [&](int n) {
            size_t count = static_cast<size_t>(n) * kIndicesPerRegion;
            std::memcpy(gatherScratch.data(), indices.data(), count * sizeof(uint16_t));
            glBindBuffer(GL_ARRAY_BUFFER, sharedVbo);
            glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)0);
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
            glUniform2f(offsetLoc, 0.0f, 0.0f);
            glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(count), GL_UNSIGNED_SHORT,
                            gatherScratch.data());
        });

        printf("\n");
    }

    if (csv) fclose(csv);

    // Cleanup GL objects (best-effort; process exits shortly after).
    glDeleteBuffers(static_cast<GLsizei>(sep.vbo.size()), sep.vbo.data());
    glDeleteBuffers(static_cast<GLsizei>(sep.ibo.size()), sep.ibo.data());
    glDeleteBuffers(1, &sharedVbo);
    glDeleteBuffers(1, &sharedIbo);
    glDeleteBuffers(1, &streamIbo);
    glDeleteProgram(prog);
    glDeleteShader(vs);
    glDeleteShader(fs);
}

} // namespace ZoneBench

#else // !EQT_HAS_GLES2

namespace ZoneBench {
void runMergeProbeTest(ZoneBenchData&, int, const char*) {
    fprintf(stderr, "[merge-probe] built without EQT_HAS_GLES2, probe unavailable\n");
}
} // namespace ZoneBench

#endif

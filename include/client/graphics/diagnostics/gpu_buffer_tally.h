#pragma once

// B955 (GitLab #273): the renderer side of the gpu_buffers ledger row
// (include/client/core/gpu_buffer_census.h). The driver's own counters --
// gpuTextureBytes (every live COGLES2Texture's GPU bytes) and vboEboBytes
// (every static hardware buffer) -- iterate the driver's containers, so they
// are read HERE, on the thread that owns the GL context, and published to the
// ledger, whose lifecycle points (some on the game thread) read the last
// published value. The GL_RENDERER string is read once, on the first call,
// to tell a software rasteriser (llvmpipe: buffers inside gpu_driver) from a
// real GPU (Lima: buffers in GEM, outside RssAnon).

namespace irr { namespace video { class IVideoDriver; } }

namespace EQT {
namespace Graphics {

// Publish now. Call only on the thread that currently owns the GL context.
void publishGpuBufferTally(irr::video::IVideoDriver* driver);

// The render loop's form: publishes at most once per `periodMs`, so a ledger
// point on another thread reads a tally at most that old. The time check is
// the whole per-frame cost.
void publishGpuBufferTallyThrottled(irr::video::IVideoDriver* driver, int periodMs = 1000);

} // namespace Graphics
} // namespace EQT

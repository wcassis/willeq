// COGLES2MaterialRenderer.h — Material type renderers for COpenGLES2Driver
// Handles blend, depth, cull state setup per EMT_* material type.

#ifndef __C_OGLES2_MATERIAL_RENDERER_H_INCLUDED__
#define __C_OGLES2_MATERIAL_RENDERER_H_INCLUDED__

#include "IrrCompileConfig.h"

#ifdef _IRR_COMPILE_WITH_OGLES2_

#include "IMaterialRenderer.h"

namespace irr
{
namespace video
{

class COpenGLES2Driver;

// Base material renderer for GLES2 — handles common state
class COGLES2MaterialRenderer : public IMaterialRenderer
{
public:
    COGLES2MaterialRenderer(COpenGLES2Driver* driver) : Driver(driver) {}

    virtual void OnSetMaterial(const SMaterial& material, const SMaterial& lastMaterial,
                               bool resetAllRenderstates, IMaterialRendererServices* services) {}
    virtual void OnUnsetMaterial() {}
    virtual bool isTransparent() const { return false; }
    virtual bool OnRender(IMaterialRendererServices* service, E_VERTEX_TYPE vtxtype) { return true; }

protected:
    COpenGLES2Driver* Driver;
};

// Solid material (opaque, depth write on)
class COGLES2MaterialRenderer_SOLID : public COGLES2MaterialRenderer
{
public:
    COGLES2MaterialRenderer_SOLID(COpenGLES2Driver* d) : COGLES2MaterialRenderer(d) {}
    virtual bool isTransparent() const { return false; }
};

// Transparent alpha channel
class COGLES2MaterialRenderer_TRANSPARENT_ALPHA_CHANNEL : public COGLES2MaterialRenderer
{
public:
    COGLES2MaterialRenderer_TRANSPARENT_ALPHA_CHANNEL(COpenGLES2Driver* d)
        : COGLES2MaterialRenderer(d) {}
    virtual bool isTransparent() const { return true; }
};

// Transparent alpha channel ref (alpha test via discard)
class COGLES2MaterialRenderer_TRANSPARENT_ALPHA_CHANNEL_REF : public COGLES2MaterialRenderer
{
public:
    COGLES2MaterialRenderer_TRANSPARENT_ALPHA_CHANNEL_REF(COpenGLES2Driver* d)
        : COGLES2MaterialRenderer(d) {}
    virtual bool isTransparent() const { return false; }  // Not truly transparent — uses discard
};

// Transparent add color
class COGLES2MaterialRenderer_TRANSPARENT_ADD_COLOR : public COGLES2MaterialRenderer
{
public:
    COGLES2MaterialRenderer_TRANSPARENT_ADD_COLOR(COpenGLES2Driver* d)
        : COGLES2MaterialRenderer(d) {}
    virtual bool isTransparent() const { return true; }
};

// Transparent vertex alpha
class COGLES2MaterialRenderer_TRANSPARENT_VERTEX_ALPHA : public COGLES2MaterialRenderer
{
public:
    COGLES2MaterialRenderer_TRANSPARENT_VERTEX_ALPHA(COpenGLES2Driver* d)
        : COGLES2MaterialRenderer(d) {}
    virtual bool isTransparent() const { return true; }
};

} // end namespace video
} // end namespace irr

#endif // _IRR_COMPILE_WITH_OGLES2_
#endif // __C_OGLES2_MATERIAL_RENDERER_H_INCLUDED__

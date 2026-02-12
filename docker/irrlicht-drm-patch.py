#!/usr/bin/env python3
"""Patch Irrlicht 1.8.5 for DRM/KMS/GBM/EGL support (no X11).

Modifies:
  - IrrCompileConfig.h: Enable FB device, disable X11
  - COpenGLExtensionHandler.h: Add EGL include path for FB device
  - COpenGLExtensionHandler.h: Add EGL vsync in extGlSwapInterval
  - COpenGLExtensionHandler.cpp: Add EGL extension loader
  - COpenGLDriver.h: Add FB device forward decl, constructor, member
  - COpenGLDriver.cpp: Add FB constructor, endScene, factory function

Usage: python3 irrlicht-drm-patch.py <irrlicht-source-dir>
  e.g. python3 irrlicht-drm-patch.py /tmp/irrlicht-1.8.5
"""
import sys
import os

if len(sys.argv) < 2:
    print("Usage: irrlicht-drm-patch.py <irrlicht-source-dir>")
    sys.exit(1)

base = sys.argv[1]
src = os.path.join(base, 'source', 'Irrlicht')
inc = os.path.join(base, 'include')

def patch(filepath, old, new, count=1):
    s = open(filepath).read()
    if old not in s:
        print(f"ERROR: Patch target not found in {filepath}")
        print(f"  Looking for: {old[:80]}...")
        sys.exit(1)
    s = s.replace(old, new, count)
    open(filepath, 'w').write(s)
    print(f"  Patched: {os.path.basename(filepath)}")

# ============================================================
# Patch 1: IrrCompileConfig.h - Enable FB device, disable X11
# ============================================================
print("Patch 1: IrrCompileConfig.h")

# Enable FB device
config_h = os.path.join(inc, 'IrrCompileConfig.h')
s = open(config_h).read()

# Add FB device define after the X11 device define
old1 = '#define _IRR_COMPILE_WITH_X11_DEVICE_\n#endif'
new1 = '//#define _IRR_COMPILE_WITH_X11_DEVICE_\n#define _IRR_COMPILE_WITH_FB_DEVICE_\n#endif'
assert old1 in s, f"Patch 1a target not found in {config_h}"
s = s.replace(old1, new1, 1)

# Disable _IRR_COMPILE_WITH_X11_ (the X11 utility flag)
old1b = '#define _IRR_COMPILE_WITH_X11_\n#ifdef NO_IRR_COMPILE_WITH_X11_\n#undef _IRR_COMPILE_WITH_X11_\n#endif'
new1b = '//#define _IRR_COMPILE_WITH_X11_\n#ifdef NO_IRR_COMPILE_WITH_X11_\n#undef _IRR_COMPILE_WITH_X11_\n#endif'
assert old1b in s, f"Patch 1b target not found in {config_h}"
s = s.replace(old1b, new1b, 1)

open(config_h, 'w').write(s)
print(f"  Patched: IrrCompileConfig.h (FB device enabled, X11 disabled)")

# ============================================================
# Patch 2: COpenGLExtensionHandler.h - Add EGL include path
# ============================================================
print("Patch 2: COpenGLExtensionHandler.h (includes)")

ext_h = os.path.join(src, 'COpenGLExtensionHandler.h')

# Add FB device block before the #else (X11/GLX) block
old2 = '''#elif defined(_IRR_COMPILE_WITH_SDL_DEVICE_) && !defined(_IRR_COMPILE_WITH_X11_DEVICE_)
	#if defined(_IRR_OPENGL_USE_EXTPOINTER_)
		#define GL_GLEXT_LEGACY 1
		#define GLX_GLXEXT_LEGACY 1
	#else
		#define GL_GLEXT_PROTOTYPES 1
		#define GLX_GLXEXT_PROTOTYPES 1
	#endif
	#define NO_SDL_GLEXT
	#include <SDL/SDL_video.h>
	#include <SDL/SDL_opengl.h>
	#include "glext.h"
#else'''

new2 = '''#elif defined(_IRR_COMPILE_WITH_SDL_DEVICE_) && !defined(_IRR_COMPILE_WITH_X11_DEVICE_)
	#if defined(_IRR_OPENGL_USE_EXTPOINTER_)
		#define GL_GLEXT_LEGACY 1
		#define GLX_GLXEXT_LEGACY 1
	#else
		#define GL_GLEXT_PROTOTYPES 1
		#define GLX_GLXEXT_PROTOTYPES 1
	#endif
	#define NO_SDL_GLEXT
	#include <SDL/SDL_video.h>
	#include <SDL/SDL_opengl.h>
	#include "glext.h"
#elif defined(_IRR_COMPILE_WITH_FB_DEVICE_) && !defined(_IRR_COMPILE_WITH_X11_DEVICE_)
	// DRM/KMS/GBM/EGL path - no X11, no GLX
	#if defined(_IRR_OPENGL_USE_EXTPOINTER_)
		#define GL_GLEXT_LEGACY 1
	#else
		#define GL_GLEXT_PROTOTYPES 1
	#endif
	#include <EGL/egl.h>
	#include <GL/gl.h>
	#if defined(_IRR_OPENGL_USE_EXTPOINTER_)
	#include "glext.h"
	#endif
#else'''

patch(ext_h, old2, new2)

# ============================================================
# Patch 3: COpenGLExtensionHandler.h - Add EGL vsync
# ============================================================
print("Patch 3: COpenGLExtensionHandler.h (extGlSwapInterval)")

old3 = '''#ifdef _IRR_COMPILE_WITH_X11_DEVICE_
	//TODO: Check GLX_EXT_swap_control and GLX_MESA_swap_control
#ifdef GLX_SGI_swap_control'''

new3 = '''#ifdef _IRR_COMPILE_WITH_FB_DEVICE_
	eglSwapInterval(eglGetCurrentDisplay(), interval);
#endif
#ifdef _IRR_COMPILE_WITH_X11_DEVICE_
	//TODO: Check GLX_EXT_swap_control and GLX_MESA_swap_control
#ifdef GLX_SGI_swap_control'''

patch(ext_h, old3, new3)

# ============================================================
# Patch 4: COpenGLExtensionHandler.cpp - Add EGL extension loader
# ============================================================
print("Patch 4: COpenGLExtensionHandler.cpp (IRR_OGL_LOAD_EXTENSION)")

ext_cpp = os.path.join(src, 'COpenGLExtensionHandler.cpp')

old4 = '''#elif defined(_IRR_COMPILE_WITH_SDL_DEVICE_) && !defined(_IRR_COMPILE_WITH_X11_DEVICE_)
	#define IRR_OGL_LOAD_EXTENSION(x) SDL_GL_GetProcAddress(reinterpret_cast<const char*>(x))
#else'''

new4 = '''#elif defined(_IRR_COMPILE_WITH_SDL_DEVICE_) && !defined(_IRR_COMPILE_WITH_X11_DEVICE_)
	#define IRR_OGL_LOAD_EXTENSION(x) SDL_GL_GetProcAddress(reinterpret_cast<const char*>(x))
#elif defined(_IRR_COMPILE_WITH_FB_DEVICE_) && !defined(_IRR_COMPILE_WITH_X11_DEVICE_)
	#define IRR_OGL_LOAD_EXTENSION(x) eglGetProcAddress(reinterpret_cast<const char*>(x))
#else'''

patch(ext_cpp, old4, new4)

# ============================================================
# Patch 5: COpenGLDriver.h - Add FB device forward decl
# ============================================================
print("Patch 5: COpenGLDriver.h (forward declaration)")

drv_h = os.path.join(src, 'COpenGLDriver.h')

old5 = '''	class CIrrDeviceWin32;
	class CIrrDeviceLinux;
	class CIrrDeviceSDL;
	class CIrrDeviceMacOSX;'''

new5 = '''	class CIrrDeviceWin32;
	class CIrrDeviceLinux;
	class CIrrDeviceSDL;
	class CIrrDeviceMacOSX;
	class CIrrDeviceFB;'''

patch(drv_h, old5, new5)

# ============================================================
# Patch 6: COpenGLDriver.h - Add FB constructor
# ============================================================
print("Patch 6: COpenGLDriver.h (constructor)")

old6 = '''		#ifdef _IRR_COMPILE_WITH_SDL_DEVICE_
		COpenGLDriver(const SIrrlichtCreationParameters& params, io::IFileSystem* io, CIrrDeviceSDL* device);
		#endif'''

new6 = '''		#ifdef _IRR_COMPILE_WITH_SDL_DEVICE_
		COpenGLDriver(const SIrrlichtCreationParameters& params, io::IFileSystem* io, CIrrDeviceSDL* device);
		#endif

		#ifdef _IRR_COMPILE_WITH_FB_DEVICE_
		COpenGLDriver(const SIrrlichtCreationParameters& params, io::IFileSystem* io, CIrrDeviceFB* device);
		bool initDriver(CIrrDeviceFB* device);
		#endif'''

patch(drv_h, old6, new6)

# ============================================================
# Patch 7: COpenGLDriver.h - Add FB member variable
# ============================================================
print("Patch 7: COpenGLDriver.h (member variable)")

old7 = '''		#ifdef _IRR_COMPILE_WITH_SDL_DEVICE_
			CIrrDeviceSDL *SDLDevice;
		#endif'''

new7 = '''		#ifdef _IRR_COMPILE_WITH_SDL_DEVICE_
			CIrrDeviceSDL *SDLDevice;
		#endif
		#ifdef _IRR_COMPILE_WITH_FB_DEVICE_
			CIrrDeviceFB *FBDevice;
		#endif'''

patch(drv_h, old7, new7)

# ============================================================
# Patch 8: COpenGLDriver.cpp - Add FB include
# ============================================================
print("Patch 8: COpenGLDriver.cpp (include)")

drv_cpp = os.path.join(src, 'COpenGLDriver.cpp')
s = open(drv_cpp).read()

# Add FB device include after the existing device includes
old8 = '#include "COpenGLDriver.h"'
new8 = '''#include "COpenGLDriver.h"
#ifdef _IRR_COMPILE_WITH_FB_DEVICE_
#include "CIrrDeviceFB.h"
#endif'''

assert old8 in s, f"Patch 8 target not found in COpenGLDriver.cpp"
s = s.replace(old8, new8, 1)

# ============================================================
# Patch 9: COpenGLDriver.cpp - Add FB constructor
# ============================================================
print("Patch 9: COpenGLDriver.cpp (constructor)")

old9 = '''#endif // _IRR_COMPILE_WITH_SDL_DEVICE_


//! destructor
COpenGLDriver::~COpenGLDriver()'''

new9 = '''#endif // _IRR_COMPILE_WITH_SDL_DEVICE_


// -----------------------------------------------------------------------
// FB/DRM CONSTRUCTOR
// -----------------------------------------------------------------------
#ifdef _IRR_COMPILE_WITH_FB_DEVICE_
COpenGLDriver::COpenGLDriver(const SIrrlichtCreationParameters& params,
		io::IFileSystem* io, CIrrDeviceFB* device)
: CNullDriver(io, params.WindowSize), COpenGLExtensionHandler(),
	CurrentRenderMode(ERM_NONE), ResetRenderStates(true),
	Transformation3DChanged(true), AntiAlias(params.AntiAlias),
	RenderTargetTexture(0), CurrentRendertargetSize(0,0), ColorFormat(ECF_R8G8B8),
	CurrentTarget(ERT_FRAME_BUFFER), Params(params),
	FBDevice(device), DeviceType(EIDT_FRAMEBUFFER)
{
	#ifdef _DEBUG
	setDebugName("COpenGLDriver");
	#endif

	#ifdef _IRR_COMPILE_WITH_CG_
	CgContext = 0;
	#endif

	genericDriverInit();
}

bool COpenGLDriver::initDriver(CIrrDeviceFB* device)
{
	genericDriverInit();
	return true;
}
#endif // _IRR_COMPILE_WITH_FB_DEVICE_


//! destructor
COpenGLDriver::~COpenGLDriver()'''

assert old9 in s, f"Patch 9 target not found in COpenGLDriver.cpp"
s = s.replace(old9, new9, 1)

# ============================================================
# Patch 10: COpenGLDriver.cpp - Add FB endScene
# ============================================================
print("Patch 10: COpenGLDriver.cpp (endScene)")

old10 = '''#ifdef _IRR_COMPILE_WITH_SDL_DEVICE_
	if (DeviceType == EIDT_SDL)
	{
		SDL_GL_SwapBuffers();
		return true;
	}
#endif

	// todo: console device present

	return false;'''

new10 = '''#ifdef _IRR_COMPILE_WITH_SDL_DEVICE_
	if (DeviceType == EIDT_SDL)
	{
		SDL_GL_SwapBuffers();
		return true;
	}
#endif

#ifdef _IRR_COMPILE_WITH_FB_DEVICE_
	if (DeviceType == EIDT_FRAMEBUFFER)
	{
		FBDevice->present(0, 0, 0);
		return true;
	}
#endif

	// todo: console device present

	return false;'''

assert old10 in s, f"Patch 10 target not found in COpenGLDriver.cpp"
s = s.replace(old10, new10, 1)

# ============================================================
# Patch 11: COpenGLDriver.cpp - Add FB factory function
# ============================================================
print("Patch 11: COpenGLDriver.cpp (factory function)")

old11 = '''} // end namespace
} // end namespace

'''

# This is at the very end of the file, after the SDL factory function
# We need to be specific about which occurrence to replace
# The factory functions are all at the end, so find the last occurrence
# Actually there are two: one inside #ifdef _IRR_COMPILE_WITH_OPENGL_ and one outside
# The last one is the namespace closing after SDL factory

# Let me use a more specific anchor
old11 = '''#ifdef _IRR_COMPILE_WITH_SDL_DEVICE_
IVideoDriver* createOpenGLDriver(const SIrrlichtCreationParameters& params,
		io::IFileSystem* io, CIrrDeviceSDL* device)
{
#ifdef _IRR_COMPILE_WITH_OPENGL_
	return new COpenGLDriver(params, io, device);
#else
	return 0;
#endif //  _IRR_COMPILE_WITH_OPENGL_
}
#endif // _IRR_COMPILE_WITH_SDL_DEVICE_

} // end namespace
} // end namespace'''

new11 = '''#ifdef _IRR_COMPILE_WITH_SDL_DEVICE_
IVideoDriver* createOpenGLDriver(const SIrrlichtCreationParameters& params,
		io::IFileSystem* io, CIrrDeviceSDL* device)
{
#ifdef _IRR_COMPILE_WITH_OPENGL_
	return new COpenGLDriver(params, io, device);
#else
	return 0;
#endif //  _IRR_COMPILE_WITH_OPENGL_
}
#endif // _IRR_COMPILE_WITH_SDL_DEVICE_

// -----------------------------------
// FB/DRM VERSION
// -----------------------------------
#ifdef _IRR_COMPILE_WITH_FB_DEVICE_
IVideoDriver* createOpenGLDriver(const SIrrlichtCreationParameters& params,
		io::IFileSystem* io, CIrrDeviceFB* device)
{
#ifdef _IRR_COMPILE_WITH_OPENGL_
	COpenGLDriver* ogl = new COpenGLDriver(params, io, device);
	if (!ogl->initDriver(device))
	{
		ogl->drop();
		ogl = 0;
	}
	return ogl;
#else
	return 0;
#endif //  _IRR_COMPILE_WITH_OPENGL_
}
#endif // _IRR_COMPILE_WITH_FB_DEVICE_

} // end namespace
} // end namespace'''

assert old11 in s, f"Patch 11 target not found in COpenGLDriver.cpp"
s = s.replace(old11, new11, 1)

open(drv_cpp, 'w').write(s)
print(f"  Patched: COpenGLDriver.cpp")

print(f"\nAll 11 patches applied successfully to {base}")
print("DRM/KMS/GBM/EGL support enabled, X11 disabled")

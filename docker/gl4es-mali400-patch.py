#!/usr/bin/env python3
"""Patch gl4es v1.1.6 glx.c for Mali 400 EGL compatibility.

Mali 400's EGL may not support EGL_WINDOW_BIT on EGL_DEFAULT_DISPLAY (used with
LIBGL_FB=1), and glXCreateContext's default config (depth=24, stencil=8) may not
match available EGL configs. This adds fallback retries with minimal EGL attributes,
plus diagnostic logging to help debug EGL config failures.
"""
import sys

path = sys.argv[1] if len(sys.argv) > 1 else 'src/glx/glx.c'
s = open(path).read()

# Patch 1: glXChooseFBConfig - add fallback with debug logging before giving up
old1 = '''        if(*count==0) {  // NO Config found....
            DBG(printf("glXChooseFBConfig found 0 config\\n");)
            return NULL;
        }'''
new1 = '''        if(*count==0) {
            LOGD("gl4es Mali400: glXChooseFBConfig retry with EGL_PBUFFER_BIT (eglDisplay=%p)\\n", eglDisplay);
            attr[0]=EGL_SURFACE_TYPE; attr[1]=EGL_PBUFFER_BIT;
            egl_eglChooseConfig(eglDisplay, attr, NULL, 0, count);
            LOGD("gl4es Mali400: pbuffer retry count=%d\\n", *count);
        }
        if(*count==0) {
            LOGD("gl4es Mali400: glXChooseFBConfig retry with minimal attrs\\n");
            attr[0]=EGL_RENDERABLE_TYPE;
            attr[1]=(hardext.esversion==1)?EGL_OPENGL_ES_BIT:EGL_OPENGL_ES2_BIT;
            attr[2]=EGL_NONE;
            egl_eglChooseConfig(eglDisplay, attr, NULL, 0, count);
            LOGD("gl4es Mali400: minimal retry count=%d\\n", *count);
        }
        if(*count==0) {
            // Last resort: try EGL_NONE only
            EGLint none_attr[] = {EGL_NONE};
            egl_eglChooseConfig(eglDisplay, none_attr, NULL, 0, count);
            LOGD("gl4es Mali400: EGL_NONE-only retry count=%d\\n", *count);
            if(*count>0) {
                attr[0]=EGL_NONE;
            }
        }
        if(*count==0) {
            LOGE("gl4es Mali400: glXChooseFBConfig found 0 configs after all retries (eglDisplay=%p, eglInitialized=%d)\\n", eglDisplay, eglInitialized);
            CheckEGLErrors();
            return NULL;
        }'''
assert old1 in s, 'Patch 1 target not found in ' + path
s = s.replace(old1, new1, 1)

# Patch 2: glXCreateContext - retry with minimal attrs before failing, with logging
old2 = '''    if (result != EGL_TRUE || fake->eglConfigsCount == 0) {
        DBG(printf(" => %p\\n", NULL);)
        LOGE("No EGL configs found (depth=%d, stencil=%d).\\n", depthBits, glxfbconfig->stencilBits);
        CheckEGLErrors();
        free(fake);
        return 0;
    }'''
new2 = '''    if (result != EGL_TRUE || fake->eglConfigsCount == 0) {
        LOGD("gl4es Mali400: glXCreateContext retry with minimal attrs (result=%d, count=%d, eglDisplay=%p)\\n", result, fake->eglConfigsCount, eglDisplay);
        EGLint _ma[] = {EGL_RENDERABLE_TYPE,
            (hardext.esversion==1)?EGL_OPENGL_ES_BIT:EGL_OPENGL_ES2_BIT,
            EGL_SURFACE_TYPE, EGL_WINDOW_BIT, EGL_NONE};
        result = egl_eglChooseConfig(eglDisplay, _ma,
            fake->eglConfigs, 64, &fake->eglConfigsCount);
        LOGD("gl4es Mali400: window retry count=%d\\n", fake->eglConfigsCount);
        if (fake->eglConfigsCount == 0) {
            _ma[3] = EGL_PBUFFER_BIT;
            result = egl_eglChooseConfig(eglDisplay, _ma,
                fake->eglConfigs, 64, &fake->eglConfigsCount);
            LOGD("gl4es Mali400: pbuffer retry count=%d\\n", fake->eglConfigsCount);
        }
        if (fake->eglConfigsCount == 0) {
            EGLint none_attr[] = {EGL_NONE};
            result = egl_eglChooseConfig(eglDisplay, none_attr,
                fake->eglConfigs, 64, &fake->eglConfigsCount);
            LOGD("gl4es Mali400: EGL_NONE-only retry count=%d\\n", fake->eglConfigsCount);
        }
    }
    if (result != EGL_TRUE || fake->eglConfigsCount == 0) {
        LOGE("No EGL configs found (depth=%d, stencil=%d, eglDisplay=%p).\\n", depthBits, glxfbconfig->stencilBits, eglDisplay);
        CheckEGLErrors();
        free(fake);
        return 0;
    }'''
assert old2 in s, 'Patch 2 target not found in ' + path
s = s.replace(old2, new2, 1)

# Patch 3: Add logging to init_display to show which EGL display is being used
old3 = '''static void init_display(Display *display) {
    LOAD_EGL(eglGetDisplay);

    if (! g_display) {
        g_display = display;//XOpenDisplay(NULL);
    }'''
new3 = '''static void init_display(Display *display) {
    LOAD_EGL(eglGetDisplay);
    LOGD("gl4es Mali400: init_display(display=%p), usefb=%d, usepbuffer=%d\\n", display, globals4es.usefb, globals4es.usepbuffer);

    if (! g_display) {
        g_display = display;//XOpenDisplay(NULL);
    }'''
assert old3 in s, 'Patch 3 target not found in ' + path
s = s.replace(old3, new3, 1)

# Patch 4: Log the result of eglGetDisplay and eglInitialize
old4 = '''    if(!eglDisplay) {
        if (globals4es.usefb || globals4es.usepbuffer) {
            eglDisplay = egl_eglGetDisplay(EGL_DEFAULT_DISPLAY);
        } else {
            eglDisplay = egl_eglGetDisplay(display);
        }
    }
}'''
new4 = '''    if(!eglDisplay) {
        if (globals4es.usefb || globals4es.usepbuffer) {
            eglDisplay = egl_eglGetDisplay(EGL_DEFAULT_DISPLAY);
            LOGD("gl4es Mali400: eglGetDisplay(EGL_DEFAULT_DISPLAY) = %p\\n", eglDisplay);
        } else {
            eglDisplay = egl_eglGetDisplay(display);
            LOGD("gl4es Mali400: eglGetDisplay(x_display=%p) = %p\\n", display, eglDisplay);
            if (!eglDisplay) {
                eglDisplay = egl_eglGetDisplay(EGL_DEFAULT_DISPLAY);
                LOGD("gl4es Mali400: X11 display failed, fallback EGL_DEFAULT_DISPLAY = %p\\n", eglDisplay);
            }
        }
    }
}'''
assert old4 in s, 'Patch 4 target not found in ' + path
s = s.replace(old4, new4, 1)

# Patch 5: glXMakeCurrent - create fbdev_window when create_native_window returns NULL
# On Mali 400 fbdev, create_native_window() returns NULL because it only handles RPi and GBM.
# Mali's fbdev EGL expects a fbdev_window struct {unsigned short width, height} as the
# native window handle. Without this, eglCreateWindowSurface gets NULL → EGL_BAD_NATIVE_WINDOW
# → no surface → eglMakeCurrent fails → all GL calls crash with SIGSEGV.
old5 = '''                            if(eglSurf == EGL_NO_SURFACE) {
                                context->nativewin = create_native_window(width,height);
#if 0//ndef NO_GBM
                                if(globals4es.usegbm) {
                                    LOAD_EGL_EXT(eglCreatePlatformWindowSurface);
                                    eglSurf = egl_eglCreatePlatformWindowSurface(eglDisplay, context->eglConfigs[context->eglconfigIdx], context->nativewin, attrib_list);
                                } else
#endif
                                eglSurf = egl_eglCreateWindowSurface(eglDisplay, context->eglConfigs[context->eglconfigIdx], (EGLNativeWindowType)context->nativewin, attrib_list);
                            } else {
                                DBG(printf("LIBGL: EglSurf Recycled\\n");)
                            }
                            eglSurface = context->eglSurface = eglSurf;
                            if(!eglSurf) {
                                DBG(printf("LIBGL: Warning, EglSurf is null\\n");)
                                CheckEGLErrors();
                            }'''
new5 = '''                            if(eglSurf == EGL_NO_SURFACE) {
                                context->nativewin = create_native_window(width,height);
                                if(context->nativewin) {
#if 0//ndef NO_GBM
                                    if(globals4es.usegbm) {
                                        LOAD_EGL_EXT(eglCreatePlatformWindowSurface);
                                        eglSurf = egl_eglCreatePlatformWindowSurface(eglDisplay, context->eglConfigs[context->eglconfigIdx], context->nativewin, attrib_list);
                                    } else
#endif
                                    eglSurf = egl_eglCreateWindowSurface(eglDisplay, context->eglConfigs[context->eglconfigIdx], (EGLNativeWindowType)context->nativewin, attrib_list);
                                }
                                if(!eglSurf) {
                                    /* Mali fbdev: create fbdev_window struct as native window */
                                    typedef struct { unsigned short width; unsigned short height; } _fbdev_window;
                                    _fbdev_window *fwin = (_fbdev_window*)malloc(sizeof(_fbdev_window));
                                    fwin->width = (unsigned short)width;
                                    fwin->height = (unsigned short)height;
                                    context->nativewin = fwin;
                                    LOGD("gl4es Mali400: created fbdev_window %dx%d at %p\\n", width, height, fwin);
                                    eglSurf = egl_eglCreateWindowSurface(eglDisplay, context->eglConfigs[context->eglconfigIdx], (EGLNativeWindowType)fwin, attrib_list);
                                    LOGD("gl4es Mali400: fbdev_window surface = %p\\n", eglSurf);
                                }
                                if(!eglSurf) {
                                    /* Last resort: try pbuffer surface */
                                    LOGD("gl4es Mali400: window surface failed, trying pbuffer %dx%d\\n", width, height);
                                    LOAD_EGL(eglCreatePbufferSurface);
                                    EGLint pb_attribs[] = {EGL_WIDTH, width, EGL_HEIGHT, height, EGL_NONE};
                                    eglSurf = egl_eglCreatePbufferSurface(eglDisplay, context->eglConfigs[context->eglconfigIdx], pb_attribs);
                                    LOGD("gl4es Mali400: pbuffer surface = %p\\n", eglSurf);
                                }
                            } else {
                                DBG(printf("LIBGL: EglSurf Recycled\\n");)
                            }
                            eglSurface = context->eglSurface = eglSurf;
                            if(!eglSurf) {
                                LOGE("gl4es Mali400: EglSurf is null after all attempts\\n");
                                CheckEGLErrors();
                            }'''
assert old5 in s, 'Patch 5 target not found in ' + path
s = s.replace(old5, new5, 1)

open(path, 'w').write(s)
print(f'Patched {path}: 5 Mali 400 EGL patches applied (fallbacks + fbdev_window + diagnostics)')

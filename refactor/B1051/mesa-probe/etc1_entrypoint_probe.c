/* B1051 (GitLab #371): ETC1 entry-point probe.
 *
 * The willeq client resolves glCompressedTexImage2D through
 * eglGetProcAddress (constrained_texture_cache_compressed.cpp,
 * createETC1Storage) while everything else in its GL path goes through the
 * libGLESv2-linked entry points. This tiny probe answers whether, on a given
 * host, the eglGetProcAddress-resolved function pointer behaves differently
 * from the linked one for a legal ETC1 upload (16x16, level 0, linear
 * filters set first = the client's exact sequence):
 *
 *   linked:      glCompressedTexImage2D(...)
 *   eglproc:     eglGetProcAddress("glCompressedTexImage2D")(...)
 *
 * If both are accepted (or both rejected identically) the resolution path is
 * not the discriminator; the same binary also reports the extension list.
 *
 * Build:   gcc -O2 etc1_entrypoint_probe.c -o probe -lEGL -lGLESv2
 * Run:     xvfb-run -a ./probe   (X11 host)   |   ./probe   (DRM host)
 */
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>

#ifndef GL_ETC1_RGB8_OES
#define GL_ETC1_RGB8_OES 0x8D64
#endif

typedef void (*CompressedTexImage2Dfn)(GLenum, GLint, GLenum, GLsizei,
                                       GLsizei, GLint, GLsizei, const GLvoid *);

static uint32_t testUpload(const char *label, CompressedTexImage2Dfn pfn,
                           const uint8_t *data)
{
    if (!pfn) {
        printf("[ep] %s: pfn=NULL\n", label);
        return 0xFFFFFFFF;
    }
    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    pfn(GL_TEXTURE_2D, 0, GL_ETC1_RGB8_OES, 16, 16, 0, 256, data);
    GLenum err = glGetError();
    printf("[ep] %s: glError = 0x%04x (%s)\n", label, err,
           err == GL_NO_ERROR ? "ACCEPTED" : "REJECTED");
    glDeleteTextures(1, &tex);
    return err;
}

int main(void)
{
    EGLDisplay dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (dpy == EGL_NO_DISPLAY) { printf("FAIL eglGetDisplay\n"); return 2; }
    EGLint maj, min;
    if (!eglInitialize(dpy, &maj, &min)) { printf("FAIL eglInitialize\n"); return 2; }

    const EGLint cfgAttrs[] = {
        EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
        EGL_NONE };
    EGLConfig cfg; EGLint ncfg;
    if (!eglChooseConfig(dpy, cfgAttrs, &cfg, 1, &ncfg) || ncfg == 0) {
        printf("FAIL eglChooseConfig\n"); return 2;
    }
    const EGLint pbufAttribs[] = { EGL_WIDTH, 16, EGL_HEIGHT, 16, EGL_NONE };
    EGLSurface surf = eglCreatePbufferSurface(dpy, cfg, pbufAttribs);
    const EGLint ca2[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
    EGLContext ctx = eglCreateContext(dpy, cfg, EGL_NO_SURFACE, ca2);
    if (ctx == EGL_NO_CONTEXT ||
        !eglMakeCurrent(dpy, surf ? surf : EGL_NO_SURFACE, surf ? surf : EGL_NO_SURFACE, ctx)) {
        printf("FAIL context\n"); return 2;
    }

    const char *ver = (const char *)glGetString(GL_VERSION);
    const char *ext = (const char *)glGetString(GL_EXTENSIONS);
    printf("[ep] GL_VERSION = %s\n", ver ? ver : "?");
    if (ext) {
        int found_etc1 = strstr(ext, "GL_OES_compressed_ETC1_RGB8_texture") != NULL;
        int found_etc2 = strstr(ext, "GL_OES_compressed_ETC2_RGB8") != NULL;
        int found_astc = strstr(ext, "GL_EXT_texture_compression_astc_hdr") != NULL ||
                         strstr(ext, "GL_KHR_texture_compression_astc") != NULL;
        printf("[ep] extensions: ETC1_OES=%d ETC2=%d ASTC=%d\n", found_etc1, found_etc2, found_astc);
        /* Print the FULL extension list to a file so the capability table is
         * complete, not just the three greps. */
        FILE *f = fopen("/tmp/ep_ext.txt", "w");
        if (f) { fputs(ext, f); fclose(f); printf("[ep] full extension list -> /tmp/ep_ext.txt\n"); }
    }

    CompressedTexImage2Dfn linked = (CompressedTexImage2Dfn)(uintptr_t)glCompressedTexImage2D;
    CompressedTexImage2Dfn viaEgl =
        (CompressedTexImage2Dfn)eglGetProcAddress("glCompressedTexImage2D");
    printf("[ep] linked ptr = %p ; eglGetProcAddress = %p\n",
           (void *)linked, (void *)viaEgl);

    /* The client's sequence: 16x16 level 0, linear filters first, 16 blocks. */
    static uint8_t data[256];
    memset(data, 0, sizeof(data));
    for (int b = 0; b < 16; ++b) {
        uint32_t c1 = 0x21 | (0x21 << 5) | (0x10 << 11);   /* some RGB */
        uint32_t word = c1 | (c1 << 14) | (1u << 30);
        data[b * 16 + 0] = (uint8_t)(word & 0xff);
        data[b * 16 + 1] = (uint8_t)((word >> 8) & 0xff);
        data[b * 16 + 2] = (uint8_t)((word >> 16) & 0xff);
        data[b * 16 + 3] = (uint8_t)((word >> 24) & 0xff);
    }

    uint32_t e1 = testUpload("linked-entrypoint ", linked, data);
    uint32_t e2 = testUpload("eglGetProcAddress", viaEgl, data);
    printf("[ep] VERDICT: %s\n",
           e1 == e2 ? "IDENTICAL BEHAVIOR (resolution path not the discriminator)"
                    : "DIVERGENT (resolution path is the discriminator)");

    eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroyContext(dpy, ctx);
    if (surf) eglDestroySurface(dpy, surf);
    eglTerminate(dpy);
    return 0;
}

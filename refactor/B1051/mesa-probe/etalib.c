#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <GLES3/gl3.h>
#include <GLES3/gl3platform.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
typedef EGLDisplay (*GetPD)(EGLenum, void *, const EGLAttrib *);
#ifndef GL_ETC1_RGB8_OES
#define GL_ETC1_RGB8_OES 0x8D64
#endif
static void dumpExt(const char *tag){
    const char *e=(const char*)glGetString(GL_EXTENSIONS);
    printf("[%s] ETC1_OES=%d ETC2=%d ASTC=%d S3TC=%d\n", tag,
        e&&strstr(e,"GL_OES_compressed_ETC1_RGB8_texture")!=NULL,
        e&&strstr(e,"GL_OES_compressed_ETC2_RGB8")!=NULL,
        e&&(strstr(e,"astc")!=NULL||strstr(e,"ASTC")!=NULL),
        e&&strstr(e,"GL_EXT_texture_compression_s3tc")!=NULL);
    /* ES3: enumerate per-context extensions properly */
    const char *(*gssi)(GLuint, GLuint)=(const char*(*)(GLuint,GLuint))eglGetProcAddress("glGetStringi");
    if(gssi){
        GLint ne=0; glGetIntegerv(GL_NUM_EXTENSIONS,&ne);
        int etc1=0, etc2=0;
        for(GLint i=0;i<ne;i++){
            const char *x=gssi(GL_EXTENSIONS,i);
            if(!x) continue;
            if(strstr(x,"GL_OES_compressed_ETC1_RGB8_texture")) etc1=1;
            if(strstr(x,"ETC2")) etc2=1;
            FILE *g=fopen("/home/user/.hermes/profiles/dev/cache/scratch/b1051_stepA/ext_dump3.txt","a");
            if(g){fprintf(g,"%s\n",x);fclose(g);}
        }
        printf("  [glGetStringi n=%d] ETC1=%d ETC2=%d\n",ne,etc1,etc2);
    } else printf("  (no glGetStringi)\n");
    /* Does the driver claim to support the formats? (ES3 query) */
    typedef void (*GetCtpvProc)(GLenum, GLsizei, void **);
    GetCtpvProc getCtpv=(GetCtpvProc)eglGetProcAddress("glGetCompressedTexelPointerv");
    GLint n=0; glGetIntegerv(GL_NUM_COMPRESSED_TEXTURE_FORMATS,&n);
    if(n>0 && n<=128 && getCtpv){
        static void *ptrs[128];
        getCtpv(GL_COMPRESSED_TEXTURE_FORMATS, n, ptrs);
        printf("  COMPRESSED_FORMATS n=%d:",n);
        for(GLint i=0;i<n;i++)
            if(ptrs[i] && (i==0x8D64||i==0x9274||i==0x83F0||i==0x87FA||i==0x93B0||i==0x93BD))
                printf(" 0x%04X",i);
        printf("\n");
    } else printf("  COMPRESSED_FORMATS n=%d (query unavailable)\n",n);
}
static int upload(const char *label, GLenum fmt, int w, int h, const void *data, size_t sz)
{
    GLuint t; glGenTextures(1,&t);
    GLenum e0=glGetError();
    if(!t||e0){printf("  glGenTextures FAILED t=%u err=0x%04x\n",t,e0);return 0x7777;}
    glBindTexture(GL_TEXTURE_2D,t);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glCompressedTexImage2D(GL_TEXTURE_2D,0,fmt,w,h,0,(GLsizei)sz,data);
    GLenum e=glGetError();
    printf("  %-20s 0x%04x %s\n", label, e, e==GL_NO_ERROR?"ACCEPTED":"REJECTED");
    glDeleteTextures(1,&t);
    return e;
}
int main(void)
{
    GetPD getPD=(GetPD)eglGetProcAddress("eglGetPlatformDisplayEXT");
    if(!getPD) getPD=eglGetPlatformDisplay;
    EGLDisplay dpy=getPD(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, (const void*)0);
    if(dpy==EGL_NO_DISPLAY){ printf("no surfaceless\n"); return 2; }
    EGLint maj,min; if(!eglInitialize(dpy,&maj,&min)){printf("no init\n");return 2;}
    printf("EGL %d.%d\n",maj,min);
    if (!eglBindAPI(EGL_OPENGL_ES_API)) { printf("no ES api\n"); return 2; }
    const EGLint ca[]={EGL_SURFACE_TYPE,EGL_PBUFFER_BIT,EGL_RED_SIZE,8,EGL_GREEN_SIZE,8,EGL_BLUE_SIZE,8,EGL_ALPHA_SIZE,8,EGL_DEPTH_SIZE,16,EGL_RENDERABLE_TYPE,EGL_OPENGL_ES2_BIT,EGL_NONE};
    EGLConfig cfg; EGLint n; if(!eglChooseConfig(dpy,ca,&cfg,1,&n)||!n){printf("no cfg (trying no-surface-type)\n");
        const EGLint ca2[]={EGL_RENDERABLE_TYPE,EGL_OPENGL_ES2_BIT,EGL_NONE};
        if(!eglChooseConfig(dpy,ca2,&cfg,1,&n)||!n){printf("no cfg\n");return 2;}}
    const EGLint c2[]={EGL_CONTEXT_CLIENT_VERSION,2,EGL_NONE};
    EGLContext ctx=eglCreateContext(dpy,cfg,EGL_NO_SURFACE,c2);
    EGLSurface pbuf=eglCreatePbufferSurface(dpy,cfg, (EGLint[]){EGL_WIDTH,64,EGL_HEIGHT,64,EGL_NONE});
    EGLBoolean mc=eglMakeCurrent(dpy,pbuf? pbuf : EGL_NO_SURFACE,pbuf? pbuf : EGL_NO_SURFACE,ctx);
    printf("makeCurrent=%d eglErr=0x%x curCtx=%p\n", (int)mc, (int)eglGetError(), (void*)eglGetCurrentContext());
    printf("ver=%s\n",glGetString(GL_VERSION));
    printf("render=%s vendor=%s\n", glGetString(GL_RENDERER), glGetString(GL_VENDOR));
    dumpExt("surfaceless ES2");
    uint8_t d[1024]; memset(d,0x55,sizeof d);
    printf("uploads (surfaceless, windowless):\n");
    {
        unsigned char px[16*16*4];
        for(int i=0;i<16*16;i++){px[i*4]=255;px[i*4+1]=128;px[i*4+2]=0;px[i*4+3]=255;}
        GLuint t0; glGenTextures(1,&t0); glBindTexture(GL_TEXTURE_2D,t0);
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,16,16,0,GL_RGBA,GL_UNSIGNED_BYTE,px);
        GLenum er=glGetError();
        printf("  RGBA8 16x16           0x%04x %s\n", er, er==GL_NO_ERROR?"ACCEPTED":"REJECTED");
        glDeleteTextures(1,&t0);
    }
    upload("ETC1 16x16 (128B)",0x8D64,16,16,d,128);
    upload("ETC1 64x64 (2048B)",0x8D64,64,64,d,2048);
    upload("S3TC_DXT1 16x16 (128B)",0x83F0,16,16,d,128);
    eglMakeCurrent(dpy,EGL_NO_SURFACE,EGL_NO_SURFACE,EGL_NO_CONTEXT);
    eglDestroyContext(dpy,ctx);
    const EGLint c3[]={EGL_CONTEXT_CLIENT_VERSION,3,EGL_NONE};
    ctx=eglCreateContext(dpy,cfg,EGL_NO_SURFACE,c3);
    eglMakeCurrent(dpy,EGL_NO_SURFACE,EGL_NO_SURFACE,ctx);
    printf("ver=%s\n",glGetString(GL_VERSION));
    dumpExt("surfaceless ES3");
    printf("uploads (surfaceless ES3):\n");
    upload("ETC1 16x16 (128B)",0x8D64,16,16,d,128);
    upload("ETC1 64x64 (2048B)",0x8D64,64,64,d,2048);
    upload("S3TC_DXT1 16x16 (128B)",0x83F0,16,16,d,128);
    return 0;
}

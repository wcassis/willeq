// B1051 probe (x86 llvmpipe, surfaceless EGL — same device the client uses):
// upload the REAL qcf per-part ETC1 payloads straight from qeynos2_chr.eqe1
// and read them back via glReadPixels. Compare the decoded pixels:
//  (1) each part vs its own source BMP (is the pack's ETC1 a correct
//      encoding of the BMP's pixels?),
//  (2) part A vs part B (do DISTINCT payloads decode to the SAME pixels —
//      the smear signature, or not?).
// Exit 0 always; prints a verdict table.
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <GLES3/gl3.h>
#include <GLES3/gl3platform.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <math.h>
typedef EGLDisplay (*GetPD)(EGLenum, void *, const EGLint *);
#ifndef GL_ETC1_RGB8_OES
#define GL_ETC1_RGB8_OES 0x8D64
#endif

static int etc1_size(int w, int h){
    return ((w+3)/4)*((h+3)/4)*8;
}
// Parse one pack: returns count; fills tables. Caller frees.
typedef struct { char name[64]; int w,h,mips,hasAlpha; size_t off,size; } Entry;
static int parsePack(const char *path, Entry *tab, int cap){
    FILE *f=fopen(path,"rb"); if(!f) return -1;
    fseek(f,0,SEEK_END); long len=ftell(f); fseek(f,0,SEEK_SET);
    uint8_t *d=(uint8_t*)malloc(len); if(fread(d,1,len,f)!=(size_t)len){fclose(f);return -2;}
    fclose(f);
    uint32_t magic; uint16_t ver, n;
    memcpy(&magic,d,4); memcpy(&ver,d+4,2); memcpy(&n,d+6,2);
    if(magic!=0x31455145){free(d);return -3;}
    int esz=(ver>=3)?56:48, idx=16, out=0;
    for(uint16_t i=0;i<n && out<cap;i++){
        size_t off=idx+(size_t)i*esz;
        char name[33]; memcpy(name,d+off,32); name[32]=0;
        Entry e; memset(&e,0,sizeof e);
        memcpy(e.name,name,32);
        e.w=d[off+32]|(d[off+33]<<8); e.h=d[off+34]|(d[off+35]<<8);
        e.mips=d[off+36]; e.hasAlpha=d[off+37];
        memcpy(&e.off,d+off+40,4); memcpy(&e.size,d+off+44,4);
        if(ver>=3){
            uint32_t noff, nlen; memcpy(&noff,d+off+48,4); memcpy(&nlen,d+off+52,4);
            if(noff && nlen && nlen<63) { memcpy(e.name,d+noff,nlen); e.name[nlen]=0; }
        }
        tab[out++]=e;
    }
    free(d);
    return out;
}
static const Entry *find(Entry *tab, int n, const char *name){
    for(int i=0;i<n;i++) if(strcmp(tab[i].name,name)==0) return &tab[i];
    return 0;
}

typedef struct { int w,h; uint8_t *px; } Img; // RGBA
static int readBMP(const char *path, Img *im){
    FILE *f=fopen(path,"rb"); if(!f) return 0;
    fseek(f,0,SEEK_END); long len=ftell(f); fseek(f,0,SEEK_SET);
    uint8_t *d=(uint8_t*)malloc(len); fread(d,1,len,f); fclose(f);
    // minimal BMP: 14+40 header, bottom-up, 32bpp
    int w=d[18]|(d[19]<<8)|(d[20]<<16)|(d[21]<<24);
    int h=d[22]|(d[23]<<8)|(d[24]<<16)|(d[25]<<24);
    int bpp=d[28]|(d[29]<<8);
    if(h<0){ h=-h; }
    im->w=w; im->h=h;
    im->px=(uint8_t*)malloc((size_t)w*h*4);
    int row=((w*4+3)/4)*4; int hdr=54;
    if(bpp!=32){ free(d); free(im->px); im->px=0; return 0; }
    for(int y=0;y<h;y++){
        int srcY=(h-1-y);
        for(int x=0;x<w;x++){
            const uint8_t *s=d+hdr+srcY*row+x*4;
            im->px[(y*w+x)*4+0]=s[2];
            im->px[(y*w+x)*4+1]=s[3];
            im->px[(y*w+x)*4+2]=s[4];
            im->px[(y*w+x)*4+3]=s[5];
        }
    }
    free(d);
    return 1;
}

static GLuint uploadETC1(const Entry *e, const uint8_t *payload){
    GLuint t; glGenTextures(1,&t); glBindTexture(GL_TEXTURE_2D,t);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    // EXACT client behavior (createETC1Storage, non-legacy): level 0 only,
    // size = ETC1 block size of level 0, filter set before upload.
    glCompressedTexImage2D(GL_TEXTURE_2D,0,GL_ETC1_RGB8_OES,e->w,e->h,0,etc1_size(e->w,e->h),payload);
    GLenum err=glGetError();
    printf("  upload %-16s %4dx%-4d L0size=%d err=0x%04x\n", e->name, e->w, e->h,
           etc1_size(e->w,e->h), err);
    return t;
}
static void readPixels(GLuint t, int w, int h, uint8_t *outRGBA){
    GLuint fbo,rbo,sbo;
    glGenFramebuffers(1,&fbo); glBindFramebuffer(GL_FRAMEBUFFER,fbo);
    glGenRenderbuffers(1,&rbo); glBindRenderbuffer(GL_RENDERBUFFER,rbo);
    glRenderbufferStorage(GL_RENDERBUFFER,GL_RGBA8,w,h);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_RENDERBUFFER,rbo);
    if(glCheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE){
        printf("  FBO INCOMPLETE 0x%x\n",glGetError()); return;
    }
    glGenTextures(1,&sbo);
    glBindTexture(GL_TEXTURE_2D,t);
    // draw the texture full-quad
    const char *vs="#version 100\nattribute vec2 p; varying vec2 uv;\nvoid main(){uv=p*0.5+0.5;gl_Position=vec4(p,0,1);}";
    const char *fs="#version 100\nprecision mediump float; varying vec2 uv;\nuniform sampler2D s;\nvoid main(){gl_FragColor=vec4(texture2D(s,uv).rgb,1.0);}";
    GLuint prog=glCreateProgram();
    GLuint v=glCreateShader(GL_VERTEX_SHADER); glShaderSource(v,1,&vs,0); glCompileShader(v);
    GLuint f=glCreateShader(GL_FRAGMENT_SHADER); glShaderSource(f,1,&fs,0); glCompileShader(f);
    glAttachShader(prog,v); glAttachShader(prog,f); glLinkProgram(prog);
    glUseProgram(prog);
    static float quad[4][2]={{-1,-1},{1,-1},{-1,1},{1,1}};
    int a=glGetAttribLocation(prog,"p");
    glEnableVertexAttribArray(a); glVertexAttribPointer(a,2,GL_FLOAT,0,0,quad);
    glViewport(0,0,w,h);
    int u=glGetUniformLocation(prog,"s"); glUniform1i(u,0);
    glDrawArrays(GL_TRIANGLE_STRIP,0,4);
    glReadPixels(0,0,w,h,GL_RGBA,GL_UNSIGNED_BYTE,outRGBA);
    glDeleteProgram(prog); glDeleteShader(v); glDeleteShader(f);
    glBindFramebuffer(GL_FRAMEBUFFER,0);
    glDeleteFramebuffers(1,&fbo); glDeleteRenderbuffers(1,&rbo);
}
static double meandist(const uint8_t *a, const uint8_t *b, int n){
    double s=0; for(int i=0;i<n;i++){
        s+=fabs(a[i*4]-b[i*4])+fabs(a[i*4+1]-b[i*4+1])+fabs(a[i*4+2]-b[i*4+2]);
    }
    return s/(n*3.0);
}

int main(int argc, char **argv){
    const char *pack = argc>1 ? argv[1] : "/mnt/projects/EverQuest_ETC1/qeynos2_chr.eqe1";
    const char *bmpdir = argc>2 ? argv[2] : "/mnt/projects/EverQuest_ETC1";
    static Entry tab[4096];
    int n=parsePack(pack,tab,4096);
    if(n<0){ printf("parse pack failed %d\n",n); return 2; }
    printf("pack: %d entries\n",n);
    const char *parts[]={"qcfch0001","qcfhe0001","qcflg0001","qcfhn0001","qcfft0001","qcffa0001","qcfua0001"};
    Entry *sel[7]; int ns=0;
    for(int i=0;i<7;i++){ const Entry *e=find(tab,n,parts[i]); if(e){ sel[ns++]=e; printf("  sel %s %dx%d\n",e->name,e->w,e->h);} }
    GetPD getPD=(GetPD)eglGetProcAddress("eglGetPlatformDisplayEXT");
    if(!getPD) getPD=eglGetPlatformDisplay;
    // Try the client's exact sequence, plus the X11 platform it may have
    // fallen back to (its maps show libX11 loaded; Xvfb :99 is up).
    static const struct { const char *tag; EGLenum plat; } plats[] = {
        {"surfaceless", 0x31DD /*EGL_PLATFORM_SURFACELESS_MESA*/},
        {"x11",         0x31D5 /*EGL_PLATFORM_X11_KHR*/},
    };
    EGLDisplay dpy=EGL_NO_DISPLAY; const char *used="none";
    for(unsigned i=0;i<sizeof plats/sizeof plats[0] && dpy==EGL_NO_DISPLAY;i++){
        EGLDisplay d2=getPD(plats[i].plat, EGL_DEFAULT_DISPLAY, (const EGLint*)0);
        int e=(int)eglGetError();
        printf("platform %s -> %p err=0x%x\n", plats[i].tag, (void*)d2, e);
        if(d2!=EGL_NO_DISPLAY){ dpy=d2; used=plats[i].tag; }
    }
    if(dpy==EGL_NO_DISPLAY){ dpy=eglGetDisplay(EGL_DEFAULT_DISPLAY); used="default"; }
    if(dpy==EGL_NO_DISPLAY){ printf("no display at all\n"); return 2; }
    EGLint maj,min; EGLBoolean ok=eglInitialize(dpy,&maj,&min);
    printf("using platform %s: eglInitialize=%d (0x%x) %d.%d\n", used, (int)ok, (int)eglGetError(), maj, min);
    eglBindAPI(EGL_OPENGL_ES_API);
    { EGLConfig all[64]; EGLint nall=0;
      eglChooseConfig(dpy,(const EGLint[]){EGL_NONE},all,64,&nall);
      printf("chooseConfig ANY n=%d err=0x%x\n",(int)nall,(int)eglGetError());
      for(int i=0;i<nall && i<6;i++){
          EGLint r,g,b,a,d,s,rt,st;
          eglGetConfigAttrib(dpy,all[i],EGL_RED_SIZE,&r); eglGetConfigAttrib(dpy,all[i],EGL_GREEN_SIZE,&g);
          eglGetConfigAttrib(dpy,all[i],EGL_BLUE_SIZE,&b); eglGetConfigAttrib(dpy,all[i],EGL_ALPHA_SIZE,&a);
          eglGetConfigAttrib(dpy,all[i],EGL_DEPTH_SIZE,&d); eglGetConfigAttrib(dpy,all[i],EGL_STENCIL_SIZE,&s);
          eglGetConfigAttrib(dpy,all[i],EGL_RENDERABLE_TYPE,&rt); eglGetConfigAttrib(dpy,all[i],EGL_SURFACE_TYPE,&st);
          printf("  cfg%d r%dg%db%da%d dep%d st%d rt=0x%x st=0x%x\n",i,r,g,b,a,d,s,rt,st);
      } }
    const EGLint ca[]={EGL_SURFACE_TYPE,EGL_PBUFFER_BIT,EGL_RED_SIZE,8,EGL_GREEN_SIZE,8,EGL_BLUE_SIZE,8,EGL_ALPHA_SIZE,8,EGL_DEPTH_SIZE,24,EGL_RENDERABLE_TYPE,EGL_OPENGL_ES2_BIT,EGL_NONE};
    EGLConfig cfg; EGLint ncfg; eglChooseConfig(dpy,ca,&cfg,1,&ncfg);
    printf("chooseConfig(pbuffer+es2) n=%d err=0x%x\n", (int)ncfg, (int)eglGetError());
    const EGLint c2[]={EGL_CONTEXT_CLIENT_VERSION,2,EGL_NONE};
    EGLContext ctx=eglCreateContext(dpy,cfg,EGL_NO_SURFACE,c2);
    printf("createContext=%p err=0x%x\n", (void*)ctx, (int)eglGetError());
    EGLint pa[]={EGL_WIDTH,256,EGL_HEIGHT,256,EGL_NONE};
    EGLSurface pbuf=eglCreatePbufferSurface(dpy,cfg,pa);
    printf("pbuffer=%p err=0x%x\n", (void*)pbuf, (int)eglGetError());
    EGLBoolean mc=eglMakeCurrent(dpy,pbuf,pbuf,ctx);
    printf("makeCurrent=%d err=0x%x\n", (int)mc, (int)eglGetError());
    if(!mc){ printf("makeCurrent FAILED\n"); return 3; }
    printf("renderer=%s ver=%s\n", glGetString(GL_RENDERER), glGetString(GL_VERSION));

    // re-read pack payload per entry (parsePack freed the file buffer)
    FILE *pf=fopen(pack,"rb");
    long plen; fseek(pf,0,SEEK_END); plen=ftell(pf); fseek(pf,0,SEEK_SET);
    uint8_t *pfile=malloc(plen); fread(pfile,1,plen,pf); fclose(pf);

    GLuint tex[7]; uint8_t *dec[7]={0};
    for(int i=0;i<ns;i++){
        tex[i]=uploadETC1(sel[i], pfile+sel[i]->off);
        dec[i]=malloc((size_t)sel[i]->w*sel[i]->h*4);
        readPixels(tex[i], sel[i]->w, sel[i]->h, dec[i]);
    }
    // (0) mean decoded color per part: is the armor (ch=chest, he=head)
    // GOLD-ish (high R+G, low B) or dark brown? Compare across parts.
    printf("=== decoded ETC1 mean RGB per part (armor should be gold-ish: R,G high, B low) ===\n");
    for(int i=0;i<ns;i++){
        if(!dec[i]) continue;
        long r=0,g=0,b=0,n=0;
        int w=sel[i]->w,h=sel[i]->h;
        for(int y=0;y<h;y++)for(int x=0;x<w;x++){
            const uint8_t*p=dec[i]+(y*w+x)*4;
            r+=p[0]; g+=p[1]; b+=p[2]; n++;
        }
        printf("  %-12s %4dx%-4d mean=(%3ld,%3ld,%3ld)\n", sel[i]->name, w,h, r/n,g/n,b/n);
    }
    // (1) vs source BMP
    printf("=== decoded ETC1 vs source BMP (mean abs channel distance; ~16-32 = good ETC1) ===\n");
    for(int i=0;i<ns;i++){
        char path[512]; snprintf(path,sizeof path,"%s/%s.bmp",bmpdir,sel[i]->name);
        Img im; memset(&im,0,sizeof im);
        if(!readBMP(path,&im) || im.w!=sel[i]->w || im.h!=sel[i]->h){
            printf("  %-12s BMP missing/dims %s (got %dx%d vs %dx%d)\n", sel[i]->name, path,
                   im.w, im.h, sel[i]->w, sel[i]->h);
            continue;
        }
        printf("  %-12s %4dx%-4d vs BMP: %.1f\n", sel[i]->name, im.w, im.h,
               meandist(dec[i],im.px,im.w*im.h));
        free(im.px);
    }
    // (2) cross-part: do two distinct payloads decode to (near-)identical pixels?
    printf("=== cross-part decoded distance (0 = IDENTICAL pixels = smear; high = distinct) ===\n");
    for(int i=0;i<ns;i++) for(int j=i+1;j<ns;j++){
        int w=sel[i]->w<sel[j]->w?sel[i]->w:sel[j]->w;
        int h=sel[i]->h<sel[j]->h?sel[i]->h:sel[j]->h;
        if(w<4||h<4) continue;
        uint8_t a[16*16*4], b[16*16*4];
        // sample the top-left 16x16 of each
        for(int y=0;y<16;y++) for(int x=0;x<16;x++){
            memcpy(&a[(y*16+x)*4], dec[i]+(y*sel[i]->w+x)*4, 4);
            memcpy(&b[(y*16+x)*4], dec[j]+(y*sel[j]->w+x)*4, 4);
        }
        printf("  %-12s vs %-12s : %.1f\n", sel[i]->name, sel[j]->name, meandist(a,b,256));
    }
    return 0;
}

// B1051 minimal 13-ETC1-texture / 13-buffer client-replica probe.
//
// Reproduces the EXACT client state from the live trace (WILLEQ_B1051_TRACE
// f2837, Guard_Treitan): 13 distinct ETC1 RGB8_OES textures, level-0-only
// upload (client createETC1Storage path), GL_LINEAR+GL_LINEAR, GL_REPEAT,
// each bound to its own mesh buffer in sequence, drawn with a solid3D-style
// shader that samples uTexture on unit 0 and a companion uAlphaTexture on
// unit 1 (the isETC1 material shape).
//
// If the rendered result is ONE flat texture (smear) but each GL object
// holds distinct pixels (verified by readback), the mechanism is reproduced
// and we bisect the GL state below. If it renders distinct, the client's
// state management (not raw GL) is the fault.
//
// Bisect knobs (env):
//   B1051_MIP=1        also upload full mip chain (levels 1..N)
//   B1051_NOALPHA=1    no companion on unit 1 (plain solid shader)
//   B1051_CLAMP=1      GL_CLAMP_TO_EDGE instead of GL_REPEAT
//   B1051_ONEPROG=0    fresh program per draw (default: one program)
//
// Build: gcc -O1 -o etc1_min13_probe etc1_min13_probe.c -lEGL -lGLESv2 -lm
// Run:   DISPLAY=:99 MESA_LOADER_DRIVER_OVERRIDE=llvmpipe ./etc1_min13_probe
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <EGL/egl.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>

static int etc1_block_bytes(int w, int h){
    // ETC1 = 4 bits/pixel = 8 bytes per 4x4 block. Confirmed by the pack's
    // own level sizes (64x128 L0 = 4096 = 16*32*8; 8-mip total = 5480 =
    // entry dsize) and by etc1_decode_probe.c which was ACCEPTED 0x0000
    // on llvmpipe with this formula.
    int bw=(w+3)/4, bh=(h+3)/4;
    return bw*bh*8;
}

// ---- pack reader (same layout as the client's EQE1) ----
struct Entry { char name[64]; int w, h, mips; unsigned off; };
typedef struct Entry Entry;
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
        memcpy(tab[out].name,name,32);
        tab[out].w=d[off+32]|(d[off+33]<<8); tab[out].h=d[off+34]|(d[off+35]<<8);
        tab[out].mips=d[off+36];
        memcpy(&tab[out].off,d+off+40,4);
        if(ver>=3){
            uint32_t noff, nlen; memcpy(&noff,d+off+48,4); memcpy(&nlen,d+off+52,4);
            if(noff && nlen && nlen<63) { memcpy(tab[out].name,d+noff,nlen); tab[out].name[nlen]=0; }
        }
        out++;
    }
    free(d);
    return out;
}
static const Entry *find(Entry *tab, int n, const char *nm){
    for(int i=0;i<n;i++) if(!strcmp(tab[i].name, nm)) return &tab[i];
    return 0;
}

// ---- BMP reader for the reference (qcf*.bmp not on disk; use none) ----

// ---- GL ----
typedef EGLDisplay (*GetPD)(EGLenum, void *, const EGLint *);
#ifndef GL_RGBA8
#define GL_RGBA8 0x8D52
#endif

int main(int argc, char **argv){
    setvbuf(stdout, (char*)0, _IONBF, 0);
    const char *pack = argc>1 ? argv[1] : "/mnt/projects/EverQuest_ETC1/qeynos2_chr.eqe1";
    static Entry tab[4096];
    int n=parsePack(pack, tab, 4096);
    if(n<0){ printf("pack parse failed %d\n", n); return 2; }
    // The 13 QCM textures from the live trace (f2837 guard, width+id order):
    // w=128 (x1), w=64, w=32, w=16, w=8 ... pick 13 distinct by width+name:
    const char *names[] = {"qcfch0001","qcfhe0001","qcflg0001","qcfhn0001",
                           "qcfft0001","qcffa0001","qcfua0001","qcfch0002",
                           "qcfhe0002","qcfhn0002","qcflg0002","qcfft0002"};
    int NN=12;
    Entry *sel[12]; int ns=0;
    for(int i=0;i<NN;i++){ const Entry *e=find(tab,n,names[i]); if(e) sel[ns++]=e; }
    printf("selected %d/%d ETC1 entries\n", ns, NN);

    GetPD getPD=(GetPD)eglGetProcAddress("eglGetPlatformDisplayEXT");
    static const struct { const char *tag; EGLenum plat; } plats[] = {
        {"surfaceless", 0x31DD}, {"x11", 0x31D5},
    };
    EGLDisplay dpy=EGL_NO_DISPLAY; const char *used="none";
    for(unsigned i=0;i<2 && dpy==EGL_NO_DISPLAY;i++){
        EGLDisplay d2=getPD(plats[i].plat, EGL_DEFAULT_DISPLAY, (const EGLint*)0);
        printf("platform %s -> %p err=0x%x\n", plats[i].tag, (void*)d2, (int)eglGetError());
        if(d2!=EGL_NO_DISPLAY){ dpy=d2; used=plats[i].tag; }
    }
    if(dpy==EGL_NO_DISPLAY) dpy=eglGetDisplay(EGL_DEFAULT_DISPLAY);
    EGLint maj,min; if(!eglInitialize(dpy,&maj,&min)){ printf("no egl\n"); return 2; }
    eglBindAPI(EGL_OPENGL_ES_API);
    EGLConfig cfg; EGLint ncfg;
    const EGLint ca[]={EGL_SURFACE_TYPE,EGL_PBUFFER_BIT,EGL_RED_SIZE,8,EGL_GREEN_SIZE,8,EGL_BLUE_SIZE,8,EGL_ALPHA_SIZE,8,EGL_DEPTH_SIZE,24,EGL_RENDERABLE_TYPE,EGL_OPENGL_ES2_BIT,EGL_NONE};
    eglChooseConfig(dpy,ca,&cfg,1,&ncfg);
    if(!ncfg){ printf("no config on %s\n", used); return 2; }
    const EGLint c2[]={EGL_CONTEXT_CLIENT_VERSION,2,EGL_NONE};
    EGLContext ctx=eglCreateContext(dpy,cfg,EGL_NO_SURFACE,c2);
    if(!ctx){ printf("no ctx\n"); return 2; }
    EGLint pa[]={EGL_WIDTH,256,EGL_HEIGHT,256,EGL_NONE};
    EGLSurface pb=eglCreatePbufferSurface(dpy,cfg,pa);
    if(!pb){ printf("no pbuffer\n"); return 2; }
    if(!eglMakeCurrent(dpy,pb,pb,ctx)){ printf("no makecurrent\n"); return 2; }
    printf("renderer=%s ver=%s\n", glGetString(GL_RENDERER), glGetString(GL_VERSION));

    int doMip = getenv("B1051_MIP")!=NULL;
    int noAlpha = getenv("B1051_NOALPHA")!=NULL;
    int doClamp = getenv("B1051_CLAMP")!=NULL;
    int oneProg = getenv("B1051_ONEPROG")==NULL || getenv("B1051_ONEPROG")[0]!='0';

    // Read the pack payloads (per-entry, distinct regions of the file)
    FILE *pf=fopen(pack,"rb"); long plen; fseek(pf,0,SEEK_END); plen=ftell(pf); fseek(pf,0,SEEK_SET);
    uint8_t *pfile=malloc(plen); if(!fread(pfile,1,plen,pf)) return 2; fclose(pf);

    // ---- 1) readback each GL object: distinct? ----
    printf("=== readback: per-object mean (proves GPU-side content distinct) ===\n");
    GLuint tex[12];
    for(int i=0;i<ns;i++){
        glGenTextures(1,&tex[i]); glBindTexture(GL_TEXTURE_2D, tex[i]);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, doMip?GL_LINEAR_MIPMAP_LINEAR:GL_LINEAR);
        GLenum wrap = doClamp?GL_CLAMP_TO_EDGE:GL_REPEAT;
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrap);
        glCompressedTexImage2D(GL_TEXTURE_2D, 0, GL_ETC1_RGB8_OES,
            sel[i]->w, sel[i]->h, 0, etc1_block_bytes(sel[i]->w, sel[i]->h), pfile+sel[i]->off);
        if(doMip){
            int mw=sel[i]->w>>1, mh=sel[i]->h>>1;
            for(int lvl=1; mw>=1 && mh>=1; lvl++, mw>>=1, mh>>=1){
                int bs=etc1_block_bytes(mw,mh);
                // real pack mips live at offset+bs(lvl-1); we only have level0
                // in hand for the probe; synthesize by copying level0 bytes
                // (content correctness of mips is not the question)
                glCompressedTexImage2D(GL_TEXTURE_2D, lvl, GL_ETC1_RGB8_OES,
                    mw, mh, 0, bs, pfile+sel[i]->off);
            }
        }
        GLenum err=glGetError();
        printf("  up %-12s %3dx%-3d mips=%d err=0x%04x\n", sel[i]->name, sel[i]->w, sel[i]->h, doMip?8:1, err);
    }
    // readback helper: draw each texture full-quad to a 32x32 FBO, mean pixel
    // (same as etc1_decode_probe)
    #define RB(n) ({ GLuint t_=tex[n]; GLubyte *d_=malloc(32*32*4); \
        GLuint fbo,rbo; glGenFramebuffers(1,&fbo); glBindFramebuffer(GL_FRAMEBUFFER,fbo); \
        glGenRenderbuffers(1,&rbo); glBindRenderbuffer(GL_RENDERBUFFER,rbo); \
        glRenderbufferStorage(GL_RENDERBUFFER,GL_RGBA8,32,32); \
        glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_RENDERBUFFER,rbo); \
        GLenum fboSt=glCheckFramebufferStatus(GL_FRAMEBUFFER); \
        glBindTexture(GL_TEXTURE_2D,t_); \
        GLuint prog=glCreateProgram(); \
        const char *vs="#version 100\nattribute vec2 p; varying vec2 uv;\nvoid main(){uv=p*0.5+0.5;gl_Position=vec4(p,0,1);}"; \
        const char *fs1="#version 100\nprecision mediump float; varying vec2 uv;\nuniform sampler2D s;\nvoid main(){gl_FragColor=vec4(texture2D(s,uv).rgb,1.0);}"; \
        GLuint v=glCreateShader(GL_VERTEX_SHADER); glShaderSource(v,1,&vs,0); glCompileShader(v); \
        { GLint ok; glGetShaderiv(v,GL_COMPILE_STATUS,&ok); if(!ok){char lb[512];glGetShaderInfoLog(v,512,0,lb);printf("  rb %s VSFAIL %s\n",sel[n]->name,lb);} } \
        GLuint f=glCreateShader(GL_FRAGMENT_SHADER); glShaderSource(f,1,&fs1,0); glCompileShader(f); \
        { GLint ok; glGetShaderiv(f,GL_COMPILE_STATUS,&ok); if(!ok){char lb[512];glGetShaderInfoLog(f,512,0,lb);printf("  rb %s FSFAIL %s\n",sel[n]->name,lb);} } \
        glAttachShader(prog,v); glAttachShader(prog,f); glLinkProgram(prog); \
        { GLint ok; glGetProgramiv(prog,GL_LINK_STATUS,&ok); if(!ok){char lb[512];glGetProgramInfoLog(prog,512,0,lb);printf("  rb %s LNKFAIL %s\n",sel[n]->name,lb);} } \
        glUseProgram(prog); \
        static float quad[4][2]={{-1,-1},{1,-1},{-1,1},{1,1}}; \
        int a=glGetAttribLocation(prog,"p"); glEnableVertexAttribArray(a); glVertexAttribPointer(a,2,GL_FLOAT,0,0,quad); \
        glViewport(0,0,32,32); int u=glGetUniformLocation(prog,"s"); glUniform1i(u,0); \
        glDrawArrays(GL_TRIANGLE_STRIP,0,4); \
        GLenum derr=glGetError(); \
        glReadPixels(0,0,32,32,GL_RGBA,GL_UNSIGNED_BYTE,d_); \
        long r=0,g=0,b=0; for(int i=0;i<32*32;i++){r+=d_[i*4];g+=d_[i*4+1];b+=d_[i*4+2];} \
        glDeleteProgram(prog); glDeleteShader(v); glDeleteShader(f); \
        glBindFramebuffer(GL_FRAMEBUFFER,0); glDeleteFramebuffers(1,&fbo); glDeleteRenderbuffers(1,&rbo); \
        printf("  rb %-12s fbo=0x%04x derr=0x%04x mean=(%3ld,%3ld,%3ld)\n", sel[n]->name, fboSt, derr, r/1024, g/1024, b/1024); free(d_); 0; })
    printf("=== per-object readback means ===\n");
    for(int i=0;i<ns;i++) RB(i);

    // ---- 2) companion alpha textures (unit 1), like the client's _alpha ----
    GLuint alphaTex[12];
    for(int i=0;i<ns;i++){
        glGenTextures(1,&alphaTex[i]); glBindTexture(GL_TEXTURE_2D, alphaTex[i]);
        glCompressedTexImage2D(GL_TEXTURE_2D, 0, GL_ETC1_RGB8_OES,
            sel[i]->w, sel[i]->h, 0, etc1_block_bytes(sel[i]->w, sel[i]->h), pfile+sel[i]->off);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    }
    // white 1x1 fallback
    GLuint white; glGenTextures(1,&white); glBindTexture(GL_TEXTURE_2D, white);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,1,1,0,GL_RGBA,GL_UNSIGNED_BYTE,(unsigned char[]){255,255,255,255});

    // ---- 3) the 13-buffer draw: one quad per buffer, tiled across 256x256,
    //        each buffer binds its own tex0 (color) + tex1 (alpha companion).
    //        Shader = the client's isETC1 solid shape:
    //        texColor = texture2D(uTexture, uv);
    //        texColor.a *= texture2D(uAlphaTexture, uv).r;
    //        gl_FragColor = vec4(texColor.rgb * vColor, texColor.a);
    printf("=== 13-buffer client-replica draw ===\n");
    const char *vs2="#version 100\nattribute vec2 p; attribute vec2 tu; varying vec2 uv;\nvoid main(){uv=tu;gl_Position=vec4(p,0,1);}";
    const char *fs2="#version 100\nprecision mediump float;\nvarying vec2 uv;uniform sampler2D uTexture;uniform sampler2D uAlphaTexture;\nvoid main(){vec4 c=texture2D(uTexture,uv);c.a*=texture2D(uAlphaTexture,uv).r;gl_FragColor=vec4(c.rgb,c.a);}";
    const char *fs2n="#version 100\nprecision mediump float;\nvarying vec2 uv;uniform sampler2D uTexture;\nvoid main(){gl_FragColor=vec4(texture2D(uTexture,uv).rgb,1.0);}";
    GLuint prog=glCreateProgram();
    {
        GLuint v=glCreateShader(GL_VERTEX_SHADER); glShaderSource(v,1,&vs2,0); glCompileShader(v);
        {
            const char *fsSrc = noAlpha ? fs2n : fs2;
            GLuint f=glCreateShader(GL_FRAGMENT_SHADER); glShaderSource(f,1,&fsSrc,0); glCompileShader(f);
            char logbuf[1024]; glGetShaderInfoLog(f,1024,0,logbuf);
            if(strstr(logbuf,"ERROR")){ printf("FS COMPILE FAIL: %s\n", logbuf); return 3; }
            glAttachShader(prog,v); glAttachShader(prog,f); glLinkProgram(prog);
            char llog[1024]; glGetProgramInfoLog(prog,1024,0,llog);
            if(strstr(llog,"ERROR")){ printf("LINK FAIL: %s\n", llog); return 3; }
        }
    }
    glUseProgram(prog);
    int ap=glGetAttribLocation(prog,"p"), au=glGetAttribLocation(prog,"tu");
    int ut=glGetUniformLocation(prog,"uTexture"), ua=glGetUniformLocation(prog,"uAlphaTexture");
    glUniform1i(ut,0); if(!noAlpha) glUniform1i(ua,1);

    // target FBO 256x256
    GLuint fbo, rbo;
    glGenFramebuffers(1,&fbo); glBindFramebuffer(GL_FRAMEBUFFER,fbo);
    glGenRenderbuffers(1,&rbo); glBindRenderbuffer(GL_RENDERBUFFER,rbo);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, 256, 256);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, rbo);
    glViewport(0,0,256,256);
    glClearColor(0,0,0,1); glClear(GL_COLOR_BUFFER_BIT);

    // 12 columns? no: tile 12 quads in a 6x2 grid across 256x256.
    // quad i occupies x=[i%6*42.6, +42.6], y row 0/1 (128 px tall)
    for(int i=0;i<ns;i++){
        glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, tex[i]);
        if(!noAlpha){ glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, alphaTex[i]); }
        float x0=(i%6)*256.0f/6, x1=x0+256.0f/6;
        float y0=(i/6)*128.0f, y1=y0+128.0f;
        static float quadP[4][2], quadU[4][2];
        quadP[0][0]=x0;quadP[0][1]=y0; quadP[1][0]=x1;quadP[1][1]=y0;
        quadP[2][0]=x0;quadP[2][1]=y1; quadP[3][0]=x1;quadP[3][1]=y1;
        quadU[0][0]=0;quadU[0][1]=0; quadU[1][0]=1;quadU[1][1]=0;
        quadU[2][0]=0;quadU[2][1]=1; quadU[3][0]=1;quadU[3][1]=1;
        // map to clip: x*2/256-1, y*2/256-1
        float cp[4][2];
        for(int k=0;k<4;k++){ cp[k][0]=quadP[k][0]*2/256-1; cp[k][1]=quadP[k][1]*2/256-1; }
        glEnableVertexAttribArray(ap); glVertexAttribPointer(ap,2,GL_FLOAT,0,0,cp);
        glEnableVertexAttribArray(au); glVertexAttribPointer(au,2,GL_FLOAT,0,0,quadU);
        glDrawArrays(GL_TRIANGLE_STRIP,0,4);
    }
    // read the whole 256x256 back
    uint8_t *img=malloc(256*256*4);
    glReadPixels(0,0,256,256,GL_RGBA,GL_UNSIGNED_BYTE,img);

    // per-tile mean + pairwise tile distance: if all tiles ~equal -> SMREER
    printf("=== per-tile mean (12 tiles, 6x2 grid, left-to-right top-to-bottom) ===\n");
    long tm[12][3];
    for(int i=0;i<ns;i++){
        int col=i%6, row=i/6;
        long r=0,g=0,b=0,cnt=0;
        int x0=col*256/6+4, x1=(col+1)*256/6-4;
        int y0=(1-row)*128+4, y1=(1-row)*128+128-4;
        for(int y=y0;y<y1;y++)for(int x=x0;x<x1;x++){
            // note: glReadPixels origin is bottom-left
            int ry=255-y;
            const uint8_t *p=img+(ry*256+x)*4;
            r+=p[0]; g+=p[1]; b+=p[2]; cnt++;
        }
        tm[i][0]=r/cnt; tm[i][1]=g/cnt; tm[i][2]=b/cnt;
        printf("  tile %2d (%-12s): (%3ld,%3ld,%3ld)\n", i, sel[i]->name, tm[i][0], tm[i][1], tm[i][2]);
    }
    int allEqual=1;
    for(int i=1;i<ns;i++){
        int dr=abs((int)tm[i][0]-tm[0][0]), dg=abs((int)tm[i][1]-tm[0][1]), db=abs((int)tm[i][2]-tm[0][2]);
        if(dr>20||dg>20||db>20) allEqual=0;
    }
    printf("=== VERDICT: tiles %s (all within 20 of tile 0) ===\n", allEqual?"ALL EQUAL -> SMREER REPRODUCED":"distinct -> GL state is fine");

    // dump the image as PPM for the eye
    {
        FILE *ppm=fopen("b1051_min13_probe.ppm","wb");
        fprintf(ppm,"P6\n256 256\n255\n");
        // flip to top-down
        for(int y=255;y>=0;y--) fwrite(img+y*256*4,1,256*3,ppm);
        fclose(ppm);
        printf("image -> b1051_min13_probe.ppm\n");
    }
    return allEqual?0:1;
}

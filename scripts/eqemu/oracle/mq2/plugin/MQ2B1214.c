/*
 * MQ2B1214 -- B1214 (#533) spike plugin: read the Titanium client's particle
 * emitter registry and camera state from inside eqgame.exe.
 *
 * Plain C (no MQ2 headers, no C++ ABI): MQ2Main.dll's API is extern "C"
 * (MQ2Main.h:69 `#define EQLIB_API extern "C" __declspec(dllexport)` in the
 * 20051101 source), so AddCommand / WriteChatColor are resolved by name with
 * GetProcAddress and called cdecl. Built with mingw-w64 (i686); the loader
 * refuses a plugin whose PE TimeDateStamp is older than MQ2Main.dll's
 * (MQ2PluginHandler.cpp checkme), so link with --insert-timestamp.
 *
 * Every address below is from OUR binaries (review/B1141/ghidra/out):
 *   eqgame.exe  0x99e36c  particle engine pointer; 0x495f02 loads it and
 *                         0x495f39 calls engine vtable +0x14 (spell family).
 *   DX9 0x1005e1d0        vtable +0x14: 0x1005e0a0(engine + 0x14, ...) -> the
 *                         SPELL family container is engine + 0x14.
 *   DX9 0x1005e2c0        0x1005e0a0(engine + 0x38, ...) -> ENVIRONMENT family.
 *   DX9 0x1005e0a0        container + 0x10 = def count, + 0x14 = CEmitterDef**;
 *                         def + 4 = enabled byte; CreateEmitter this = def.
 *   DX9 0x1005a430        CreateEmitter: def + 0x14 = EDD record, links the new
 *                         CEmitter at def + 0x10 (head), next = emitter + 0x84.
 *   DX9 0x1005a0e0        CEmitter ctor (0x8c bytes): +0x08 key, +0x0c ring
 *                         capacity, +0x20 scale, +0x24 duration, +0x38 pos[3],
 *                         +0x68 actor, +0x6c attach point, +0x70 track,
 *                         +0x74 ring base, +0x78 ring end, +0x7c/+0x80 ring
 *                         read/write (0x80-byte particles), +0x84 next.
 *   eqgame 0x495eee       key = (spawn +0x258 SpawnID) << 16 | spell id (measured: 0x01030116 = spawn 259, spell 278).
 *   eqgame 0x925508/0x92550c  user camera 1/2 objects (SaveCamerasToINI
 *                         0x4ae... reads +0x34 Distance, +0x28 Heading,
 *                         +0x30 Pitch, +0x40 Zoom).
 *
 * Commands:
 *   /emdump [all]   every live CEmitter of the spell family (all: + env family)
 *   /camdump        user camera 1/2 fields
 *   /peek <hex> [n] n dwords (default 4) at an address
 *   /poke <hex> <float>  write a float (camera distance A/B)
 * Output goes to the MQ2 chat (and so to MQ2Telnet) and to Logs\b1214.log.
 */
#include <windows.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>

typedef void (__cdecl *fCmd)(void *pChar, char *szLine);
typedef void (__cdecl *fAddCommand)(char *, fCmd, BOOL, BOOL, BOOL);
typedef BOOL (__cdecl *fRemoveCommand)(char *);
typedef void (__cdecl *fWriteChatColor)(char *, DWORD, DWORD);

static fAddCommand pAddCommand;
static fRemoveCommand pRemoveCommand;
static fWriteChatColor pWriteChatColor;
static FILE *gLog;

#define ENGINE_PTR   0x0099e36cu
#define CAM1_PTR     0x00925508u
#define CAM2_PTR     0x0092550cu
#define CHARSPAWN    0x00905cf8u   /* eqgame.h pinstCharSpawn */
#define TARGET_PTR   0x00905d04u   /* eqgame.h pinstTarget */

static void out(const char *fmt, ...)
{
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    _vsnprintf(buf, sizeof buf - 1, fmt, ap);
    va_end(ap);
    buf[sizeof buf - 1] = 0;
    if (pWriteChatColor) pWriteChatColor(buf, 0xFF + 18, 0);
    if (gLog) {
        SYSTEMTIME t;
        GetSystemTime(&t);
        fprintf(gLog, "%04d-%02d-%02dT%02d:%02d:%02d.%03dZ %s\n", t.wYear, t.wMonth, t.wDay,
                t.wHour, t.wMinute, t.wSecond, t.wMilliseconds, buf);
        fflush(gLog);
    }
}

static int ok(DWORD a, DWORD n) { return a >= 0x10000 && !IsBadReadPtr((void *)a, n); }
static DWORD rd(DWORD a) { return ok(a, 4) ? *(DWORD *)a : 0; }
static float rf(DWORD a) { return ok(a, 4) ? *(float *)a : -9999.0f; }

static DWORD my_spawn_id(void)
{
    DWORD me = rd(CHARSPAWN);
    return me ? rd(me + 0x258) : 0;
}

static DWORD target_spawn_id(void)
{
    DWORD t = rd(TARGET_PTR);
    return t ? rd(t + 0x258) : 0;
}

static int dump_family(DWORD engine, DWORD off, const char *name, DWORD meid, DWORD tid)
{
    DWORD cont = engine + off;
    int count = (int)rd(cont + 0x10);
    DWORD defs = rd(cont + 0x14);
    int i, live = 0;
    out("B1214E family=%s container=0x%08lx defs=%d array=0x%08lx", name, cont, count, defs);
    if (count < 0 || count > 100000 || !ok(defs, 4 * (DWORD)(count > 0 ? count : 1))) return 0;
    for (i = 0; i < count; i++) {
        DWORD def = rd(defs + 4 * i), e, guard = 0;
        if (!def) continue;
        e = rd(def + 0x10);
        while (e && ok(e, 0x8c) && guard++ < 4096) {
            DWORD key = rd(e + 0x08), sid = key >> 16;
            DWORD rb = rd(e + 0x74), re = rd(e + 0x78), rr = rd(e + 0x7c), rw = rd(e + 0x80);
            int cap = (int)rd(e + 0x0c);
            int used = 0;
            if (re > rb) {
                long d = (long)rw - (long)rr;
                if (d < 0) d += (long)(re - rb) + 0x80;
                used = (int)(d / 0x80);
            }
            out("B1214E %s def=%d emitter=0x%08lx key=0x%08lx spawn=%lu%s spell=%lu cap=%d live=%d "
                "scale=%.3f dur=%.1f pos=(%.2f,%.2f,%.2f) actor=0x%08lx point=0x%08lx track=0x%08lx "
                "valid=%u edd=0x%08lx",
                name, i, e, key, sid, sid && sid == meid ? "(ME)" : (sid && sid == tid ? "(TARGET)" : ""),
                key & 0xffff, cap, used, rf(e + 0x20), rf(e + 0x24), rf(e + 0x38), rf(e + 0x3c), rf(e + 0x40),
                rd(e + 0x68), rd(e + 0x6c), rd(e + 0x70), (unsigned)(ok(e + 4, 1) ? *(BYTE *)(e + 4) : 0),
                rd(def + 0x14));
            live++;
            e = rd(e + 0x84);
        }
    }
    out("B1214E family=%s live_emitters=%d", name, live);
    return live;
}

static void __cdecl cmd_emdump(void *pChar, char *line)
{
    DWORD engine = rd(ENGINE_PTR);
    DWORD meid = my_spawn_id(), tid = target_spawn_id();
    (void)pChar;
    out("B1214E begin engine=0x%08lx vtbl=0x%08lx me_spawn=%lu target_spawn=%lu", engine, rd(engine), meid, tid);
    if (!engine) return;
    dump_family(engine, 0x14, "spell", meid, tid);
    if (line && strstr(line, "all")) dump_family(engine, 0x38, "env", meid, tid);
    out("B1214E end");
}

static void cam(const char *n, DWORD pp)
{
    DWORD c = rd(pp);
    out("B1214C %s obj=0x%08lx vtbl=0x%08lx distance=%.3f heading=%.3f pitch=%.3f zoom=%.3f [+0x2c]=%.3f [+0x38]=%.3f [+0x3c]=%.3f",
        n, c, rd(c), rf(c + 0x34), rf(c + 0x28), rf(c + 0x30), rf(c + 0x40), rf(c + 0x2c), rf(c + 0x38), rf(c + 0x3c));
}

static void __cdecl cmd_camdump(void *pChar, char *line)
{
    (void)pChar; (void)line;
    cam("user1", CAM1_PTR);
    cam("user2", CAM2_PTR);
}

static void __cdecl cmd_peek(void *pChar, char *line)
{
    DWORD a, n, i;
    char *p = line;
    (void)pChar;
    a = strtoul(p, &p, 16);
    n = strtoul(p, &p, 0);
    if (!n || n > 64) n = 4;
    for (i = 0; i < n; i++)
        out("B1214P 0x%08lx: 0x%08lx (%.4f)%s", a + 4 * i, rd(a + 4 * i), rf(a + 4 * i), ok(a + 4 * i, 4) ? "" : " UNREADABLE");
}

static void __cdecl cmd_poke(void *pChar, char *line)
{
    char *p = line;
    DWORD a = strtoul(p, &p, 16);
    float v = (float)atof(p);
    DWORD old;
    (void)pChar;
    if (!ok(a, 4)) { out("B1214P poke 0x%08lx UNREADABLE", a); return; }
    if (VirtualProtect((void *)a, 4, PAGE_READWRITE, &old)) {
        *(float *)a = v;
        VirtualProtect((void *)a, 4, old, &old);
        out("B1214P poke 0x%08lx = %.4f", a, v);
    }
}

__declspec(dllexport) void __cdecl InitializePlugin(void)
{
    HMODULE m = GetModuleHandleA("MQ2Main.dll");
    char path[MAX_PATH];
    pAddCommand = (fAddCommand)GetProcAddress(m, "AddCommand");
    pRemoveCommand = (fRemoveCommand)GetProcAddress(m, "RemoveCommand");
    pWriteChatColor = (fWriteChatColor)GetProcAddress(m, "WriteChatColor");
    GetModuleFileNameA(m, path, sizeof path);
    { char *s = strrchr(path, '\\'); if (s) strcpy(s + 1, "Logs\\b1214.log"); }
    gLog = fopen(path, "a");
    if (pAddCommand) {
        pAddCommand("/emdump", cmd_emdump, 0, 1, 1);
        pAddCommand("/camdump", cmd_camdump, 0, 1, 1);
        pAddCommand("/peek", cmd_peek, 0, 1, 0);
        pAddCommand("/poke", cmd_poke, 0, 1, 0);
    }
    out("B1214 plugin loaded: AddCommand=%p WriteChatColor=%p", (void *)pAddCommand, (void *)pWriteChatColor);
}

__declspec(dllexport) void __cdecl ShutdownPlugin(void)
{
    if (pRemoveCommand) {
        pRemoveCommand("/emdump");
        pRemoveCommand("/camdump");
        pRemoveCommand("/peek");
        pRemoveCommand("/poke");
    }
    if (gLog) fclose(gLog);
    gLog = NULL;
}

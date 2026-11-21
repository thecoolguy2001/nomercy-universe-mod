/*
 * Headless output for angrylion-rdp-plus, used as a scripted debugger for
 * reverse engineering on a machine without a display.
 *
 * Env:
 *   NMDBG_SCRIPT  script file (see below)
 *   NMDBG_OUT     output directory for screenshots / RAM dumps
 *
 * Script lines: "<frame> <command> [args]"   (frame = VI count from boot)
 *   press BTN[+BTN...] [frames]   hold buttons (A B Z START L R DU DD DL DR CU CD CL CR)
 *   stick X Y [frames]            hold analog stick (-80..80)
 *   shot NAME                     save NAME.png of the current frame
 *   dump NAME                     save NAME.bin = full RDRAM (big-endian, as the N64 sees it)
 *   poke ADDR VALUE               write 32-bit VALUE at RDRAM ADDR (hex, 0x80xxxxxx ok)
 *   pokeb ADDR VALUE              write 8-bit value
 *   log ADDR LEN                  print LEN bytes at ADDR to stdout
 *   savestate PATH / loadstate PATH   mupen64plus savestates (taken at next VI)
 *   endtrig                       when the match clock (0x8015AB18 min, 0x8015AB1C sec)
 *                                 stays frozen 150 frames: write pre.bin (>=180 frames
 *                                 before the freeze) and post.bin, shoot end_NN.png
 *                                 every 30 frames for 600 frames, then quit
 *   bpr ADDR LEN / bpw ADDR LEN   memory read / write breakpoint (debugger core)
 *   bpx ADDR                      execution breakpoint (debugger core)
 *   bplimit N                     stop logging (and remove breakpoints) after N hits
 *   quit                          exit the emulator
 *   every N shot PREFIX           (frame ignored) screenshot every N frames
 */
#include "output/vdac.h"
#include "output/screen.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>
#include <dlfcn.h>

typedef int (*core_cmd_fn)(int, int, void *);
/* mupen64plus debugger API (core built with DEBUGGER=1, Core[EnableDebugger]) */
typedef struct { uint32_t address, endaddr; unsigned int flags; } bkp_t;
static int (*dbg_set_callbacks)(void (*)(void), void (*)(unsigned int), void (*)(void));
static int (*dbg_set_runstate)(int);
static int (*dbg_step)(void);
static int (*dbg_bkp_cmd)(int, unsigned int, bkp_t *);
static void *(*dbg_cpu_ptr)(int);
static uint32_t bp_hits, bp_limit = 200;
static core_cmd_fn core_cmd;
#define M64CMD_STATE_LOAD 6
#define M64CMD_STATE_SAVE 7

#define MAX_CMDS 4096

typedef struct {
    uint32_t frame;
    char cmd[16];
    char a[64];
    char b[64];
    char c[64];
} Cmd;

static Cmd cmds[MAX_CMDS];
static int ncmds, next_cmd;
static uint32_t frame;
static uint8_t *rdram;
static uint8_t *dmem;
static uint32_t **dp_reg;
static uint32_t texlog_until;
static uint32_t rdram_size;
static const char *outdir = ".";
static uint32_t every_n;
static char every_prefix[64];

static uint8_t *pix;          /* last frame, RGB */
static uint32_t pix_w, pix_h;

static uint32_t hold_buttons, hold_until;

/* end-of-match trigger */
#define RING 6
static int endtrig_on, endtrig_fired, endtrig_keep;
static uint32_t clk_last, clk_change_frame, end_frame;
static uint8_t *ring[RING];
static uint32_t ring_frame[RING];
static int ring_pos;
static int8_t stick_x, stick_y;
static uint32_t stick_until;

/* ---- exported for the input plugin ---- */
uint32_t nmdbg_buttons(void) { return frame < hold_until ? hold_buttons : 0; }
int nmdbg_stick_x(void) { return frame < stick_until ? stick_x : 0; }
int nmdbg_stick_y(void) { return frame < stick_until ? stick_y : 0; }
uint32_t nmdbg_frame(void) { return frame; }

static uint32_t parse_buttons(const char *s)
{
    /* mupen64plus BUTTONS bit layout */
    static const struct { const char *n; uint32_t v; } B[] = {
        {"DR", 0x0001}, {"DL", 0x0002}, {"DD", 0x0004}, {"DU", 0x0008},
        {"START", 0x0010}, {"Z", 0x0020}, {"B", 0x0040}, {"A", 0x0080},
        {"CR", 0x0100}, {"CL", 0x0200}, {"CD", 0x0400}, {"CU", 0x0800},
        {"R", 0x1000}, {"L", 0x2000},
    };
    uint32_t v = 0;
    char buf[64], *tok, *save;
    snprintf(buf, sizeof buf, "%s", s);
    for (tok = strtok_r(buf, "+", &save); tok; tok = strtok_r(NULL, "+", &save)) {
        size_t i;
        for (i = 0; i < sizeof B / sizeof B[0]; i++)
            if (!strcmp(tok, B[i].n)) v |= B[i].v;
    }
    return v;
}

static uint32_t ram_off(const char *s)
{
    return (uint32_t)strtoul(s, NULL, 16) & 0x00FFFFFF;
}

/* mupen64plus keeps RDRAM as host-endian 32-bit words */
static uint8_t rd8(uint32_t a) { return rdram[a ^ 3]; }
static void wr8(uint32_t a, uint8_t v) { rdram[a ^ 3] = v; }

static void write_png(const char *path);

static uint32_t rd32(uint32_t a) { return (uint32_t)rd8(a) << 24 | rd8(a + 1) << 16 | rd8(a + 2) << 8 | rd8(a + 3); }

static void save_ram(const char *name, const uint8_t *src)
{
    char path[512];
    FILE *f;
    uint32_t i;
    snprintf(path, sizeof path, "%s/%s.bin", outdir, name);
    if (!(f = fopen(path, "wb"))) return;
    for (i = 0; i < rdram_size; i++) fputc(src[i ^ 3], f);
    fclose(f);
}

static void endtrig_tick(void)
{
    uint32_t clk = rd32(0x15AB18) * 60 + rd32(0x15AB1C);
    char path[512];
    int k;
    if (frame % 60 == 0) {          /* rolling snapshots */
        if (!ring[ring_pos]) ring[ring_pos] = malloc(rdram_size);
        memcpy(ring[ring_pos], rdram, rdram_size);
        ring_frame[ring_pos] = frame;
        ring_pos = (ring_pos + 1) % RING;
    }
    if (endtrig_fired && endtrig_keep) return;
    if (endtrig_fired) {
        if ((frame - end_frame) % 30 == 0) {
            snprintf(path, sizeof path, "%s/end_%02u.png", outdir, (frame - end_frame) / 30);
            write_png(path);
        }
        if (frame - end_frame >= 600) {
            uint32_t a2;
            printf("[nmdbg] frame %u mailbox 80408000:", frame);
            for (a2 = 0x408000; a2 < 0x408020; a2++) printf("%s%02X", (a2 & 3) ? "" : " ", rd8(a2));
            printf("\n[nmdbg] frame %u endtrig done\n", frame);
            fflush(stdout);
            _Exit(0);
        }
        return;
    }
    if (clk != clk_last) {
        clk_last = clk;
        clk_change_frame = frame;
        return;
    }
    if (clk >= 5 && frame - clk_change_frame >= 150) {
        int best = -1;
        for (k = 0; k < RING; k++)  /* newest snapshot taken >=180 frames before the freeze */
            if (ring[k] && ring_frame[k] + 180 <= clk_change_frame &&
                (best < 0 || ring_frame[k] > ring_frame[best]))
                best = k;
        endtrig_fired = 1;
        end_frame = frame;
        if (endtrig_keep) {            /* rebase "after the bell" commands (frame >= 90000) */
            int k;
            for (k = next_cmd; k < ncmds; k++)
                if (cmds[k].frame >= 90000) cmds[k].frame = end_frame + (cmds[k].frame - 90000);
        }
        printf("[nmdbg] frame %u MATCH END: clock %u:%02u froze at frame %u, pre snapshot frame %u\n",
               frame, clk / 60, clk % 60, clk_change_frame, best >= 0 ? ring_frame[best] : 0);
        if (best >= 0) save_ram("pre", ring[best]);
        save_ram("post", rdram);
    }
}

static void write_png(const char *path)
{
    FILE *f;
    uint32_t y, crc;
    uLongf zlen;
    uint8_t *raw, *z, hdr[13];
    size_t rawlen = (size_t)pix_h * (pix_w * 3 + 1);
    if (!pix) return;
    raw = malloc(rawlen);
    for (y = 0; y < pix_h; y++) {
        raw[y * (pix_w * 3 + 1)] = 0;
        memcpy(raw + y * (pix_w * 3 + 1) + 1, pix + y * pix_w * 3, pix_w * 3);
    }
    zlen = compressBound(rawlen);
    z = malloc(zlen);
    compress2(z, &zlen, raw, rawlen, 6);
    f = fopen(path, "wb");
    if (f) {
#define BE32(v) do { uint8_t t[4] = {(v) >> 24, (v) >> 16, (v) >> 8, (v)}; fwrite(t, 1, 4, f); } while (0)
#define CHUNK(type, data, len) do { BE32((uint32_t)(len)); fwrite(type, 1, 4, f); \
        if (len) fwrite(data, 1, len, f); crc = crc32(0, (const Bytef *)type, 4); \
        crc = crc32(crc, data, len); BE32(crc); } while (0)
        fwrite("\x89PNG\r\n\x1a\n", 1, 8, f);
        hdr[0] = pix_w >> 24; hdr[1] = pix_w >> 16; hdr[2] = pix_w >> 8; hdr[3] = pix_w;
        hdr[4] = pix_h >> 24; hdr[5] = pix_h >> 16; hdr[6] = pix_h >> 8; hdr[7] = pix_h;
        hdr[8] = 8; hdr[9] = 2; hdr[10] = 0; hdr[11] = 0; hdr[12] = 0;
        CHUNK("IHDR", hdr, 13);
        CHUNK("IDAT", z, zlen);
        CHUNK("IEND", (const uint8_t *)"", 0);
        fclose(f);
    }
    free(raw);
    free(z);
}

static void run_cmd(const Cmd *c)
{
    char path[512];
    if (!strcmp(c->cmd, "press")) {
        hold_buttons = parse_buttons(c->a);
        hold_until = frame + (c->b[0] ? (uint32_t)atoi(c->b) : 3);
    } else if (!strcmp(c->cmd, "stick")) {
        stick_x = (int8_t)atoi(c->a);
        stick_y = (int8_t)atoi(c->b);
        stick_until = frame + (c->c[0] ? (uint32_t)atoi(c->c) : 3);
    } else if (!strcmp(c->cmd, "shot")) {
        snprintf(path, sizeof path, "%s/%s.png", outdir, c->a);
        write_png(path);
        printf("[nmdbg] frame %u shot %s\n", frame, path);
    } else if (!strcmp(c->cmd, "dump")) {
        FILE *f;
        uint32_t i;
        snprintf(path, sizeof path, "%s/%s.bin", outdir, c->a);
        f = fopen(path, "wb");
        if (f) {
            for (i = 0; i < rdram_size; i++) fputc(rd8(i), f);
            fclose(f);
        }
        printf("[nmdbg] frame %u dump %s\n", frame, path);
    } else if (!strcmp(c->cmd, "poke")) {
        uint32_t a = ram_off(c->a), v = (uint32_t)strtoul(c->b, NULL, 16);
        wr8(a, v >> 24); wr8(a + 1, v >> 16); wr8(a + 2, v >> 8); wr8(a + 3, v);
    } else if (!strcmp(c->cmd, "pokeb")) {
        wr8(ram_off(c->a), (uint8_t)strtoul(c->b, NULL, 16));
    } else if (!strcmp(c->cmd, "log")) {
        uint32_t a = ram_off(c->a), n = (uint32_t)strtoul(c->b, NULL, 0), i;
        printf("[nmdbg] frame %u %08X:", frame, a | 0x80000000);
        for (i = 0; i < n; i++) printf("%s%02X", i % 4 ? "" : " ", rd8(a + i));
        printf("\n");
    } else if (!strcmp(c->cmd, "savestate") || !strcmp(c->cmd, "loadstate")) {
        if (!core_cmd) {
            void *h = dlopen(getenv("NMDBG_CORE_SO"), RTLD_NOW | RTLD_NOLOAD);
            if (h) core_cmd = (core_cmd_fn)dlsym(h, "CoreDoCommand");
        }
        if (core_cmd) {
            /* save: type 1 = mupen64plus format */
            int rc = core_cmd(c->cmd[0] == 's' ? M64CMD_STATE_SAVE : M64CMD_STATE_LOAD,
                              c->cmd[0] == 's' ? 1 : 0, (void *)c->a);
            printf("[nmdbg] frame %u %s %s rc=%d\n", frame, c->cmd, c->a, rc);
        } else {
            printf("[nmdbg] no core handle for %s\n", c->cmd);
        }
    } else if (!strcmp(c->cmd, "bpr") || !strcmp(c->cmd, "bpw") || !strcmp(c->cmd, "bpx")) {
        bkp_t b;
        uint32_t a = (uint32_t)strtoul(c->a, NULL, 16);
        uint32_t n = c->b[0] ? (uint32_t)strtoul(c->b, NULL, 0) : 4;
        if (!dbg_bkp_cmd) { printf("[nmdbg] no debugger core\n"); return; }
        if (c->cmd[2] != 'x') {           /* memory bps match physical addresses */
            uint32_t end = (a & 0x1FFFFFFF) + n - 1;
            a = (a & 0x1FFFFFFF) & ~3u;
            n = end - a + 1;
        }
        b.address = a;
        b.endaddr = c->cmd[2] == 'x' ? a : a + n - 1;
        b.flags = 0x01 | (c->cmd[2] == 'r' ? 0x02 : c->cmd[2] == 'w' ? 0x04 : 0x08);
        printf("[nmdbg] frame %u %s %08X-%08X -> %d\n", frame, c->cmd, b.address, b.endaddr,
               dbg_bkp_cmd(2 /* ADD_STRUCT */, 0, &b));
    } else if (!strcmp(c->cmd, "bplimit")) {
        bp_limit = (uint32_t)atoi(c->a);
    } else if (!strcmp(c->cmd, "logstr")) {   /* logstr PTRADDR: print the string it points to */
        uint32_t pa = ram_off(c->a), p = rd32(pa) & 0xFFFFFF, i;
        printf("[nmdbg] frame %u *%s = %08X \"", frame, c->a, rd32(pa));
        for (i = 0; i < 24 && p + i < rdram_size; i++) {
            uint8_t ch = rd8(p + i);
            if (!ch) break;
            putchar(ch >= 32 && ch < 127 ? ch : '?');
        }
        printf("\"\n");
    } else if (!strcmp(c->cmd, "texlog")) {
        texlog_until = frame + (uint32_t)atoi(c->a);
    } else if (!strcmp(c->cmd, "endtrig")) {
        endtrig_on = 1;
        endtrig_keep = !strcmp(c->a, "keep");   /* keep running; frames >= 90000 are relative to the bell */
        clk_change_frame = frame;
    } else if (!strcmp(c->cmd, "quit")) {
        printf("[nmdbg] frame %u quit\n", frame);
        fflush(stdout);
        _Exit(0);
    }
}

static void load_script(void)
{
    const char *path = getenv("NMDBG_SCRIPT");
    char line[256];
    FILE *f;
    if (getenv("NMDBG_OUT")) outdir = getenv("NMDBG_OUT");
    if (!path || !(f = fopen(path, "r"))) return;
    while (fgets(line, sizeof line, f) && ncmds < MAX_CMDS) {
        Cmd *c = &cmds[ncmds];
        char first[32];
        memset(c, 0, sizeof *c);
        if (line[0] == '#' || sscanf(line, "%31s", first) != 1) continue;
        if (!strcmp(first, "every")) {
            char what[16];
            if (sscanf(line, "every %u %15s %63s", &every_n, what, every_prefix) < 3) every_n = 0;
            continue;
        }
        if (sscanf(line, "%u %15s %63s %63s %63s", &c->frame, c->cmd, c->a, c->b, c->c) >= 2)
            ncmds++;
    }
    fclose(f);
    printf("[nmdbg] %d script commands\n", ncmds);
}

static int dbg_started;
static void dbg_ui_init(void) {}
static void dbg_ui_vi(void) {}
static void dbg_ui_update(unsigned int pc)
{
    if (dbg_started) {
        int64_t *r = dbg_cpu_ptr ? (int64_t *)dbg_cpu_ptr(2 /* M64P_CPU_REG_REG */) : NULL;
        static uint32_t last_pc, last_a0, last_ra;
        if (r && getenv("NMDBG_DEDUPE") && pc == last_pc && (uint32_t)r[4] == last_a0 &&
            (uint32_t)r[31] == last_ra) {
            dbg_set_runstate(2);
            dbg_step();
            return;
        }
        if (r) { last_pc = pc; last_a0 = (uint32_t)r[4]; last_ra = (uint32_t)r[31]; }
        if (!getenv("NMDBG_EA")) bp_hits++;
        if (bp_hits <= bp_limit && r && !getenv("NMDBG_EA"))
            printf("[bp] frame %u pc=%08X ra=%08X a0=%08X a1=%08X a2=%08X a3=%08X v0=%08X sp=%08X arg4=%08X\n",
                   frame, pc, (uint32_t)r[31], (uint32_t)r[4], (uint32_t)r[5], (uint32_t)r[6],
                   (uint32_t)r[7], (uint32_t)r[2], (uint32_t)r[29],
                   rd32(((uint32_t)r[29] + 0x10) & 0x7FFFFF));
        if (bp_hits <= bp_limit && r && getenv("NMDBG_EA")) {
            /* decode the load/store at pc (and pc-4) and print its effective address */
            int k;
            for (k = 0; k < 2; k++) {
                uint32_t ia = (pc - 4 * k) & 0x7FFFFF, ins = rd32(ia), op = ins >> 26;
                if (op >= 0x20 && op <= 0x2F) {
                    uint32_t base = (ins >> 21) & 31; int16_t off = (int16_t)(ins & 0xFFFF);
                    {
                        static uint32_t seen_pc[1024], seen_ea[1024];
                        static int nseen;
                        uint32_t ea = (uint32_t)r[base] + off, j;
                        int dup = 0;
                        for (j = 0; j < (uint32_t)nseen; j++)
                            if (seen_pc[j] == pc && seen_ea[j] == ea) { dup = 1; break; }
                        if (!dup && nseen < 1024) { seen_pc[nseen] = pc; seen_ea[nseen++] = ea; }
                        if (!dup) printf("[ea] frame %u pc=%08X ea=%08X insn=%08X\n", frame, pc - 4 * k, ea, ins);
                    }
                    break;
                }
            }
        }
        if (bp_hits <= bp_limit && r && getenv("NMDBG_STACKARGS"))
            printf("[bp]   stack 0x10..0x1C: %08X %08X %08X %08X\n",
                   rd32(((uint32_t)r[29] + 0x10) & 0x7FFFFF), rd32(((uint32_t)r[29] + 0x14) & 0x7FFFFF),
                   rd32(((uint32_t)r[29] + 0x18) & 0x7FFFFF), rd32(((uint32_t)r[29] + 0x1C) & 0x7FFFFF));
        if (bp_hits == bp_limit) {
            unsigned int i;
            printf("[bp] limit reached, removing breakpoints\n");
            for (i = 0; i < 64; i++) dbg_bkp_cmd(5 /* REMOVE_IDX */, 0, NULL);
        }
    }
    dbg_started = 1;
    dbg_set_runstate(2 /* RUNNING */);
    dbg_step();
}

static void dbg_attach(void)
{
    void *h = dlopen(getenv("NMDBG_CORE_SO"), RTLD_NOW | RTLD_NOLOAD);
    if (!h) return;
    dbg_set_callbacks = (int (*)(void (*)(void), void (*)(unsigned int), void (*)(void)))dlsym(h, "DebugSetCallbacks");
    dbg_set_runstate = (int (*)(int))dlsym(h, "DebugSetRunState");
    dbg_step = (int (*)(void))dlsym(h, "DebugStep");
    dbg_bkp_cmd = (int (*)(int, unsigned int, bkp_t *))dlsym(h, "DebugBreakpointCommand");
    dbg_cpu_ptr = (void *(*)(int))dlsym(h, "DebugGetCPUDataPtr");
    if (dbg_set_callbacks && dbg_set_runstate && dbg_step) {
        dbg_set_callbacks(dbg_ui_init, dbg_ui_update, dbg_ui_vi);
        printf("[nmdbg] debugger attached\n");
    } else {
        dbg_bkp_cmd = NULL;
    }
}

void vdac_init(struct n64video_config *config)
{
    setvbuf(stdout, NULL, _IOLBF, 0);
    rdram = config->gfx.rdram;
    dmem = config->gfx.dmem;
    dp_reg = config->gfx.dp_reg;
    rdram_size = config->gfx.rdram_size;
    load_script();
    if (getenv("NMDBG_DEBUG")) dbg_attach();
}

void vdac_read(struct n64video_frame_buffer *fb, bool alpha)
{
    (void)alpha;
    fb->width = pix_w;
    fb->height = pix_h;
}

void vdac_write(struct n64video_frame_buffer *fb)
{
    uint32_t x, y;
    if (fb->width != pix_w || fb->height != pix_h) {
        free(pix);
        pix_w = fb->width;
        pix_h = fb->height;
        pix = malloc(pix_w * pix_h * 3);
    }
    for (y = 0; y < pix_h; y++)
        for (x = 0; x < pix_w; x++) {
            const struct n64video_pixel *p = &fb->pixels[y * fb->pitch + x];
            uint8_t *d = pix + (y * pix_w + x) * 3;
            d[0] = p->r; d[1] = p->g; d[2] = p->b;
        }
}

void vdac_sync(bool valid)
{
    (void)valid;
    frame++;
    if (every_n && frame % every_n == 0) {
        char path[512];
        snprintf(path, sizeof path, "%s/%s%06u.png", outdir, every_prefix, frame);
        write_png(path);
    }
    while (next_cmd < ncmds && cmds[next_cmd].frame <= frame)
        run_cmd(&cmds[next_cmd++]);
    if (endtrig_on) endtrig_tick();
}

void vdac_close(void) {}

void screen_toggle_fullscreen(void) {}
int32_t win_width = 640, win_height = 480, win_fullscreen;

/* ---- RDP command logging: wraps angrylion's ProcessRDPList ----
 * Logs Set Texture Image (0x3D), Set Tile (0x35), Load Block (0x33),
 * Load Tile (0x34) and Texture Rectangle (0x24) while texlog is active. */
void angry_ProcessRDPList(void);
__attribute__((visibility("default"))) void ProcessRDPList(void)
{
    if (frame < texlog_until && dp_reg) {
        uint32_t start = *dp_reg[0] & 0xFFFFF8, end = *dp_reg[1] & 0xFFFFF8;
        int xbus = *dp_reg[3] & 1;
        uint32_t a;
        for (a = start; a + 8 <= end; a += 8) {
            uint32_t w0, w1;
            if (xbus) {
                const uint8_t *p = dmem + (a & 0xFF8);
                w0 = (uint32_t)p[3^0] << 24 | p[3^1] << 16 | p[3^2] << 8 | p[3^3];
                w1 = (uint32_t)p[4+(3^0)] << 24 | p[4+(3^1)] << 16 | p[4+(3^2)] << 8 | p[4+(3^3)];
            } else {
                w0 = (uint32_t)rd8(a) << 24 | rd8(a+1) << 16 | rd8(a+2) << 8 | rd8(a+3);
                w1 = (uint32_t)rd8(a+4) << 24 | rd8(a+5) << 16 | rd8(a+6) << 8 | rd8(a+7);
            }
            switch (w0 >> 24 & 0x3F) {
            case 0x3D: printf("[tex] f%u SETTIMG fmt=%u siz=%u width=%u addr=%08X\n", frame,
                              w0 >> 21 & 7, w0 >> 19 & 3, (w0 & 0x3FF) + 1, w1 & 0xFFFFFF); break;
            case 0x35: printf("[tex] f%u SETTILE fmt=%u siz=%u line=%u tmem=%03X tile=%u pal=%u\n", frame,
                              w0 >> 21 & 7, w0 >> 19 & 3, w0 >> 9 & 0x1FF, w0 & 0x1FF, w1 >> 24 & 7, w1 >> 20 & 15); break;
            case 0x33: printf("[tex] f%u LOADBLOCK tile=%u texels=%u\n", frame, w1 >> 24 & 7, (w1 >> 12 & 0xFFF) + 1); break;
            case 0x34: printf("[tex] f%u LOADTILE tile=%u s=%u..%u t=%u..%u\n", frame, w1 >> 24 & 7,
                              (w0 >> 12 & 0xFFF) >> 2, (w1 >> 12 & 0xFFF) >> 2, (w0 & 0xFFF) >> 2, (w1 & 0xFFF) >> 2); break;
            case 0x30: printf("[tex] f%u LOADTLUT tile=%u\n", frame, w1 >> 24 & 7); break;
            case 0x36: printf("[tex] f%u FILLRECT %u,%u - %u,%u\n", frame, (w1 >> 14) & 0x3FF, (w1 >> 2) & 0x3FF,
                              (w0 >> 14) & 0x3FF, (w0 >> 2) & 0x3FF); break;
            case 0x2F: printf("[tex] f%u OTHERMODE %08X %08X\n", frame, w0, w1); break;
            case 0x3C: printf("[tex] f%u COMBINE %08X %08X\n", frame, w0, w1); break;
            case 0x3A: printf("[tex] f%u PRIMCOLOR %08X\n", frame, w1); break;
            case 0x3F: printf("[tex] f%u COLORIMAGE fmt=%u siz=%u width=%u addr=%08X\n", frame,
                              w0 >> 21 & 7, w0 >> 19 & 3, (w0 & 0x3FF) + 1, w1 & 0xFFFFFF); break;
            case 0x24: case 0x25: printf("[tex] f%u TEXRECT x=%u..%u y=%u..%u tile=%u\n", frame,
                              (w1 >> 12 & 0xFFF) >> 2, (w0 >> 12 & 0xFFF) >> 2, (w1 & 0xFFF) >> 2, (w0 & 0xFFF) >> 2, w1 >> 24 & 7);
                       a += 8; break;
            }
        }
    }
    angry_ProcessRDPList();
}

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
static uint32_t rdram_size;
static const char *outdir = ".";
static uint32_t every_n;
static char every_prefix[64];

static uint8_t *pix;          /* last frame, RGB */
static uint32_t pix_w, pix_h;

static uint32_t hold_buttons, hold_until;
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

void vdac_init(struct n64video_config *config)
{
    rdram = config->gfx.rdram;
    rdram_size = config->gfx.rdram_size;
    load_script();
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
}

void vdac_close(void) {}

void screen_toggle_fullscreen(void) {}
int32_t win_width = 640, win_height = 480, win_fullscreen;

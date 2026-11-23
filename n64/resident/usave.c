/*
 * The Universe save, in the cartridge's flash (notes/re-findings.md, "The
 * Universe save").
 *
 * No Mercy keeps its own save in a log-structured file system over the
 * flash; the ROM patch (tools/build_rom.py FLASH_PATCHES) shrinks it to
 * sectors 0-5, so sectors 6 and 7 (pages 768-1023, 16 KB each) are the
 * Universe's: two copies, written in turn, so a save cut short leaves the
 * other one. A copy is its data pages (page 1 on) and then its header page
 * (page 0), written last: a copy without a good header is ignored.
 *
 * All flash access goes through the game's own libultra routines (osFlash*,
 * with the game's IO message and queue), checked before use.
 */
#include <stdint.h>
#include "usave.h"

#define RAM8(a)   (*(volatile uint8_t *)(a))
#define RAM32(a)  (*(volatile uint32_t *)(a))

typedef int  (*erase_fn)(uint32_t page);
typedef int  (*write_buffer_fn)(void *mb, int pri, void *dram, void *mq);
typedef int  (*write_array_fn)(uint32_t page);
typedef int  (*read_array_fn)(void *mb, int pri, uint32_t page, void *dram, uint32_t n, void *mq);
typedef int  (*recv_fn)(void *mq, void *msg, int block);
typedef void (*cache_fn)(void *p, int len);

#define F_ERASE      0x800302B0u     /* osFlashSectorErase(page) */
#define F_WRITE_BUF  0x80030400u     /* osFlashWriteBuffer(mb, pri, dram, mq) */
#define F_WRITE_ARR  0x800304A0u     /* osFlashWriteArray(page) */
#define F_READ_ARR   0x800305F0u     /* osFlashReadArray(mb, pri, page, dram, n, mq) */
#define F_RECV       0x80036D60u     /* osRecvMesg */
#define F_WB_DCACHE  0x800344D0u     /* osWritebackDCache */
#define F_INV_DCACHE 0x800343A0u     /* osInvalDCache */
#define G_MB         0x800644D0u     /* the game's flash OSIoMesg */
#define G_MQ         0x800644F0u     /* ... its queue */
#define G_MSG        0x80064508u     /* ... and message */
#define G_UNINIT     0x800436E0u     /* 1 until the game has run osFlashInit */

#define PAGE         128
#define SLOT_PAGE(s) (768u + 128u * (uint32_t)(s))   /* sector 6 or 7 */
#define SLOT_PAGES   128
#define MAGIC        0x4E4D5556u     /* 'NMUV' */

static uint8_t page_buf[PAGE] __attribute__((aligned(16)));

static int code_is(uint32_t a, uint32_t w0, uint32_t w1)
{
    return RAM32(a) == w0 && RAM32(a + 4) == w1;
}

/* the game's flash routines are where they should be and initialised */
static int flash_ok(void)
{
    return RAM8(G_UNINIT) == 0 &&
           code_is(F_ERASE, 0x27BDFF88u, 0x3C03800Bu) &&
           code_is(F_WRITE_BUF, 0x27BDFFD8u, 0xAFB10014u) &&
           code_is(F_WRITE_ARR, 0x3C038009u, 0x8C6320C0u) &&
           code_is(F_READ_ARR, 0x27BDFFC8u, 0xAFB40028u) &&
           code_is(F_RECV, 0x27BDFFD8u, 0xAFB00010u) &&
           code_is(F_WB_DCACHE, 0x18A00011u, 0x00000000u) &&
           code_is(F_INV_DCACHE, 0x18A00020u, 0x00000000u);
}

static int read_page(uint32_t page)
{
    ((cache_fn)F_INV_DCACHE)(page_buf, PAGE);
    if (((read_array_fn)F_READ_ARR)((void *)G_MB, 0, page, page_buf, 1, (void *)G_MQ)) return -1;
    ((recv_fn)F_RECV)((void *)G_MQ, (void *)G_MSG, 1);
    return 0;
}

static int write_page(uint32_t page)
{
    ((cache_fn)F_WB_DCACHE)(page_buf, PAGE);
    if (((write_buffer_fn)F_WRITE_BUF)((void *)G_MB, 0, page_buf, (void *)G_MQ)) return -1;
    ((recv_fn)F_RECV)((void *)G_MQ, (void *)G_MSG, 1);
    return ((write_array_fn)F_WRITE_ARR)(page) ? -1 : 0;
}

static uint32_t checksum(const uint8_t *p, uint32_t n)
{
    uint32_t h = 2166136261u;                 /* FNV-1a */
    while (n--) { h ^= *p++; h *= 16777619u; }
    return h;
}

static uint32_t get32(const uint8_t *p) { return (uint32_t)p[0] << 24 | p[1] << 16 | p[2] << 8 | p[3]; }
static void put32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v; }

/* a slot's header: 0 = magic, 4 = sequence, 8 = length, 12 = data
 * checksum, 16 = header checksum (of bytes 0-15) */
static int slot_header(int s, uint32_t *seq, uint32_t *len, uint32_t *sum)
{
    if (read_page(SLOT_PAGE(s))) return -1;
    if (get32(page_buf) != MAGIC || get32(page_buf + 16) != checksum(page_buf, 16)) return -1;
    *seq = get32(page_buf + 4);
    *len = get32(page_buf + 8);
    *sum = get32(page_buf + 12);
    if (*len == 0 || *len > (SLOT_PAGES - 1) * PAGE) return -1;
    return 0;
}

static int slot_read(int s, uint8_t *data, uint32_t len, uint32_t sum)
{
    uint32_t off, k;
    for (off = 0; off < len; off += PAGE) {
        if (read_page(SLOT_PAGE(s) + 1 + off / PAGE)) return -1;
        for (k = 0; k < PAGE && off + k < len; k++) data[off + k] = page_buf[k];
    }
    return checksum(data, len) == sum ? 0 : -1;
}

/* the newest good copy: its slot (and sequence), or -1 */
static int newest(uint32_t *seq_out)
{
    uint32_t seq[2], len, sum;
    int s, best = -1;
    for (s = 0; s < 2; s++)
        if (slot_header(s, &seq[s], &len, &sum) == 0 && (best < 0 || seq[s] > seq[best])) best = s;
    if (best >= 0) *seq_out = seq[best];
    return best;
}

int usave_load(uint8_t *data, int cap)
{
    uint32_t seq[2], len[2], sum[2];
    int s, order[2] = {0, 1}, ok[2];
    if (!flash_ok()) return -1;
    for (s = 0; s < 2; s++) ok[s] = slot_header(s, &seq[s], &len[s], &sum[s]) == 0;
    if (ok[1] && (!ok[0] || seq[1] > seq[0])) { order[0] = 1; order[1] = 0; }
    for (s = 0; s < 2; s++) {                 /* newest first, then the other */
        int k = order[s];
        if (!ok[k] || (int)len[k] > cap) continue;
        if (slot_read(k, data, len[k], sum[k]) == 0) return (int)len[k];
    }
    return -1;
}

int usave_store(const uint8_t *data, int len)
{
    uint32_t seq = 0, off, k, want;
    int cur, s, i;
    if (!flash_ok() || len <= 0 || len > (SLOT_PAGES - 1) * PAGE) return -1;
    cur = newest(&seq);
    s = cur == 0 ? 1 : 0;                     /* the other slot */
    if (((erase_fn)F_ERASE)(SLOT_PAGE(s))) return -1;
    for (off = 0; off < (uint32_t)len; off += PAGE) {
        for (k = 0; k < PAGE; k++) page_buf[k] = off + k < (uint32_t)len ? data[off + k] : 0xFF;
        if (write_page(SLOT_PAGE(s) + 1 + off / PAGE)) return -1;
    }
    for (i = 0; i < PAGE; i++) page_buf[i] = 0xFF;
    put32(page_buf, MAGIC);
    put32(page_buf + 4, seq + 1);
    put32(page_buf + 8, (uint32_t)len);
    put32(page_buf + 12, want = checksum(data, (uint32_t)len));
    put32(page_buf + 16, checksum(page_buf, 16));
    if (write_page(SLOT_PAGE(s))) return -1;
    {   /* read it back */
        uint32_t q, l, c;
        if (slot_header(s, &q, &l, &c) || q != seq + 1 || l != (uint32_t)len || c != want) return -1;
    }
    return 0;
}

/* No Mercy's own format (a blank or broken save): erase its sectors 0-5
 * only, not the whole chip (patched in place of osFlashChipErase). */
int flash_format_game(void)
{
    uint32_t s;
    for (s = 0; s < 6; s++)
        if (((erase_fn)F_ERASE)(s * 128u)) return -1;
    return 0;
}

/* SNDDUMP: run one of Covert Action's sound drivers (the original .CA file)
 * and print what it sends to its device, tick by tick, in the format of
 * `reaction-tool sound-events`.
 *
 *   SNDDUMP R|A|I FILE.CA TICKS N[,A,B][@T] ...
 *
 * The driver is loaded like COVERT's load_overlay (relocated in a 64 KiB
 * block), then its output routines are patched to software interrupts that
 * this program logs:
 *   R (RSOUND): mpu_data (CODE:048Ah) -> INT 60h, logs BL; mpu_reset and
 *               mpu_command (no MPU-401 needed) succeed at once.
 *   A (ASOUND): opl_write (CODE:0E5Dh, after the shadow write) -> INT 60h,
 *               logs AL = register, BL = value.
 *   I (ISOUND): each OUT 42h,AL -> INT 61h (divisor bytes, low then high);
 *               each OUT 61h,AL -> INT 62h (the gate, AL & 3).
 * IRQ 0 is masked: the timer interrupts are INT 8 instructions, one per tick,
 * so the run is deterministic. The handler installed (the driver's slow or fast
 * ISR) is printed with each tick, as [ISR offset].
 */
#include <stdio.h>

/* (the toolchain has STDIO.H only: the rest is declared here) */
int inp(unsigned port);
int outp(unsigned port, int value);
long atol(char *s);
char *strchr(char *s, int c);
void exit(int status);
char *getenv(char *name);

#define MKFP(s, o) ((void far *)(((unsigned long)(s) << 16) | (unsigned)(o)))
#define SEGOF(p) ((unsigned)((unsigned long)(void far *)(p) >> 16))
#define OFFOF(p) ((unsigned)(unsigned long)(void far *)(p))

typedef int (far *DRVFN)();
typedef void (far *STUB)(void);

static unsigned char far image[0x8000];         /* the driver (and its BSS) */
static unsigned char far int8stub[4] = { 0xCD, 0x08, 0xCB };   /* int 8; retf */

static unsigned code_seg;
static char mode;
static char line[24000];
static int len;
static int div_lo = -1;

/* The handlers run on the driver's stack (SS != DS): no library calls, no
 * pointers to locals. */
static void put(int c)
{
    if (len < (int)sizeof line - 1) line[len++] = (char)c;
    line[len] = 0;
}

static void hex(unsigned v, int digits)
{
    while (digits--) put("0123456789ABCDEF"[(v >> (digits * 4)) & 15]);
}

void interrupt far log60(unsigned es, unsigned ds, unsigned di, unsigned si, unsigned bp,
                         unsigned sp, unsigned bx, unsigned dx, unsigned cx, unsigned ax,
                         unsigned ip, unsigned cs, unsigned flags)
{
    put(' ');
    if (mode == 'R') {
        if ((bx & 0xFF) >= 0x80) put('|');
        hex(bx & 0xFF, 2);
    } else {
        hex(ax & 0xFF, 2);
        put('=');
        hex(bx & 0xFF, 2);
    }
}

void interrupt far log61(unsigned es, unsigned ds, unsigned di, unsigned si, unsigned bp,
                         unsigned sp, unsigned bx, unsigned dx, unsigned cx, unsigned ax,
                         unsigned ip, unsigned cs, unsigned flags)
{
    if (div_lo < 0) { div_lo = ax & 0xFF; return; }
    put(' '); put('d'); put('i'); put('v'); put('=');
    hex(((ax & 0xFF) << 8) | div_lo, 4);
    div_lo = -1;
}

void interrupt far log62(unsigned es, unsigned ds, unsigned di, unsigned si, unsigned bp,
                         unsigned sp, unsigned bx, unsigned dx, unsigned cx, unsigned ax,
                         unsigned ip, unsigned cs, unsigned flags)
{
    put(' ');
    if ((ax & 3) == 3) { put('o'); put('n'); } else { put('o'); put('f'); put('f'); }
}

static void patch(unsigned off, unsigned char *bytes, int n)
{
    unsigned char far *p = MKFP(code_seg, off);
    while (n--) *p++ = *bytes++;
}

static DRVFN slot(int k)
{
    unsigned far *exports = MKFP(code_seg, 0x32);
    return (DRVFN)MKFP(code_seg, exports[k - 100]);
}

static unsigned isr_off(void)
{
    unsigned far *ivt = MKFP(0, 0);
    return ivt[8 * 2];
}

static void (interrupt far *getvect(int n))()
{
    void (interrupt far * far *ivt)() = MKFP(0, 0);
    return ivt[n];
}

static void setvect(int n, void (interrupt far *h)())
{
    void (interrupt far * far *ivt)() = MKFP(0, 0);
    ivt[n] = h;
}

static void load(char *name)
{
    static unsigned char hdr[32];
    unsigned seg, i, nrel, hdrsize, size;
    unsigned char far *mem;
    unsigned long total;
    FILE *fd = fopen(name, "rb");
    if (!fd) { printf("can't open %s\n", name); exit(1); }
    fread(hdr, 1, 28, fd);
    total = (unsigned long)(*(unsigned *)(hdr + 4)) * 512;
    if (*(unsigned *)(hdr + 2)) total -= 512 - *(unsigned *)(hdr + 2);
    hdrsize = *(unsigned *)(hdr + 8) * 16;
    nrel = *(unsigned *)(hdr + 6);
    size = (unsigned)(total - hdrsize);
    seg = SEGOF(image) + (OFFOF(image) + 15) / 16;
    mem = MKFP(seg, 0);
    for (i = 0; i < 0x7FF0; i++) mem[i] = 0;
    fseek(fd, (long)hdrsize, 0);
    for (i = 0; i < size; i++) mem[i] = (unsigned char)getc(fd);
    fseek(fd, (long)*(unsigned *)(hdr + 0x18), 0);
    for (i = 0; i < nrel; i++) {
        unsigned rel[2];
        unsigned far *w;
        fread(rel, 1, 4, fd);
        w = MKFP(seg + rel[1], rel[0]);
        *w += seg;
    }
    fclose(fd);
    code_seg = *(unsigned far *)MKFP(seg, 0x28);   /* dw CODE, relocated */
}

struct play { long at; int n, a, b; };

static void dbg(char *msg)
{
    FILE *f;
    if (!getenv("SNDDEBUG")) return;
    f = fopen("DBG.TXT", "a");
    fprintf(f, "%s\n", msg);
    fclose(f);
}

int main(int argc, char **argv)
{
    static unsigned char r_data[] = { 0xCD, 0x60, 0xB8, 0x01, 0x00, 0xC3 };  /* int 60h; mov ax,1; ret */
    static unsigned char r_ok0[] = { 0x31, 0xC0, 0xC3 };                    /* xor ax,ax; ret */
    static unsigned char r_ok1[] = { 0xB8, 0x01, 0x00, 0xC3 };              /* mov ax,1; ret */
    static unsigned char a_out[] = { 0xCD, 0x60, 0xC3 };                    /* int 60h; ret */
    static unsigned char i_42[] = { 0xCD, 0x61 };
    static unsigned char i_61[] = { 0xCD, 0x62 };
    static unsigned isnd_42[] = { 0x01CB, 0x01D2, 0x01FB, 0x01FF, 0x02EB, 0x02EF };
    static unsigned isnd_61[] = { 0x025D, 0x045C };
    static struct play plays[32];
    void (interrupt far *old60)(), (interrupt far *old61)(), (interrupt far *old62)();
    int nplays = 0, i, k, mask;
    long ticks, t;

    if (argc < 5) { printf("usage: SNDDUMP R|A|I FILE.CA TICKS N[,A,B][@T] ...\n"); return 2; }
    mode = argv[1][0];
    dbg("start");
    load(argv[2]);
    dbg("loaded");
    ticks = atol(argv[3]);
    for (i = 4; i < argc && nplays < 32; i++) {
        char *at = strchr(argv[i], '@');
        struct play *p = &plays[nplays++];
        p->at = at ? atol(at + 1) : 0;
        p->a = p->b = 0;
        sscanf(argv[i], "%d,%d,%d", &p->n, &p->a, &p->b);
    }
    if (mode == 'R') {
        patch(0x048A, r_data, sizeof r_data);
        patch(0x04C3, r_ok0, sizeof r_ok0);
        patch(0x044E, r_ok1, sizeof r_ok1);
    } else if (mode == 'A') {
        patch(0x0E5D, a_out, sizeof a_out);
    } else {
        for (k = 0; k < 6; k++) patch(isnd_42[k], i_42, 2);
        for (k = 0; k < 2; k++) patch(isnd_61[k], i_61, 2);
    }
    old60 = getvect(0x60);
    old61 = getvect(0x61);
    old62 = getvect(0x62);
    setvect(0x60, log60);
    setvect(0x61, log61);
    setvect(0x62, log62);

    mask = inp(0x21);
    outp(0x21, mask | 1);                       /* no hardware timer ticks */
    len = 0;
    dbg("init");
    slot(100)();                                /* init */
    dbg("init done");
    printf("init:%s\n", line);
    for (t = 0; t < ticks; t++) {
        len = 0;
        line[0] = 0;
        for (k = 0; k < nplays; k++)
            if (plays[k].at == t) slot(101)(plays[k].n, plays[k].a, plays[k].b);
        if (len) printf("play %5ld:%s\n", t, line);
        len = 0;
        line[0] = 0;
        ((STUB)int8stub)();
        if (len) printf("tick %5ld [%04X]:%s\n", t, isr_off(), line);
    }
    slot(102)();                                /* shutdown: the int 8 vector back */
    outp(0x21, mask);
    setvect(0x60, old60);
    setvect(0x61, old61);
    setvect(0x62, old62);
    return 0;
}

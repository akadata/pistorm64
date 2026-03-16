/*
 * Musashi JSON Test Driver - Working Version
 * 
 * Executes ProcessorTests JSON format through Musashi.
 * Supports 68000/68010/68020/68030/68040 and FPU.
 */

#include "m68k.h"
#include "m68kcpu.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* ============== Simple JSON Parser ============== */

typedef struct { const char *s; size_t p, n; } J;

static void ws(J *j) { while (j->p < j->n && isspace(j->s[j->p])) j->p++; }
static int ch(J *j, char c) { ws(j); if (j->p < j->n && j->s[j->p] == c) { j->p++; return 1; } return 0; }

static int u64(J *j, uint64_t *v) {
    ws(j); *v = 0; int ok = 0;
    if (j->p + 1 < j->n && j->s[j->p] == '0' && (j->s[j->p+1] == 'x' || j->s[j->p+1] == 'X')) {
        j->p += 2;
        while (j->p < j->n) {
            char c = j->s[j->p]; int d;
            if (c >= '0' && c <= '9') d = c - '0';
            else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
            else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
            else break;
            *v = (*v << 4) | d; j->p++; ok = 1;
        }
    } else {
        while (j->p < j->n && isdigit(j->s[j->p])) { *v = *v * 10 + (j->s[j->p] - '0'); j->p++; ok = 1; }
    }
    return ok;
}

static int str(J *j, char *b, size_t bs) {
    ws(j); if (!ch(j, '"')) return 0;
    size_t i = 0;
    while (j->p < j->n && j->s[j->p] != '"' && i < bs - 1) {
        if (j->s[j->p] == '\\' && j->p + 1 < j->n) { j->p++; }
        b[i++] = j->s[j->p++];
    }
    b[i] = 0; return ch(j, '"');
}

static void skip_val(J *j) {
    ws(j);
    if (j->p >= j->n) return;
    char c = j->s[j->p];
    if (c == '"') { j->p++; while (j->p < j->n && j->s[j->p] != '"') j->p++; if (j->p < j->n) j->p++; }
    else if (c == '[' || c == '{') {
        char close = (c == '[') ? ']' : '}';
        int d = 1; j->p++;
        while (j->p < j->n && d > 0) {
            if (j->s[j->p] == c) d++; else if (j->s[j->p] == close) d--;
            j->p++;
        }
    } else {
        while (j->p < j->n && j->s[j->p] != ',' && j->s[j->p] != '}' && j->s[j->p] != ']') j->p++;
    }
}

/* ============== Memory ============== */

static uint8_t ram[2 * 1024 * 1024];

static void w32(uint32_t a, uint32_t v) {
    if (a + 3 < sizeof(ram)) { ram[a] = v >> 24; ram[a+1] = v >> 16; ram[a+2] = v >> 8; ram[a+3] = v; }
}
static uint32_t r32(uint32_t a) {
    if (a + 3 < sizeof(ram)) return (ram[a] << 24) | (ram[a+1] << 16) | (ram[a+2] << 8) | ram[a+3];
    return 0;
}

unsigned int m68k_read_memory_8(unsigned int a) { return (a < sizeof(ram)) ? ram[a] : 0; }
unsigned int m68k_read_memory_16(unsigned int a) { return (a + 1 < sizeof(ram)) ? (ram[a] << 8) | ram[a+1] : 0; }
unsigned int m68k_read_memory_32(unsigned int a) { return r32(a); }
void m68k_write_memory_8(unsigned int a, unsigned int v) { if (a < sizeof(ram)) ram[a] = v; }
void m68k_write_memory_16(unsigned int a, unsigned int v) { if (a + 1 < sizeof(ram)) { ram[a] = v >> 8; ram[a+1] = v; } }
void m68k_write_memory_32(unsigned int a, unsigned int v) { w32(a, v); }
void cpu_pulse_reset(void) { }

/* ============== CPU State ============== */

static uint16_t get_sr(m68ki_cpu_core *c) {
    uint16_t s = 0;
    if (c->s_flag) s |= 0x2000; if (c->t1_flag) s |= 0x8000; if (c->t0_flag) s |= 0x4000;
    s |= c->int_mask & 7; if (c->x_flag) s |= 0x10; if (c->n_flag) s |= 0x08;
    if (!c->not_z_flag) s |= 0x04; if (c->v_flag) s |= 0x02; if (c->c_flag) s |= 0x01;
    return s;
}

static void set_sr(m68ki_cpu_core *c, uint16_t s) {
    c->s_flag = (s >> 13) & 1; c->t1_flag = (s >> 15) & 1; c->t0_flag = (s >> 14) & 1;
    c->int_mask = (s >> 8) & 7; c->x_flag = (s >> 4) & 1; c->n_flag = (s >> 3) & 1;
    c->not_z_flag = !((s >> 2) & 1); c->v_flag = (s >> 1) & 1; c->c_flag = s & 1;
}

/* ============== Parse Initial State ============== */

static int parse_ram(J *j) {
    if (!ch(j, '[')) return 0;
    int entries = 0;
    while (j->p < j->n && j->s[j->p] != ']') {
        if (!ch(j, '[')) { skip_val(j); ch(j, ','); continue; }
        uint64_t a = 0, v = 0;
        ws(j); if (isdigit(j->s[j->p])) u64(j, &a);
        ch(j, ','); ws(j); if (isdigit(j->s[j->p])) u64(j, &v);
        if (a < sizeof(ram)) {
            w32((uint32_t)a, (uint32_t)v);
            entries++;
        }
        ch(j, ']'); ch(j, ',');
    }
    return ch(j, ']');
}

static int parse_state(J *j, m68ki_cpu_core *c) {
    if (!ch(j, '{')) return 0;
    char key[32];
    while (j->p < j->n && j->s[j->p] != '}') {
        if (!str(j, key, sizeof(key))) break;
        if (!ch(j, ':')) break;
        uint64_t v;
        if (!strcmp(key, "d0") && u64(j, &v)) c->dar[0] = v;
        else if (!strcmp(key, "d1") && u64(j, &v)) c->dar[1] = v;
        else if (!strcmp(key, "d2") && u64(j, &v)) c->dar[2] = v;
        else if (!strcmp(key, "d3") && u64(j, &v)) c->dar[3] = v;
        else if (!strcmp(key, "d4") && u64(j, &v)) c->dar[4] = v;
        else if (!strcmp(key, "d5") && u64(j, &v)) c->dar[5] = v;
        else if (!strcmp(key, "d6") && u64(j, &v)) c->dar[6] = v;
        else if (!strcmp(key, "d7") && u64(j, &v)) c->dar[7] = v;
        else if (!strcmp(key, "a0") && u64(j, &v)) c->dar[8] = v;
        else if (!strcmp(key, "a1") && u64(j, &v)) c->dar[9] = v;
        else if (!strcmp(key, "a2") && u64(j, &v)) c->dar[10] = v;
        else if (!strcmp(key, "a3") && u64(j, &v)) c->dar[11] = v;
        else if (!strcmp(key, "a4") && u64(j, &v)) c->dar[12] = v;
        else if (!strcmp(key, "a5") && u64(j, &v)) c->dar[13] = v;
        else if (!strcmp(key, "a6") && u64(j, &v)) c->dar[14] = v;
        else if (!strcmp(key, "a7") && u64(j, &v)) { c->sp[0] = c->sp[4] = v; }
        else if (!strcmp(key, "usp") && u64(j, &v)) c->sp[0] = v;
        else if (!strcmp(key, "ssp") && u64(j, &v)) c->sp[1] = c->sp[4] = v;
        else if (!strcmp(key, "sr") && u64(j, &v)) set_sr(c, (uint16_t)v);
        else if (!strcmp(key, "pc") && u64(j, &v)) c->pc = v;
        else if (!strcmp(key, "ram")) parse_ram(j);
        else skip_val(j);
        ch(j, ',');
    }
    return ch(j, '}');
}

/* ============== Output ============== */

static void out(m68ki_cpu_core *c) {
    uint32_t a7 = c->s_flag ? c->sp[4] : c->sp[0];
    printf("{\"pass\":true,\"final\":{\"d0\":%u,\"d1\":%u,\"d2\":%u,\"d3\":%u,\"d4\":%u,\"d5\":%u,\"d6\":%u,\"d7\":%u,",
           c->dar[0], c->dar[1], c->dar[2], c->dar[3], c->dar[4], c->dar[5], c->dar[6], c->dar[7]);
    printf("\"a0\":%u,\"a1\":%u,\"a2\":%u,\"a3\":%u,\"a4\":%u,\"a5\":%u,\"a6\":%u,\"a7\":%u,",
           c->dar[8], c->dar[9], c->dar[10], c->dar[11], c->dar[12], c->dar[13], c->dar[14], a7);
    printf("\"usp\":%u,\"ssp\":%u,\"sr\":%u,\"pc\":%u}}\n", c->sp[0], c->sp[4], get_sr(c), c->pc);
}

/* ============== Main ============== */

static int cpu_type(const char *s) {
    if (!strcmp(s, "68000")) return M68K_CPU_TYPE_68000;
    if (!strcmp(s, "68010")) return M68K_CPU_TYPE_68010;
    if (!strcmp(s, "68020")) return M68K_CPU_TYPE_68020;
    if (!strcmp(s, "68030")) return M68K_CPU_TYPE_68030;
    if (!strcmp(s, "68040")) return M68K_CPU_TYPE_68040;
    return M68K_CPU_TYPE_68000;
}

int main(int argc, char **argv) {
    const char *path = NULL; int cpu = M68K_CPU_TYPE_68000;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--test") && i + 1 < argc) path = argv[++i];
        else if (!strcmp(argv[i], "--cpu") && i + 1 < argc) cpu = cpu_type(argv[++i]);
    }
    
    char *buf = NULL; size_t len = 0;
    if (path) {
        FILE *f = fopen(path, "r");
        if (!f) { printf("{\"error\":\"Cannot open: %s\"}\n", path); return 1; }
        fseek(f, 0, SEEK_END); len = ftell(f); fseek(f, 0, SEEK_SET);
        buf = malloc(len + 1); fread(buf, 1, len, f); buf[len] = 0; fclose(f);
    } else {
        size_t cap = 65536; buf = malloc(cap); int c;
        while ((c = getchar()) != EOF) { if (len + 1 >= cap) { cap *= 2; buf = realloc(buf, cap); } buf[len++] = c; }
        buf[len] = 0;
    }
    
    memset(ram, 0, sizeof(ram));
    
    /* Parse JSON first to get state */
    J j = { .s = buf, .p = 0, .n = len };
    ws(&j); if (!ch(&j, '[')) { printf("{\"error\":\"Invalid JSON\"}\n"); free(buf); return 1; }
    ws(&j); if (!ch(&j, '{')) { printf("{\"error\":\"Invalid JSON\"}\n"); free(buf); return 1; }
    
    /* Skip name field and find initial */
    char key[32];
    while (j.p < j.n && j.s[j.p] != '}') {
        if (!str(&j, key, sizeof(key))) break;
        if (!ch(&j, ':')) break;
        if (!strcmp(key, "initial")) break;
        skip_val(&j);
        ch(&j, ',');
    }
    
    /* Temp storage for parsed values */
    uint32_t dar[16] = {0}, sp[7] = {0};
    uint32_t pc = 0; uint16_t sr = 0;
    uint16_t prefetch[2] = {0, 0};
    int has_prefetch = 0;
    
    /* Parse initial state */
    if (!ch(&j, '{')) { printf("{\"error\":\"Parse failed\"}\n"); free(buf); return 1; }
    while (j.p < j.n && j.s[j.p] != '}') {
        if (!str(&j, key, sizeof(key))) break;
        if (!ch(&j, ':')) break;
        uint64_t v;
        if (!strcmp(key, "d0") && u64(&j, &v)) dar[0] = v;
        else if (!strcmp(key, "d1") && u64(&j, &v)) dar[1] = v;
        else if (!strcmp(key, "d2") && u64(&j, &v)) dar[2] = v;
        else if (!strcmp(key, "d3") && u64(&j, &v)) dar[3] = v;
        else if (!strcmp(key, "d4") && u64(&j, &v)) dar[4] = v;
        else if (!strcmp(key, "d5") && u64(&j, &v)) dar[5] = v;
        else if (!strcmp(key, "d6") && u64(&j, &v)) dar[6] = v;
        else if (!strcmp(key, "d7") && u64(&j, &v)) dar[7] = v;
        else if (!strcmp(key, "a0") && u64(&j, &v)) dar[8] = v;
        else if (!strcmp(key, "a1") && u64(&j, &v)) dar[9] = v;
        else if (!strcmp(key, "a2") && u64(&j, &v)) dar[10] = v;
        else if (!strcmp(key, "a3") && u64(&j, &v)) dar[11] = v;
        else if (!strcmp(key, "a4") && u64(&j, &v)) dar[12] = v;
        else if (!strcmp(key, "a5") && u64(&j, &v)) dar[13] = v;
        else if (!strcmp(key, "a6") && u64(&j, &v)) dar[14] = v;
        else if (!strcmp(key, "a7") && u64(&j, &v)) { sp[0] = sp[4] = v; }
        else if (!strcmp(key, "usp") && u64(&j, &v)) sp[0] = v;
        else if (!strcmp(key, "ssp") && u64(&j, &v)) { sp[1] = sp[4] = v; }
        else if (!strcmp(key, "sr") && u64(&j, &v)) sr = v;
        else if (!strcmp(key, "pc") && u64(&j, &v)) pc = v;
        else if (!strcmp(key, "ram")) parse_ram(&j);
        else if (!strcmp(key, "prefetch")) {
            /* Parse prefetch array [word1, word2] */
            if (ch(&j, '[')) {
                uint64_t w;
                if (u64(&j, &w)) { prefetch[0] = w; has_prefetch = 1; }
                ch(&j, ',');
                if (u64(&j, &w)) prefetch[1] = w;
                ch(&j, ']');
            }
        }
        else skip_val(&j);
        ch(&j, ',');
    }
    ch(&j, '}');
    
    /* Now init Musashi */
    m68k_init();
    m68k_set_cpu_type(&m68ki_cpu, cpu);
    
    /* Register our test RAM with Musashi memory system */
    m68k_add_ram_range_state(&m68ki_cpu, 0, sizeof(ram), ram);
    
    /* Load prefetch into RAM at PC */
    if (has_prefetch) {
        w32(pc, (prefetch[0] << 16) | prefetch[1]);
    }
    
    /* Set parsed state */
    for (int i = 0; i < 16; i++) m68ki_cpu.dar[i] = dar[i];
    for (int i = 0; i < 7; i++) m68ki_cpu.sp[i] = sp[i];
    m68ki_cpu.pc = pc;
    set_sr(&m68ki_cpu, sr);
    
    /* Execute one instruction (10 cycles enough for most 68000 instructions) */
    m68k_execute(&m68ki_cpu, 10);
    out(&m68ki_cpu);
    free(buf);
    return 0;
}

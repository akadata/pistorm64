/*
 * JIT JSON Test Driver
 * 
 * Executes ProcessorTests JSON format through the m68xkcpu JIT.
 * Compares results against expected final state.
 */

#define USE_M68XK_JIT 1

#include "m68k.h"
#include "m68kcpu.h"
#include "m68xkcpu/jit.h"
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

/* ============== CPU State ============== */

extern m68ki_cpu_core m68ki_cpu;

static void set_sr(uint32_t sr) {
    m68ki_cpu.s_flag = (sr >> 13) & 1;
    m68ki_cpu.n_flag = (sr >> 7) & 0x80;
    m68ki_cpu.not_z_flag = !((sr >> 2) & 1);
    m68ki_cpu.v_flag = (sr >> 1) & 0x80;
    m68ki_cpu.c_flag = (sr >> 0) & 0x100;
    m68ki_cpu.x_flag = (sr >> 4) & 0x100;
}

static uint32_t get_sr(void) {
    uint32_t sr = 0;
    if (m68ki_cpu.s_flag) sr |= (1 << 13);
    if (m68ki_cpu.n_flag) sr |= (1 << 7);
    if (!m68ki_cpu.not_z_flag) sr |= (1 << 2);
    if (m68ki_cpu.v_flag) sr |= (1 << 1);
    if (m68ki_cpu.c_flag) sr |= (1 << 0);
    if (m68ki_cpu.x_flag) sr |= (1 << 4);
    return sr;
}

static void set_cpu_state(uint32_t *d, uint32_t *a, uint32_t pc, uint32_t sr, uint32_t usp, uint32_t ssp) {
    for (int i = 0; i < 8; i++) m68ki_cpu.dar[i] = d[i];
    for (int i = 0; i < 8; i++) m68ki_cpu.dar[8+i] = a[i];
    m68ki_cpu.pc = pc;
    set_sr(sr);
    m68ki_cpu.sp[0] = usp;
    m68ki_cpu.sp[4] = ssp;
    m68ki_cpu.dar[15] = ssp;
}

static void get_cpu_state(uint32_t *d, uint32_t *a, uint32_t *pc, uint32_t *sr, uint32_t *usp, uint32_t *ssp) {
    for (int i = 0; i < 8; i++) d[i] = m68ki_cpu.dar[i];
    for (int i = 0; i < 8; i++) a[i] = m68ki_cpu.dar[8+i];
    *pc = m68ki_cpu.pc;
    *sr = get_sr();
    *usp = m68ki_cpu.sp[0];
    *ssp = m68ki_cpu.sp[4];
}

/* ============== Test Execution ============== */

static int run_jit_test(const char *json, size_t json_len) {
    J j = {json, 0, json_len};
    int passed = 0, failed = 0;
    
    if (!ch(&j, '[')) return -1;
    
    while (j.p < j.n) {
        ws(&j);
        if (j.p >= j.n || j.s[j.p] == ']') break;
        if (!ch(&j, '{')) { skip_val(&j); ch(&j, ','); continue; }
        
        /* Parse test case */
        uint32_t d[8] = {0}, a[8] = {0}, pc = 0, sr = 0, usp = 0, ssp = 0;
        uint32_t exp_d[8] = {0}, exp_a[8] = {0}, exp_pc = 0, exp_sr = 0;
        
        while (j.p < j.n && j.s[j.p] != '}') {
            char key[64];
            if (!str(&j, key, sizeof(key))) { skip_val(&j); ch(&j, ','); continue; }
            ws(&j); ch(&j, ':');
            
            if (strcmp(key, "initial") == 0) {
                if (ch(&j, '{')) {
                    while (j.p < j.n && j.s[j.p] != '}') {
                        char k2[32]; uint64_t v;
                        if (str(&j, k2, sizeof(k2)) && ch(&j, ':') && u64(&j, &v)) {
                            if (k2[0] == 'd' && k2[1] >= '0' && k2[1] <= '7') d[k2[1]-'0'] = v;
                            else if (k2[0] == 'a' && k2[1] >= '0' && k2[1] <= '7') a[k2[1]-'0'] = v;
                            else if (strcmp(k2, "pc") == 0) pc = v;
                            else if (strcmp(k2, "sr") == 0) sr = v;
                            else if (strcmp(k2, "usp") == 0) usp = v;
                            else if (strcmp(k2, "ssp") == 0) ssp = v;
                        } else {
                            skip_val(&j);
                        }
                        ch(&j, ',');
                    }
                    ch(&j, '}');
                }
            } else if (strcmp(key, "final") == 0) {
                if (ch(&j, '{')) {
                    while (j.p < j.n && j.s[j.p] != '}') {
                        char k2[32]; uint64_t v;
                        if (str(&j, k2, sizeof(k2)) && ch(&j, ':') && u64(&j, &v)) {
                            if (k2[0] == 'd' && k2[1] >= '0' && k2[1] <= '7') exp_d[k2[1]-'0'] = v;
                            else if (k2[0] == 'a' && k2[1] >= '0' && k2[1] <= '7') exp_a[k2[1]-'0'] = v;
                            else if (strcmp(k2, "pc") == 0) exp_pc = v;
                            else if (strcmp(k2, "sr") == 0) exp_sr = v;
                        } else {
                            skip_val(&j);
                        }
                        ch(&j, ',');
                    }
                    ch(&j, '}');
                }
            } else if (strcmp(key, "ram") == 0) {
                if (ch(&j, '[')) {
                    while (j.p < j.n && j.s[j.p] != ']') {
                        if (ch(&j, '[')) {
                            uint64_t addr, val;
                            if (u64(&j, &addr) && ch(&j, ',') && u64(&j, &val)) {
                                w32((uint32_t)addr, (uint32_t)val);
                            }
                            ch(&j, ']');
                        }
                        ch(&j, ',');
                    }
                    ch(&j, ']');
                }
            } else {
                skip_val(&j);
            }
            ch(&j, ',');
        }
        ch(&j, '}');
        ch(&j, ',');
        
        /* Execute through JIT */
        memset(ram, 0, sizeof(ram));
        set_cpu_state(d, a, pc, sr, usp, ssp);
        
        /* Initialize JIT */
        static int jit_inited = 0;
        if (!jit_inited) {
            if (jit_init(&m68ki_cpu, 0) != 0) {
                fprintf(stderr, "JIT init failed\n");
                return -1;
            }
            jit_inited = 1;
        }
        
        /* Execute one instruction through JIT */
        int cycles = jit_execute(pc, 100);
        
        /* Get result */
        uint32_t got_d[8], got_a[8], got_pc, got_sr, got_usp, got_ssp;
        get_cpu_state(got_d, got_a, &got_pc, &got_sr, &got_usp, &got_ssp);
        
        /* Compare */
        int ok = 1;
        for (int i = 0; i < 8 && ok; i++) if (got_d[i] != exp_d[i]) ok = 0;
        for (int i = 0; i < 8 && ok; i++) if (got_a[i] != exp_a[i]) ok = 0;
        if (got_pc != exp_pc) ok = 0;
        if (got_sr != exp_sr) ok = 0;
        
        if (ok) passed++; else failed++;
    }
    
    printf("JIT: %d passed, %d failed\n", passed, failed);
    return failed == 0 ? 0 : 1;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <test.json>\n", argv[0]);
        return 1;
    }
    
    /* Load JSON file */
    FILE *f = fopen(argv[1], "rb");
    if (!f) {
        fprintf(stderr, "Cannot open %s\n", argv[1]);
        return 1;
    }
    fseek(f, 0, SEEK_END);
    size_t len = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *json = malloc(len + 1);
    fread(json, 1, len, f);
    json[len] = 0;
    fclose(f);
    
    /* Run test */
    int rc = run_jit_test(json, len);
    free(json);
    return rc;
}

#include "m68k.h"
#include "m68kcpu.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct memory_device_tag_t {
    uint32_t mask;

    uint8_t (*read8)(struct memory_device_tag_t *, uint32_t);
    uint16_t (*read16)(struct memory_device_tag_t *, uint32_t);
    uint32_t (*read32)(struct memory_device_tag_t *, uint32_t);
    void (*write8)(struct memory_device_tag_t *, uint32_t, uint8_t);
    void (*write16)(struct memory_device_tag_t *, uint32_t, uint16_t);
    void (*write32)(struct memory_device_tag_t *, uint32_t, uint32_t);
} memory_device_t;

#define BLOCK_SIZE 0x10000U
#define MMAP_SIZE 0x10000U

static memory_device_t *memory_map[MMAP_SIZE];

static uint8_t read8_fail(memory_device_t *dev, uint32_t address)
{
    (void)dev;
    (void)address;
    m68k_pulse_bus_error(&m68ki_cpu);
    return 0;
}

static uint16_t read16_fail(memory_device_t *dev, uint32_t address)
{
    (void)dev;
    (void)address;
    m68k_pulse_bus_error(&m68ki_cpu);
    return 0;
}

static uint32_t read32_fail(memory_device_t *dev, uint32_t address)
{
    (void)dev;
    (void)address;
    m68k_pulse_bus_error(&m68ki_cpu);
    return 0;
}

static void write8_fail(memory_device_t *dev, uint32_t address, uint8_t value)
{
    (void)dev;
    (void)address;
    (void)value;
    m68k_pulse_bus_error(&m68ki_cpu);
}

static void write16_fail(memory_device_t *dev, uint32_t address, uint16_t value)
{
    (void)dev;
    (void)address;
    (void)value;
    m68k_pulse_bus_error(&m68ki_cpu);
}

static void write32_fail(memory_device_t *dev, uint32_t address, uint32_t value)
{
    (void)dev;
    (void)address;
    (void)value;
    m68k_pulse_bus_error(&m68ki_cpu);
}

static memory_device_t mdev_not_mapped = {
    0, read8_fail, read16_fail, read32_fail, write8_fail, write16_fail, write32_fail,
};

static void memory_map_init(void)
{
    size_t i;
    for (i = 0; i < MMAP_SIZE; ++i) {
        memory_map[i] = &mdev_not_mapped;
    }
}

static void memory_map_add(memory_device_t *dev, uint32_t start_addr, uint32_t size)
{
    unsigned count;
    unsigned off;
    unsigned i;

    assert(start_addr % BLOCK_SIZE == 0);
    assert(size % BLOCK_SIZE == 0);

    count = size / BLOCK_SIZE;
    off = start_addr / BLOCK_SIZE;
    for (i = 0; i < count; ++i) {
        memory_map[off + i] = dev;
    }
}

unsigned int m68k_read_memory_8(unsigned int address)
{
    unsigned slot = address / BLOCK_SIZE;
    memory_device_t *dev = memory_map[slot];
    return dev->read8(dev, dev->mask & address);
}

unsigned int m68k_read_memory_16(unsigned int address)
{
    unsigned slot = address / BLOCK_SIZE;
    memory_device_t *dev = memory_map[slot];
    return dev->read16(dev, dev->mask & address);
}

unsigned int m68k_read_memory_32(unsigned int address)
{
    unsigned slot = address / BLOCK_SIZE;
    memory_device_t *dev = memory_map[slot];
    return dev->read32(dev, dev->mask & address);
}

void m68k_write_memory_8(unsigned int address, unsigned int value)
{
    unsigned slot = address / BLOCK_SIZE;
    memory_device_t *dev = memory_map[slot];
    dev->write8(dev, dev->mask & address, value);
}

void m68k_write_memory_16(unsigned int address, unsigned int value)
{
    unsigned slot = address / BLOCK_SIZE;
    memory_device_t *dev = memory_map[slot];
    dev->write16(dev, dev->mask & address, value);
}

void m68k_write_memory_32(unsigned int address, unsigned int value)
{
    unsigned slot = address / BLOCK_SIZE;
    memory_device_t *dev = memory_map[slot];
    dev->write32(dev, dev->mask & address, value);
}

typedef struct test_device_tag_t {
    memory_device_t dev;
    uint32_t test_pass_count;
    uint32_t test_fail_count;
} test_device_t;

static uint8_t test_read8(memory_device_t *dev, uint32_t address)
{
    (void)dev;
    (void)address;
    return 0;
}

static uint16_t test_read16(memory_device_t *dev, uint32_t address)
{
    (void)dev;
    (void)address;
    return 0;
}

static uint32_t test_read32(memory_device_t *dev, uint32_t address)
{
    (void)dev;
    (void)address;
    return 0;
}

static void test_write8(memory_device_t *dev, uint32_t address, uint8_t value)
{
    (void)dev;
    if (address == 0x14) {
        char ss[2] = {(char)value, 0};
        puts(ss);
    }
}

static void test_write16(memory_device_t *dev, uint32_t address, uint16_t value)
{
    (void)dev;
    (void)address;
    (void)value;
}

static void test_write32(memory_device_t *dev, uint32_t address, uint32_t value)
{
    test_device_t *td = (test_device_t *)dev;
    if (address == 0x0) {
        ++td->test_fail_count;
    }
    if (address == 0x4) {
        ++td->test_pass_count;
    }
    if (address == 0xC) {
        m68k_set_irq(value & 0x7);
        m68k_end_timeslice();
    }
}

static void test_device_init(test_device_t *dev)
{
    dev->test_pass_count = 0;
    dev->test_fail_count = 0;
    dev->dev.mask = 0x10000 - 1;
    dev->dev.read8 = test_read8;
    dev->dev.read16 = test_read16;
    dev->dev.read32 = test_read32;
    dev->dev.write8 = test_write8;
    dev->dev.write16 = test_write16;
    dev->dev.write32 = test_write32;
}

#define RAM_SLOT_SIZE 0x10000U

typedef struct ram_slot_tag_t {
    memory_device_t dev;
    uint8_t memory[RAM_SLOT_SIZE];
} ram_slot_t;

static uint8_t ram_slot_read8(memory_device_t *dev, uint32_t addr)
{
    assert(addr < RAM_SLOT_SIZE);
    return ((ram_slot_t *)dev)->memory[addr];
}

static uint16_t ram_slot_read16(memory_device_t *dev, uint32_t addr)
{
    if (addr + 1 >= RAM_SLOT_SIZE) {
        m68k_pulse_bus_error(&m68ki_cpu);
        return 0;
    }
    return (((uint16_t)ram_slot_read8(dev, addr + 0)) << 8) |
           (((uint16_t)ram_slot_read8(dev, addr + 1)) << 0);
}

static uint32_t ram_slot_read32(memory_device_t *dev, uint32_t addr)
{
    return (((uint32_t)ram_slot_read16(dev, addr + 0)) << 16) |
           (((uint32_t)ram_slot_read16(dev, addr + 2)) << 0);
}

static void ram_slot_write8(memory_device_t *dev, uint32_t addr, uint8_t val)
{
    assert(addr < RAM_SLOT_SIZE);
    ((ram_slot_t *)dev)->memory[addr] = val;
}

static void ram_slot_write16(memory_device_t *dev, uint32_t addr, uint16_t val)
{
    if (addr + 1 >= RAM_SLOT_SIZE) {
        m68k_pulse_bus_error(&m68ki_cpu);
        return;
    }
    ram_slot_write8(dev, addr + 0, (val >> 8) & 0xFF);
    ram_slot_write8(dev, addr + 1, (val >> 0) & 0xFF);
}

static void ram_slot_write32(memory_device_t *dev, uint32_t addr, uint32_t val)
{
    ram_slot_write16(dev, addr + 0, (val >> 16) & 0xFFFF);
    ram_slot_write16(dev, addr + 2, (val >> 0) & 0xFFFF);
}

static void ram_slot_init(ram_slot_t *dev)
{
    dev->dev.mask = RAM_SLOT_SIZE - 1;
    dev->dev.read8 = ram_slot_read8;
    dev->dev.read16 = ram_slot_read16;
    dev->dev.read32 = ram_slot_read32;
    dev->dev.write8 = ram_slot_write8;
    dev->dev.write16 = ram_slot_write16;
    dev->dev.write32 = ram_slot_write32;
}

#define ROM_SLOT_SIZE 0x10000U

typedef struct rom_slot_tag_t {
    memory_device_t dev;
    uint8_t memory[ROM_SLOT_SIZE];
} rom_slot_t;

static uint8_t rom_slot_read8(memory_device_t *dev, uint32_t addr)
{
    assert(addr < ROM_SLOT_SIZE);
    return ((rom_slot_t *)dev)->memory[addr];
}

static uint16_t rom_slot_read16(memory_device_t *dev, uint32_t addr)
{
    if (addr + 1 >= ROM_SLOT_SIZE) {
        m68k_pulse_bus_error(&m68ki_cpu);
        return 0;
    }
    return (((uint16_t)rom_slot_read8(dev, addr + 0)) << 8) |
           (((uint16_t)rom_slot_read8(dev, addr + 1)) << 0);
}

static uint32_t rom_slot_read32(memory_device_t *dev, uint32_t addr)
{
    return (((uint32_t)rom_slot_read16(dev, addr + 0)) << 16) |
           (((uint32_t)rom_slot_read16(dev, addr + 2)) << 0);
}

static void rom_slot_write8(memory_device_t *dev, uint32_t address, uint8_t value)
{
    (void)dev;
    (void)address;
    (void)value;
    m68k_pulse_bus_error(&m68ki_cpu);
}

static void rom_slot_write16(memory_device_t *dev, uint32_t address, uint16_t value)
{
    (void)dev;
    (void)address;
    (void)value;
    m68k_pulse_bus_error(&m68ki_cpu);
}

static void rom_slot_write32(memory_device_t *dev, uint32_t address, uint32_t value)
{
    (void)dev;
    (void)address;
    (void)value;
    m68k_pulse_bus_error(&m68ki_cpu);
}

static size_t rom_slot_init(rom_slot_t *dev, FILE *file)
{
    dev->dev.mask = ROM_SLOT_SIZE - 1;
    dev->dev.read8 = rom_slot_read8;
    dev->dev.read16 = rom_slot_read16;
    dev->dev.read32 = rom_slot_read32;
    dev->dev.write8 = rom_slot_write8;
    dev->dev.write16 = rom_slot_write16;
    dev->dev.write32 = rom_slot_write32;
    if (file != NULL) {
        return fread(dev->memory, 1, ROM_SLOT_SIZE, file);
    }
    return 0;
}

static ram_slot_t g_stack;
static ram_slot_t g_extra_ram1;
#define N_ROMS 4
static rom_slot_t g_roms[N_ROMS];
static test_device_t g_test_device;

static void setup_memory(void)
{
    unsigned i;
    memory_map_init();

    memory_map_add(&g_stack.dev, 0x0, RAM_SLOT_SIZE);
    for (i = 0; i < N_ROMS; ++i) {
        memory_map_add(&g_roms[i].dev, RAM_SLOT_SIZE + ROM_SLOT_SIZE * i, ROM_SLOT_SIZE);
    }
    memory_map_add(&g_extra_ram1.dev, 0x300000, RAM_SLOT_SIZE);
    memory_map_add(&g_test_device.dev, 0x100000, 0x10000);
}

static void setup_bootsec(void)
{
    int i;
    for (i = 0; i < 64; ++i) {
        m68k_write_memory_32((unsigned)(i * 4), 0xDEADBEEF);
    }
    m68k_write_memory_32(0, 0x3F0);     /* SP */
    m68k_write_memory_32(4, 0x10000);   /* Entry */
}

static void usage(const char *prog)
{
    fprintf(stderr,
            "Usage: %s <test.bin> [--cpu 68000|68010|68020|68030|68040] "
            "[--iterations N] [--cycles N]\n",
            prog);
}

static unsigned parse_cpu_type(const char *cpu)
{
    if (strcmp(cpu, "68000") == 0) return M68K_CPU_TYPE_68000;
    if (strcmp(cpu, "68010") == 0) return M68K_CPU_TYPE_68010;
    if (strcmp(cpu, "68020") == 0) return M68K_CPU_TYPE_68020;
    if (strcmp(cpu, "68030") == 0) return M68K_CPU_TYPE_68030;
    if (strcmp(cpu, "68040") == 0) return M68K_CPU_TYPE_68040;
    return 0;
}

int main(int argc, char *argv[])
{
    FILE *infile;
    unsigned cpu_type = M68K_CPU_TYPE_68040;
    int iterations = 100;
    int cycles = 0x1000000;
    int i;
    const char *bin_path;

    if (argc < 2) {
        usage(argv[0]);
        return EXIT_FAILURE;
    }

    bin_path = argv[1];
    for (i = 2; i < argc; ++i) {
        if (strcmp(argv[i], "--cpu") == 0) {
            if (++i >= argc) {
                usage(argv[0]);
                return EXIT_FAILURE;
            }
            cpu_type = parse_cpu_type(argv[i]);
            if (cpu_type == 0) {
                fprintf(stderr, "Unsupported CPU type: %s\n", argv[i]);
                return EXIT_FAILURE;
            }
        } else if (strcmp(argv[i], "--iterations") == 0) {
            if (++i >= argc) {
                usage(argv[0]);
                return EXIT_FAILURE;
            }
            iterations = (int)strtol(argv[i], NULL, 0);
        } else if (strcmp(argv[i], "--cycles") == 0) {
            if (++i >= argc) {
                usage(argv[0]);
                return EXIT_FAILURE;
            }
            cycles = (int)strtol(argv[i], NULL, 0);
        } else {
            usage(argv[0]);
            return EXIT_FAILURE;
        }
    }

    infile = fopen(bin_path, "rb");
    if (infile == NULL) {
        printf("Cannot open: %s\n", bin_path);
        return EXIT_FAILURE;
    }

    ram_slot_init(&g_stack);
    ram_slot_init(&g_extra_ram1);
    for (i = 0; i < N_ROMS; ++i) {
        (void)rom_slot_init(&g_roms[i], infile);
    }
    fclose(infile);

    test_device_init(&g_test_device);
    setup_memory();
    setup_bootsec();

    m68k_init();
    m68k_set_cpu_type(&m68ki_cpu, cpu_type);
    m68k_pulse_reset(&m68ki_cpu);

    for (i = 0; i < iterations; ++i) {
        (void)m68k_execute(&m68ki_cpu, cycles);
    }

    printf("test_pass_count = %u\n", g_test_device.test_pass_count);
    printf("test_fail_count = %u\n", g_test_device.test_fail_count);

    if (g_test_device.test_fail_count == 0 && g_test_device.test_pass_count > 0) {
        return EXIT_SUCCESS;
    }
    return EXIT_FAILURE;
}

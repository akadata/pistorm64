#include "qemu/osdep.h"
#include "qapi/error.h"
#include "cpu.h"
#include "hw/m68k/mcf.h"
#include "hw/boards.h"
#include "hw/irq.h"
#include "hw/loader.h"
#include "elf.h"
#include "exec/address-spaces.h"
#include "qemu/error-report.h"
#include "sysemu/qtest.h"
#include <stdio.h>
#include "ps_protocol.h"


static uint64_t pistorm_ami_read(void *opaque, hwaddr addr,
                                      unsigned size)
{
   uint64_t value;

//   printf("READ SIZE:%x ADDR:%llx\n",size,addr);

   switch (size) {
     case 1: value = (uint64_t)read8((uint32_t)addr);break;
     case 2: value = (uint64_t)read16((uint32_t)addr);break;
     case 4: value = (uint64_t)read32((uint32_t)addr);break;
     default: value = 0x4E71; printf("WTF READ?!\n"); break;
   }
   return value;
}


static void pistorm_ami_write(void *opaque, hwaddr addr,
                                   uint64_t val, unsigned size)
{
//   printf("WRITE SIZE:%x ADDR:%llx DATA:%llx\n",size,addr,val);
   switch (size) {
     case 1: write8((uint32_t)addr,(uint32_t)val);break;
     case 2: write16((uint32_t)addr,(uint32_t)val);break;
     case 4: write32((uint32_t)addr,(uint32_t)val);break;
     default: printf("WTF WRITE?!\n"); break;
   }
   return;
}

static const MemoryRegionOps pistorm_ami_ops = {
    .read = pistorm_ami_read,
    .write = pistorm_ami_write,
    .valid.min_access_size = 1,
    .valid.max_access_size = 4,
    .impl.min_access_size = 1,
    .impl.max_access_size = 4,
    .endianness = DEVICE_BIG_ENDIAN,
};


static void pistorm_init(MachineState *machine)
{
    M68kCPU *cpu;
    CPUM68KState *env;

    MemoryRegion *address_space_mem = get_system_memory();
    MemoryRegion *ami = g_new(MemoryRegion, 1);

    cpu = M68K_CPU(cpu_create(machine->cpu_type));
    env = &cpu->env;

    printf("PiStorm QEMU Test\n");

    ps_setup_protocol();
    ps_reset_state_machine();
    ps_pulse_reset();
    printf("PiStorm GPIOs initalizedt\n");

    env->vbr = 0;

    memory_region_init_io(ami, NULL, &pistorm_ami_ops, NULL,"pistorm_mmio", 0xffffff);
    memory_region_add_subregion(address_space_mem, 0, ami);

    env->pc = 0;
    cpu_reset(CPU(cpu));
}

static void pistorm_machine_init(MachineClass *mc)
{
    mc->desc = "PiStorm";
    mc->init = pistorm_init;
    mc->default_cpu_type = M68K_CPU_TYPE_NAME("m68020");
    mc->default_ram_id = "pistorm.ram";
}

DEFINE_MACHINE("pistorm", pistorm_machine_init)

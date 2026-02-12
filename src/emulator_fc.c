// emulator_fc.c

#include "emulator_fc.h"
#include "log.h"

#ifdef PISTORM_KMOD
#include "gpio/ps_protocol.h"
#endif

// Current 3-bit FC value on the (emulated) bus (FC2..FC0)
uint32_t current_fc = 0;

// Internal: remember last full Musashi FC value (may include FLAG_S)
static uint32_t last_fc_full = 0;

// Internal mode state
static enum fc_mode fc_mode_state = FC_MODE_OFF;

void fc_set_mode(enum fc_mode mode)
{
    fc_mode_state = mode;

    switch (mode) {
    case FC_MODE_OFF:
        LOG_INFO("[FC] disabled\n");
        break;
    case FC_MODE_STUB:
        LOG_INFO("[FC] enabled (stub)\n");
        break;
    case FC_MODE_CPLD:
        LOG_INFO("[FC] enabled (cpld)\n");
        break;
    default:
        LOG_WARN("[FC] unknown mode %d, forcing off\n", mode);
        fc_mode_state = FC_MODE_OFF;
        break;
    }
}

enum fc_mode fc_get_mode(void)
{
    return fc_mode_state;
}

void cpu_set_fc(uint32_t fc)
{
    // Musashi passes FLAG_S | FUNCTION_CODE_xxx here.
    last_fc_full = fc;

    // Bus FC lines are the low three bits.
    uint8_t fc_val = (uint8_t)(fc & 0x7u);

    // No change, nothing to do.
    if (fc_val == current_fc) {
        return;
    }

    current_fc = fc_val;

    // FC logic disabled at emulator level, do not bother the CPLD.
    if (fc_mode_state == FC_MODE_OFF) {
        return;
    }

    // Drive the CPLD FC pins when enabled.
    if (fc_mode_state == FC_MODE_CPLD) {
#ifdef PISTORM_KMOD
        ps_fc_write(fc_val);
#endif
    }

#ifdef PISTORM_FC_TRACE
    // Decode for optional FC transition tracing.
    const char *space = "reserved";

    switch (fc_val) {
    case 1:
        space = "user-data";
        break;
    case 2:
        space = "user-prog";
        break;
    case 5:
        space = "super-data";
        break;
    case 6:
        space = "super-prog";
        break;
    case 7:
        space = "cpu-space";
        break;
    default:
        space = "reserved";
        break;
    }

    LOG_DEBUG("[FC] fc=%u -> %s (mode=%d)\n",
              (unsigned)fc_val, space, (int)fc_mode_state);
#endif
}

// Simple SFC/DFC helpers.
// For now they just mirror the current bus FC, with a sane default when FC
// handling is disabled. This matches the earlier behaviour and is enough
// to let MMU code ask “what space are we in?” without forcing FC on.

uint32_t get_current_sfc(void)
{
    // Default to supervisor data when FC handling is off.
    if (fc_mode_state == FC_MODE_OFF) {
        return 5u; // Supervisor Data
    }

    return current_fc;
}

uint32_t get_current_dfc(void)
{
    // Default to supervisor data when FC handling is off.
    if (fc_mode_state == FC_MODE_OFF) {
        return 5u; // Supervisor Data
    }

    return current_fc;
}

// tools/pistorm_smoke.c
#include <stdio.h>
#include <unistd.h>
#include "../src/gpio/ps_protocol_kmod.c"  // Include the shim directly for testing

int main() {
    printf("PiStorm Smoke Test\n");
    
    // Initialize the protocol
    printf("Setting up protocol...\n");
    if (ps_setup_protocol() < 0) {
        printf("ERROR: Failed to setup protocol\n");
        return 1;
    }
    printf("Protocol setup successful\n");
    
    // Pulse reset
    printf("Pulsing reset...\n");
    ps_pulse_reset();
    printf("Reset pulse completed\n");
    
    // Try a simple read (INTREQR register)
    printf("Reading INTREQR register (0xDFF01E)...\n");
    unsigned v = ps_read_16(0xDFF01E);
    printf("INTREQR read result: 0x%04x\n", v);
    
    // Try another register read (DMACONR)
    printf("Reading DMACONR register (0xDFF002)...\n");
    unsigned v2 = ps_read_16(0xDFF002);
    printf("DMACONR read result: 0x%04x\n", v2);
    
    printf("Smoke test completed successfully!\n");
    return 0;
}
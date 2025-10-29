#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

#define HW_REGS_BASE        0xff200000
#define HW_REGS_SPAN        0x00200000
#define RAM_BASE_OFFSET     0x00000000
#define RAM_SPAN_BYTES      19200

int main() {
    int fd;
    void *virtual_base;
    volatile uint8_t *ram_ptr;

    // Abrir /dev/mem
    if ((fd = open("/dev/mem", (O_RDWR | O_SYNC))) == -1) {
        printf("Erro ao abrir /dev/mem.\n");
        return 1;
    }

    // Mapeia o endereço físico
    virtual_base = mmap(NULL, HW_REGS_SPAN, (PROT_READ | PROT_WRITE), MAP_SHARED, fd, HW_REGS_BASE);
    if (virtual_base == MAP_FAILED) {
        printf("Erro ao mapear memória.\n");
        close(fd);
        return 1;
    }

    ram_ptr = (uint8_t *)(virtual_base + RAM_BASE_OFFSET);

    printf("Lendo conteúdo da RAM...\n");
    int i;
    for (i = 0; i < RAM_SPAN_BYTES; i++) {
        printf("RAM[%05d] = 0x%02X\n", i, ram_ptr[i]);
    }

    munmap(virtual_base, HW_REGS_SPAN);
    close(fd);
    return 0;
}


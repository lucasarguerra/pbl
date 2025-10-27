#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include "./hps_0.h"   // Gerado pelo sopc-create-header-files

#define LW_BRIDGE_BASE  0xFF200000
#define LW_BRIDGE_SPAN  0x00005000

int main(void)
{
    int fd;
    void *LW_virtual;
    volatile unsigned char *RAM_ptr; // ponteiro para RAM FPGA
    FILE *img;
    long img_size;
    unsigned char *buffer;

    // Abre o /dev/mem
    if ((fd = open("/dev/mem", (O_RDWR | O_SYNC))) == -1) {
        printf("ERRO: não foi possível abrir /dev/mem\n");
        return -1;
    }

    // Mapeia o barramento LW para memória virtual
    LW_virtual = mmap(NULL, LW_BRIDGE_SPAN, (PROT_READ | PROT_WRITE),
                      MAP_SHARED, fd, LW_BRIDGE_BASE);

    if (LW_virtual == MAP_FAILED) {
        printf("ERRO: mmap falhou\n");
        close(fd);
        return -1;
    }

    // Ponteiro para a memória on-chip (ajuste o nome conforme seu projeto)
    RAM_ptr = (unsigned char *)(LW_virtual + ONCHIP_MEMORY2_1_BASE);

    // Abre arquivo da imagem (por exemplo, "imagem.bin")
    img = fopen("imagem.bin", "rb");
    if (!img) {
        printf("ERRO: não foi possível abrir imagem.bin\n");
        munmap(LW_virtual, LW_BRIDGE_SPAN);
        close(fd);
        return -1;
    }

    // Descobre tamanho do arquivo
    fseek(img, 0, SEEK_END);
    img_size = ftell(img);
    rewind(img);

    // Aloca buffer
    buffer = (unsigned char *)malloc(img_size);
    if (!buffer) {
        printf("ERRO: memória insuficiente\n");
        fclose(img);
        munmap(LW_virtual, LW_BRIDGE_SPAN);
        close(fd);
        return -1;
    }

    // Lê imagem inteira
    fread(buffer, 1, img_size, img);
    fclose(img);

    printf("Carregando imagem (%ld bytes)...\n", img_size);

    // Copia para a RAM do FPGA
    for (long i = 0; i < img_size; i++) {
        RAM_ptr[i] = buffer[i];
    }

    printf("Imagem carregada na RAM do FPGA!\n");

    // Libera recursos
    free(buffer);
    munmap(LW_virtual, LW_BRIDGE_SPAN);
    close(fd);

    return 0;
}

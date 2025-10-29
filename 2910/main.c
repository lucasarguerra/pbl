#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include "./hps_0.h"

// Ponte HPS–FPGA
#define LW_BRIDGE_BASE 0xFF200000
#define LW_BRIDGE_SPAN  0x30000

// Memória da imagem (vem do Qsys)
#define IMAGE_MEM_BASE  ONCHIP_MEMORY2_1_BASE
#define IMAGE_MEM_SPAN  ONCHIP_MEMORY2_1_SPAN
#define IMAGE_MEM_SIZE  ONCHIP_MEMORY2_1_SIZE_VALUE

// Caminho da imagem .mif (ajuste aqui)
#define IMAGE_PATH "/home/aluno/TEC499/TP02/SirioeGuerra/imagem.mif"

int main() {
    FILE *file = NULL;
    unsigned char *img_buffer = NULL;
    int fd;
    void *lw_bridge_map = NULL;
    volatile unsigned char *image_mem_ptr;

    printf("=== Escritor de Imagem MIF ===\n");
    printf("Abrindo arquivo MIF: %s\n", IMAGE_PATH);

    file = fopen(IMAGE_PATH, "r");
    if (!file) {
        perror("Erro ao abrir arquivo MIF");
        return 1;
    }

    img_buffer = (unsigned char *)malloc(IMAGE_MEM_SIZE);
    if (!img_buffer) {
        perror("Erro ao alocar buffer");
        fclose(file);
        return 1;
    }

    // Lê valores hexadecimais do MIF
    char line[128];
    int index = 0;
    int value;
    while (fgets(line, sizeof(line), file)) {
        if (strstr(line, "CONTENT") || strstr(line, "BEGIN") || strstr(line, "END") ||
            strstr(line, "ADDRESS_RADIX") || strstr(line, "DATA_RADIX"))
            continue;

        if (sscanf(line, "%*x : %x", &value) == 1) {
            if (index < IMAGE_MEM_SIZE)
                img_buffer[index++] = (unsigned char)value;
        }
    }
    fclose(file);

    printf("Imagem MIF lida com sucesso (%d bytes)\n", index);

    // Mapeia a ponte
    fd = open("/dev/mem", O_RDWR | O_SYNC);
    if (fd == -1) {
        perror("Erro ao abrir /dev/mem");
        free(img_buffer);
        return 1;
    }

    lw_bridge_map = mmap(NULL, LW_BRIDGE_SPAN, PROT_READ | PROT_WRITE, MAP_SHARED, fd, LW_BRIDGE_BASE);
    if (lw_bridge_map == MAP_FAILED) {
        perror("Erro ao mapear LW Bridge");
        close(fd);
        free(img_buffer);
        return 1;
    }

    image_mem_ptr = (volatile unsigned char *)(lw_bridge_map + IMAGE_MEM_BASE);
    printf("Escrevendo imagem na RAM da FPGA (base 0x%X)...\n", IMAGE_MEM_BASE);

    memcpy((void *)image_mem_ptr, img_buffer, index);
    printf("Transferência concluída com sucesso!\n");

    printf("=== Fim da escrita. Imagem agora está na RAM da FPGA ===\n");

    // Mantém mapeado até reboot / reset do processo
    return 0;
}

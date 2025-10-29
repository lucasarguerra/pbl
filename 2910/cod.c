#include <stdio.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>

#define ONCHIP_BASE  0x00000000     // início da RAM
#define ONCHIP_SPAN  19200          // tamanho total (bytes)
#define HPS_BRIDGE_BASE  0xC0000000 // endereço base do bridge HPS-FPGA (ajuste se necessário)

// Caminho do arquivo da imagem (8 bits/pixel)
#define IMG_PATH "/home/root/imagem.raw"

int main() {
    int fd;
    void *virtual_base;
    volatile uint8_t *onchip_ptr;
    FILE *img_file;
    size_t bytes_lidos;

    printf("Iniciando escrita da imagem na RAM on-chip...\n");

    // Abre /dev/mem para acesso ao barramento físico
    fd = open("/dev/mem", (O_RDWR | O_SYNC));
    if (fd < 0) {
        perror("Erro ao abrir /dev/mem");
        return 1;
    }

    // Mapeia a memória física da RAM on-chip para o espaço de endereçamento do processo
    virtual_base = mmap(NULL, ONCHIP_SPAN, PROT_READ | PROT_WRITE, MAP_SHARED, fd, HPS_BRIDGE_BASE + ONCHIP_BASE);
    if (virtual_base == MAP_FAILED) {
        perror("Erro no mmap");
        close(fd);
        return 1;
    }

    onchip_ptr = (volatile uint8_t *) virtual_base;

    // Abre o arquivo da imagem
    img_file = fopen(IMG_PATH, "rb");
    if (!img_file) {
        perror("Erro ao abrir arquivo da imagem");
        munmap((void *)virtual_base, ONCHIP_SPAN);
        close(fd);
        return 1;
    }

    // Lê e grava byte a byte na RAM on-chip
    uint8_t pixel;
    size_t i = 0;
    while (fread(&pixel, sizeof(uint8_t), 1, img_file) == 1 && i < ONCHIP_SPAN) {
        onchip_ptr[i++] = pixel;
    }

    printf("Imagem escrita na RAM (%zu bytes gravados)\n", i);

    // Fecha tudo
    fclose(img_file);
    munmap((void *)virtual_base, ONCHIP_SPAN);
    close(fd);

    return 0;
}

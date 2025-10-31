#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include "./hps_0.h"

#define LW_BRIDGE_BASE 0xFF200000
#define LW_BRIDGE_SPAN 0x30000
#define IMAGE_MEM_BASE ONCHIP_MEMORY2_1_BASE
#define IMAGE_MEM_SPAN ONCHIP_MEMORY2_1_SPAN
#define IMAGE_MEM_SIZE ONCHIP_MEMORY2_1_SIZE_VALUE
#define CONTROL_PIO_BASE PIO_LED_BASE
#define IMAGE_PATH "/home/aluno/TEC499/TP02/SirioeGuerra/imagem.mif"
#define EXPECTED_IMG_WIDTH 160
#define EXPECTED_IMG_HEIGHT 120
#define EXPECTED_IMG_SIZE (EXPECTED_IMG_WIDTH * EXPECTED_IMG_HEIGHT)
#define ST_RESET 7
#define ST_REPLICACAO 0
#define ST_DECIMACAO 1
#define ST_ZOOMNN 2
#define ST_MEDIA 3
#define ST_COPIA_DIRETA 4
#define ST_REPLICACAO4 8
#define ST_DECIMACAO4 9
#define ST_ZOOMNN4 10
#define ST_MED4 11

volatile unsigned char *IMAGE_MEM_ptr = NULL;
volatile unsigned int *CONTROL_PIO_ptr = NULL;
int fd = -1;
void *LW_virtual = MAP_FAILED;
unsigned char *hps_img_buffer = NULL;

void exibirMenu() {
    printf("\n=== Escolha o Algoritmo de Processamento ===\n");
    printf("1 - Reset\n2 - Replicação 2x\n3 - Decimação 2x\n4 - Zoom NN 2x\n5 - Média 2x\n6 - Cópia Direta\n7 - Replicação 4x\n8 - Decimação 4x\n9 - Zoom NN 4x\n10 - Média 4x\n0 - Sair\nSelecione uma opção: ");
}

int obterCodigoEstado(int opcao) {
    switch (opcao) {
        case 1: return ST_RESET; case 2: return ST_REPLICACAO; case 3: return ST_DECIMACAO;
        case 4: return ST_ZOOMNN; case 5: return ST_MEDIA; case 6: return ST_COPIA_DIRETA;
        case 7: return ST_REPLICACAO4; case 8: return ST_DECIMACAO4; case 9: return ST_ZOOMNN4;
        case 10: return ST_MED4; default: return -1;
    }
}

int carregarImagemMIF(const char *path) {
    FILE *img_file = fopen(path, "r");
    if (!img_file) return -1;
    hps_img_buffer = malloc(IMAGE_MEM_SIZE);
    if (!hps_img_buffer) { fclose(img_file); return -1; }
    char line[128]; int index = 0, value;
    while (fgets(line, sizeof(line), img_file)) {
        if (strstr(line, "CONTENT") || strstr(line, "BEGIN") || strstr(line, "END") ||
            strstr(line, "ADDRESS_RADIX") || strstr(line, "DATA_RADIX") ||
            strstr(line, "WIDTH") || strstr(line, "DEPTH")) continue;
        if (sscanf(line, "%*x : %x", &value) == 1) {
            if (index < IMAGE_MEM_SIZE)
                hps_img_buffer[index++] = (unsigned char)value;
        }
    }
    fclose(img_file);
    return index;
}

int mapearPonte() {
    fd = open("/dev/mem", O_RDWR | O_SYNC);
    if (fd == -1) return -1;
    LW_virtual = mmap(NULL, LW_BRIDGE_SPAN, PROT_READ | PROT_WRITE, MAP_SHARED, fd, LW_BRIDGE_BASE);
    if (LW_virtual == MAP_FAILED) return -1;
    IMAGE_MEM_ptr = (volatile unsigned char *)(LW_virtual + IMAGE_MEM_BASE);
    CONTROL_PIO_ptr = (volatile unsigned int *)(LW_virtual + CONTROL_PIO_BASE);
    return 0;
}

void transferirImagemFPGA(int tamanho) {
    memcpy((void *)IMAGE_MEM_ptr, hps_img_buffer, tamanho);
}

int testarPIO() {
    unsigned int padroes_teste[] = {0x3FF, 0x000, 0x155, 0x2AA, 0x00F};
    int ok = 0;
    for (int n = 0; n < 5; n++) {
        *CONTROL_PIO_ptr = padroes_teste[n];
        asm volatile("" ::: "memory");
        usleep(1000);
        if (*CONTROL_PIO_ptr == padroes_teste[n]) ok++;
    }
    return ok;
}

void enviarComando(int codigo) {
    *CONTROL_PIO_ptr = codigo;
    asm volatile("" ::: "memory");
    usleep(10000);
}

void limparRecursos() {
    if (hps_img_buffer) free(hps_img_buffer);
    if (LW_virtual != MAP_FAILED) munmap(LW_virtual, LW_BRIDGE_SPAN);
    if (fd != -1) close(fd);
}

int main() {
    printf("=== Sistema de Processamento de Imagem HPS-FPGA ===\n");
    int bytes = carregarImagemMIF(IMAGE_PATH);
    if (bytes < 0) { perror("Erro ao carregar imagem"); return 1; }
    printf("Imagem carregada (%d bytes)\n", bytes);
    if (mapearPonte() < 0) { perror("Erro ao mapear ponte"); limparRecursos(); return 1; }
    transferirImagemFPGA(bytes);
    printf("Imagem transferida\n");
    int teste = testarPIO();
    if (teste == 0) { printf("Erro PIO\n"); limparRecursos(); return 1; }
    int opcao = -1;
    while (opcao != 0) {
        exibirMenu();
        if (scanf("%d", &opcao) != 1) { while (getchar() != '\n'); continue; }
        if (opcao == 0) break;
        int codigo = obterCodigoEstado(opcao);
        if (codigo < 0) continue;
        enviarComando(codigo);
        usleep(500000);
    }
    limparRecursos();
    printf("Encerrado\n");
    return 0;
}

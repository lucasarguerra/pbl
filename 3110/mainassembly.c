#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include "./hps_0.h"

#define IMAGE_PATH "/home/aluno/TEC499/TP02/SirioeGuerra/imagem.mif"
#define EXPECTED_IMG_WIDTH 160
#define EXPECTED_IMG_HEIGHT 120
#define EXPECTED_IMG_SIZE (EXPECTED_IMG_WIDTH * EXPECTED_IMG_HEIGHT)

const unsigned int IMAGE_MEM_BASE_VAL = ONCHIP_MEMORY2_1_BASE;
const unsigned int CONTROL_PIO_BASE_VAL = PIO_LED_BASE;

volatile unsigned char *IMAGE_MEM_ptr = NULL;
volatile unsigned int *CONTROL_PIO_ptr = NULL;
int fd = -1;
void *LW_virtual = MAP_FAILED;
unsigned char *hps_img_buffer = NULL;

extern int carregarImagemMIF(const char *path);
extern int mapearPonte(void);
extern void transferirImagemFPGA(int tamanho);
extern void enviarComando(int codigo);
extern void limparRecursos(void);
extern int obterCodigoEstado(int opcao);

void exibirMenu() {
    printf("\n=== Escolha o Algoritmo de Processamento ===\n");
    printf("1 - Reset\n");
    printf("2 - Replicação 2x\n");
    printf("3 - Decimação 2x\n");
    printf("4 - Zoom NN 2x\n");
    printf("5 - Média 2x\n");
    printf("6 - Cópia Direta\n");
    printf("7 - Replicação 4x\n");
    printf("8 - Decimação 4x\n");
    printf("9 - Zoom NN 4x\n");
    printf("10 - Média 4x\n");
    printf("0 - Sair\n");
    printf("Selecione uma opção: ");
}

int main() {
    printf("=== Sistema de Processamento de Imagem HPS-FPGA ===\n");
    
    int bytes = carregarImagemMIF(IMAGE_PATH);
    if (bytes < 0) {
        perror("Erro ao carregar imagem");
        return 1;
    }
    
    // Mapeia a ponte lightweight usando a função assembly
    if (mapearPonte() < 0) {
        perror("Erro ao mapear ponte lightweight");
        limparRecursos();
        return 1;
    }
    
    transferirImagemFPGA(bytes);
    
    int opcao = -1;
    while (opcao != 0) {
        exibirMenu();
        
        if (scanf("%d", &opcao) != 1) {
            while (getchar() != '\n'); // Limpa buffer
            printf("Entrada inválida!\n");
            continue;
        }
        
        if (opcao == 0) {
            break;
        }
        
        int codigo = obterCodigoEstado(opcao);
        if (codigo < 0) {
            printf("Opção inválida!\n");
            continue;
        }
        
        // Envia comando usando a função assembly
        enviarComando(codigo);
        
    }
    
    // Limpeza final
    limparRecursos();
    printf("Sistema encerrado\n");
    
    return 0;
}

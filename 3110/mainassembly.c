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

// Exporta constantes como variáveis globais para o assembly
const unsigned int IMAGE_MEM_BASE_VAL = ONCHIP_MEMORY2_1_BASE;
const unsigned int CONTROL_PIO_BASE_VAL = PIO_LED_BASE;

volatile unsigned char *IMAGE_MEM_ptr = NULL;
volatile unsigned int *CONTROL_PIO_ptr = NULL;
int fd = -1;
void *LW_virtual = MAP_FAILED;
unsigned char *hps_img_buffer = NULL;

// Declarações das funções assembly
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
    
    // Carrega a imagem MIF usando a função assembly
    int bytes = carregarImagemMIF(IMAGE_PATH);
    if (bytes < 0) {
        perror("Erro ao carregar imagem");
        return 1;
    }
    printf("Imagem carregada com sucesso (%d bytes)\n", bytes);
    
    // Mapeia a ponte lightweight usando a função assembly
    if (mapearPonte() < 0) {
        perror("Erro ao mapear ponte lightweight");
        limparRecursos();
        return 1;
    }
    printf("Ponte mapeada com sucesso\n");
    
    // Transfere a imagem para o FPGA usando a função assembly
    transferirImagemFPGA(bytes);
    printf("Imagem transferida para o FPGA\n");
    
    // Testa a comunicação com o PIO
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
        printf("Enviando comando %d...\n", codigo);
        enviarComando(codigo);
        
        // Aguarda processamento
        usleep(100000); // 500ms
        printf("Comando executado\n");
    }
    
    // Limpeza final
    limparRecursos();
    printf("Sistema encerrado\n");
    
    return 0;
}

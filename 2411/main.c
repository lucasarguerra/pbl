#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include "./hps_0.h"
#include "api.h"

#define IMAGE_PATH "/home/aluno/TEC499/TP02/SirioeGuerra/imagem.mif"

// Limites da imagem base
#define IMG_W 160
#define IMG_H 120

// Comandos
#define CMD_PINTAR 1

// Ponteiro local para a ponte lightweight
static void* ponte_lw = NULL;

void exibirMenu() {
    printf("\n=== Menu Principal ===\n");
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
    printf("11 - Pintar Bloco\n");
    printf("0 - Sair\n");
    printf("Selecione uma opção: ");
}

void exibirGridBlocos() {
    printf("\n=== Grid de Blocos (8x6) ===\n");
    printf("Cada bloco tem 20x20 pixels\n");
    printf("┌────┬────┬────┬────┬────┬────┬────┬────┐\n");
    for (int row = 0; row < 6; row++) {
        printf("│");
        for (int col = 0; col < 8; col++) {
            printf(" %02d │", row * 8 + col);
        }
        printf("\n");
        if (row < 5) {
            printf("├────┼────┼────┼────┼────┼────┼────┼────┤\n");
        }
    }
    printf("└────┴────┴────┴────┴────┴────┴────┴────┘\n");
    printf("Total: 48 blocos (0 a 47)\n");
}

int mapearPontePintura() {
    int fd = open("/dev/mem", O_RDWR | O_SYNC);
    if (fd < 0) {
        perror("Erro ao abrir /dev/mem para pintura");
        return -1;
    }

    // Base da ponte lightweight HPS-to-FPGA
    ponte_lw = mmap(NULL, 0x200000, PROT_READ | PROT_WRITE, 
                    MAP_SHARED, fd, 0xFF200000);
    
    close(fd);
    
    if (ponte_lw == MAP_FAILED) {
        perror("Erro ao mapear ponte lightweight");
        ponte_lw = NULL;
        return -1;
    }
    
    return 0;
}

void escreverPIO(unsigned int offset, unsigned int valor) {
    if (ponte_lw != NULL) {
        volatile unsigned int *pio = (volatile unsigned int*)(ponte_lw + offset);
        *pio = valor;
    }
}

unsigned int lerPIO(unsigned int offset) {
    if (ponte_lw != NULL) {
        volatile unsigned int *pio = (volatile unsigned int*)(ponte_lw + offset);
        return *pio;
    }
    return 0;
}

void pintarRetangulo(int x1, int y1, int x2, int y2) {
    // Valida coordenadas
    if (x1 < 0) x1 = 0;
    if (y1 < 0) y1 = 0;
    if (x2 >= IMG_W) x2 = IMG_W - 1;
    if (y2 >= IMG_H) y2 = IMG_H - 1;
    
    if (x1 > x2 || y1 > y2) {
        printf("Erro: Coordenadas inválidas!\n");
        return;
    }

    printf("Pintando retângulo: (%d,%d) até (%d,%d)\n", x1, y1, x2, y2);

    // Escreve coordenadas nos PIOs usando os offsets do hps_0.h
    escreverPIO(PIO_X1_BASE, x1);
    escreverPIO(PIO_Y1_BASE, y1);
    escreverPIO(PIO_X2_BASE, x2);
    escreverPIO(PIO_Y2_BASE, y2);

    // Dispara comando de pintura
    escreverPIO(PIO_CMD_BASE, CMD_PINTAR);

    // Aguarda processamento
    sleep(1); // 1 segundo

    // Limpa comando
    escreverPIO(PIO_CMD_BASE, 0);

    printf("Bloco pintado!\n");
}

void menuPintura() {
    int opcao;
    
    printf("\n=== Menu de Pintura ===\n");
    printf("1 - Pintar por Coordenadas\n");
    printf("2 - Pintar por Grid (Blocos)\n");
    printf("3 - Voltar\n");
    printf("Escolha: ");
    
    if (scanf("%d", &opcao) != 1) {
        while (getchar() != '\n');
        return;
    }
    
    if (opcao == 1) {
        // Pintar por coordenadas
        int x1, y1, x2, y2;
        
        printf("\nImagem: %dx%d pixels\n", IMG_W, IMG_H);
        printf("Coordenadas válidas: X[0-%d], Y[0-%d]\n\n", IMG_W-1, IMG_H-1);
        
        printf("X1 (inicial): ");
        scanf("%d", &x1);
        printf("Y1 (inicial): ");
        scanf("%d", &y1);
        printf("X2 (final): ");
        scanf("%d", &x2);
        printf("Y2 (final): ");
        scanf("%d", &y2);
        
        pintarRetangulo(x1, y1, x2, y2);
        
    } else if (opcao == 2) {
        // Pintar por grid
        exibirGridBlocos();
        
        int bloco;
        printf("\nNúmero do bloco (0-47): ");
        scanf("%d", &bloco);
        
        if (bloco >= 0 && bloco <= 47) {
            int col = bloco % 8;
            int row = bloco / 8;
            
            int x1 = col * 20;
            int y1 = row * 20;
            int x2 = x1 + 19;
            int y2 = y1 + 19;
            
            printf("Bloco %d -> Posição [%d,%d]\n", bloco, row, col);
            pintarRetangulo(x1, y1, x2, y2);
        } else {
            printf("Bloco inválido!\n");
        }
    }
}

int main() {
    // Inicializa os valores base (do hps_0.h)
    IMAGE_MEM_BASE_VAL = ONCHIP_MEMORY2_1_BASE;
    CONTROL_PIO_BASE_VAL = PIO_LED_BASE;
    
    // Carrega a imagem MIF usando a função assembly
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
    
    // Mapeia ponte para pintura (separado)
    if (mapearPontePintura() < 0) {
        perror("Erro ao mapear ponte para pintura");
        limparRecursos();
        return 1;
    }
    
    // Transfere a imagem para o FPGA usando a função assembly
    transferirImagemFPGA(bytes);
    
    printf("\n=== Sistema Inicializado ===\n");
    printf("Imagem: %d bytes (%dx%d)\n", bytes, IMG_W, IMG_H);
    
    // Loop do menu
    int opcao = -1;
    while (opcao != 0) {
        exibirMenu();
        
        if (scanf("%d", &opcao) != 1) {
            while (getchar() != '\n');
            printf("Entrada inválida!\n");
            continue;
        }
        
        if (opcao == 0) {
            printf("Encerrando...\n");
            break;
        }
        
        if (opcao == 11) {
            // Menu de pintura
            menuPintura();
        } else {
            // Comandos de processamento normais
            int codigo = obterCodigoEstado(opcao);
            if (codigo < 0) {
                printf("Opção inválida!\n");
                continue;
            }
            
            enviarComando(codigo);
        }
    }
    
    // Limpeza final
    if (ponte_lw != NULL) {
        munmap(ponte_lw, 0x200000);
    }
    limparRecursos();
    printf("Sistema encerrado!\n");
    
    return 0;
}

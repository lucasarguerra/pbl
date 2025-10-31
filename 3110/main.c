#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include "./hps_0.h"

// --- Configurações da Ponte e Memória ---
#define LW_BRIDGE_BASE 0xFF200000
#define LW_BRIDGE_SPAN 0x30000

// Memória de Imagem Fonte
#define IMAGE_MEM_BASE ONCHIP_MEMORY2_1_BASE
#define IMAGE_MEM_SPAN ONCHIP_MEMORY2_1_SPAN
#define IMAGE_MEM_SIZE ONCHIP_MEMORY2_1_SIZE_VALUE

// PIO - Usando apenas PIO_LED (OUTPUT, 10 bits)
#define CONTROL_PIO_BASE PIO_LED_BASE  // 0x8010

// Caminho da imagem .mif
#define IMAGE_PATH "/home/aluno/TEC499/TP02/SirioeGuerra/imagem.mif"

// Dimensões esperadas da imagem
#define EXPECTED_IMG_WIDTH 160
#define EXPECTED_IMG_HEIGHT 120
#define EXPECTED_IMG_SIZE (EXPECTED_IMG_WIDTH * EXPECTED_IMG_HEIGHT)

// Estados do hardware (códigos de controle) - bits [3:0]
#define ST_RESET         7
#define ST_REPLICACAO    0
#define ST_DECIMACAO     1
#define ST_ZOOMNN        2
#define ST_MEDIA         3
#define ST_COPIA_DIRETA  4
#define ST_REPLICACAO4   8
#define ST_DECIMACAO4    9
#define ST_ZOOMNN4       10
#define ST_MED4          11

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

int obterCodigoEstado(int opcao) {
    switch (opcao) {
        case 1: return ST_RESET;
        case 2: return ST_REPLICACAO;
        case 3: return ST_DECIMACAO;
        case 4: return ST_ZOOMNN;
        case 5: return ST_MEDIA;
        case 6: return ST_COPIA_DIRETA;
        case 7: return ST_REPLICACAO4;
        case 8: return ST_DECIMACAO4;
        case 9: return ST_ZOOMNN4;
        case 10: return ST_MED4;
        default: return -1;
    }
}

int main() {
    // --- Ponteiros para Hardware ---
    volatile unsigned char *IMAGE_MEM_ptr = NULL;
    volatile unsigned int *CONTROL_PIO_ptr = NULL;
    
    int fd = -1;
    void *LW_virtual = MAP_FAILED;
    unsigned char *hps_img_buffer = NULL;
    FILE *img_file = NULL;

    printf("=== Sistema de Processamento de Imagem HPS-FPGA ===\n");
    
    // --- Verificações de Endereço ---
    printf("\n[DEBUG] Configuração de Endereços:\n");
    printf("  LW_BRIDGE_BASE: 0x%08X\n", LW_BRIDGE_BASE);
    printf("  IMAGE_MEM_BASE: 0x%08X\n", IMAGE_MEM_BASE);
    printf("  CONTROL_PIO_BASE (PIO_LED): 0x%08X\n", CONTROL_PIO_BASE);
    printf("  IMAGE_MEM_SIZE: %d bytes\n", IMAGE_MEM_SIZE);
    
    if (IMAGE_MEM_BASE == 0x0) {
        printf("ALERTA: O offset base da memoria onchip_memory2_1 é 0x0.\n");
        printf("        Verifique Qsys Address Map para conflitos!\n");
    }

    // --- Passo 1: Ler Arquivo de Imagem MIF ---
    printf("\n[PASSO 1] Carregando imagem MIF...\n");
    printf("Abrindo arquivo: %s\n", IMAGE_PATH);
    
    img_file = fopen(IMAGE_PATH, "r");
    if (img_file == NULL) {
        perror("ERRO ao abrir arquivo MIF");
        goto cleanup;
    }

    hps_img_buffer = (unsigned char *)malloc(IMAGE_MEM_SIZE);
    if (hps_img_buffer == NULL) {
        perror("ERRO ao alocar buffer");
        goto cleanup;
    }

    // Lê valores hexadecimais do arquivo MIF
    char line[128];
    int index = 0;
    int value;
    
    while (fgets(line, sizeof(line), img_file)) {
        // Ignora linhas de cabeçalho
        if (strstr(line, "CONTENT") || strstr(line, "BEGIN") || strstr(line, "END") ||
            strstr(line, "ADDRESS_RADIX") || strstr(line, "DATA_RADIX") || 
            strstr(line, "WIDTH") || strstr(line, "DEPTH"))
            continue;
        
        // Lê pares endereço : valor
        if (sscanf(line, "%*x : %x", &value) == 1) {
            if (index < IMAGE_MEM_SIZE)
                hps_img_buffer[index++] = (unsigned char)value;
        }
    }
    fclose(img_file);
    img_file = NULL;

    if (index != EXPECTED_IMG_SIZE) {
        fprintf(stderr, "AVISO: Tamanho do arquivo MIF (%d bytes) diferente do esperado (%d bytes).\n", 
                index, EXPECTED_IMG_SIZE);
    }
    printf("✓ Imagem MIF lida com sucesso: %d bytes\n", index);

    // --- Passo 2: Mapear a Ponte HPS-FPGA ---
    printf("\n[PASSO 2] Mapeando ponte Lightweight HPS-FPGA...\n");
    
    fd = open("/dev/mem", O_RDWR | O_SYNC);
    if (fd == -1) {
        perror("ERRO ao abrir /dev/mem");
        goto cleanup;
    }

    LW_virtual = mmap(NULL, LW_BRIDGE_SPAN, PROT_READ | PROT_WRITE, MAP_SHARED, fd, LW_BRIDGE_BASE);
    if (LW_virtual == MAP_FAILED) {
        perror("ERRO ao mapear LW Bridge");
        goto cleanup;
    }
    printf("✓ Ponte mapeada em: %p\n", LW_virtual);

    // Calcula os ponteiros virtuais
    IMAGE_MEM_ptr = (volatile unsigned char *)(LW_virtual + IMAGE_MEM_BASE);
    CONTROL_PIO_ptr = (volatile unsigned int *)(LW_virtual + CONTROL_PIO_BASE);

    printf("  Ponteiro Memoria Imagem (offset 0x%X): %p\n", IMAGE_MEM_BASE, IMAGE_MEM_ptr);
    printf("  Ponteiro PIO_LED Controle (offset 0x%X): %p\n", CONTROL_PIO_BASE, CONTROL_PIO_ptr);

    // --- Passo 3: Escrever Imagem na RAM da FPGA ---
    printf("\n[PASSO 3] Transferindo imagem para RAM da FPGA...\n");
    printf("Transferindo %d bytes para offset 0x%X...\n", index, IMAGE_MEM_BASE);
    
    memcpy((void *)IMAGE_MEM_ptr, hps_img_buffer, index);
    printf("✓ Imagem transferida para a FPGA!\n");

    // --- Passo 4: Teste do PIO ---
    printf("\n[PASSO 4] Testando comunicação com PIO_LED...\n");
    
    // Lê valor inicial do PIO
    unsigned int pio_inicial = *CONTROL_PIO_ptr;
    printf("  PIO_LED inicial: %u (0b", pio_inicial);
    int k;
    for (k = 9; k >= 0; k--) printf("%d", (pio_inicial >> k) & 1);
    printf(")\n");
    
    // TESTE DE ESCRITA/LEITURA
    printf("\n[TESTE CRÍTICO] Verificando se PIO aceita escritas...\n");
    printf("  Tentando escrever padrões de teste:\n");
    
    unsigned int padroes_teste[] = {0x3FF, 0x000, 0x155, 0x2AA, 0x00F};
    int teste_passou = 0;
    int n;
    
    for (n = 0; n < 5; n++) {
        *CONTROL_PIO_ptr = padroes_teste[n];
        asm volatile("" ::: "memory");
        usleep(1000);
        
        unsigned int lido = *CONTROL_PIO_ptr;
        printf("    Escrito: 0x%03X → Lido: 0x%03X", padroes_teste[n], lido);
        
        if (lido == padroes_teste[n]) {
            printf(" ✓ OK\n");
            teste_passou++;
        } else {
            printf(" ✗ FALHOU\n");
        }
    }
    
    if (teste_passou == 0) {
        printf("\n  ⚠️  ERRO CRÍTICO: PIO NÃO aceita escritas!\n");
        printf("  → O PIO está configurado como INPUT no Platform Designer.\n");
        printf("  → SOLUÇÃO: Abra Platform Designer e mude para OUTPUT.\n");
        printf("  → Depois recompile o projeto e reprograme a FPGA.\n");
        printf("\n  Programa será encerrado.\n");
        goto cleanup;
    } else if (teste_passou < 5) {
        printf("\n  ⚠️  AVISO: Algumas escritas falharam (%d/5 OK).\n", teste_passou);
        printf("  → Pode haver problema de sincronização.\n");
    } else {
        printf("\n  ✓ PIO funcionando corretamente! (%d/5 testes OK)\n", teste_passou);
    }

    // --- Passo 5: Menu Interativo de Controle ---
    printf("\n[PASSO 5] Sistema pronto para processamento!\n");
    printf("INFO: Usando PIO_LED [3:0] para enviar comandos ao hardware.\n");
    
    int opcao = -1;
    while (opcao != 0) {
        exibirMenu();
        
        if (scanf("%d", &opcao) != 1) {
            // Limpa buffer em caso de entrada inválida
            while (getchar() != '\n');
            printf("Entrada inválida. Tente novamente.\n");
            continue;
        }

        if (opcao == 0) {
            printf("\n>>> Encerrando programa...\n");
            break;
        }

        int codigo = obterCodigoEstado(opcao);
        if (codigo >= 0) {
            printf("\n>>> Processando algoritmo selecionado...\n");
            
            // Mostra o código que será enviado
            printf("[COMANDO] Código: %d (0b", codigo);
            int i;
            for (i = 3; i >= 0; i--) printf("%d", (codigo >> i) & 1);
            printf(")\n");

            // Escreve código no PIO_LED
            printf("[ENVIO] Escrevendo no PIO_LED (offset 0x%X)...\n", CONTROL_PIO_BASE);
            *CONTROL_PIO_ptr = codigo;
            
            // Força sincronização de memória
            asm volatile("" ::: "memory");
            usleep(10000); // 10ms para estabilizar

            // Verifica escrita (múltiplas leituras)
            printf("[VERIFICAÇÃO] Lendo PIO_LED:\n");
            int j;
            int sucesso = 0;
            for (j = 0; j < 5; j++) {
                unsigned int pio_lido = *CONTROL_PIO_ptr;
                unsigned int bits_comando = pio_lido & 0x0F; // Extrai bits [3:0]
                
                printf("  Leitura %d: %u (0b", j+1, pio_lido);
                int m;
                for (m = 9; m >= 0; m--) printf("%d", (pio_lido >> m) & 1);
                printf(") -> [3:0] = %u\n", bits_comando);
                
                if (bits_comando == codigo) {
                    printf("    ✓ Comando recebido corretamente!\n");
                    sucesso = 1;
                    break;
                }
                usleep(5000);
            }
            
            if (!sucesso) {
                printf("    ✗ AVISO: Hardware pode não ter recebido o comando corretamente.\n");
            }
            
            // Aguarda processamento do hardware
            printf("[PROCESSAMENTO] Aguardando conclusão (500ms)...\n");
            usleep(500000);
            printf("  ✓ Processamento concluído!\n");
            
        } else {
            printf(">>> Opção inválida.\n");
        }
    }

cleanup:
    printf("\n=== Limpeza e Encerramento ===\n");
    
    // Libera o buffer da imagem
    if (hps_img_buffer != NULL) {
        free(hps_img_buffer);
        printf("✓ Buffer da imagem liberado.\n");
    }
    
    // Desmapeia a memória
    if (LW_virtual != MAP_FAILED) {
        if (munmap(LW_virtual, LW_BRIDGE_SPAN) != 0) {
            perror("ERRO ao desmapear memória");
        } else {
            printf("✓ Memória desmapeada.\n");
        }
    }
    
    // Fecha /dev/mem
    if (fd != -1) {
        close(fd);
        printf("✓ /dev/mem fechado.\n");
    }
    
    // Fecha arquivo se ainda estiver aberto
    if (img_file != NULL) {
        fclose(img_file);
    }

    printf("\n=== Programa Concluído ===\n");
    return 0;
}

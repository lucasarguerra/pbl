#include <stdio.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include "./hps_0.h"   // Arquivo gerado pelo Platform Designer

// Endereço base do barramento Lightweight (HPS–FPGA)
#define LW_BRIDGE_BASE  0xFF200000
#define LW_BRIDGE_SPAN  0x00005000

int main(void)
{
    int fd;
    void *LW_virtual;
    volatile int *SELETOR_ptr;

    // Abre o /dev/mem para acesso direto ao barramento de periféricos
    if ((fd = open("/dev/mem", (O_RDWR | O_SYNC))) == -1) {
        printf("ERRO: não foi possível abrir /dev/mem\n");
        return -1;
    }

    // Mapeia o barramento LW para o espaço de endereçamento do usuário
    LW_virtual = mmap(NULL, LW_BRIDGE_SPAN, (PROT_READ | PROT_WRITE),
                      MAP_SHARED, fd, LW_BRIDGE_BASE);

    if (LW_virtual == MAP_FAILED) {
        printf("ERRO: mmap falhou\n");
        close(fd);
        return -1;
    }

    /*
     * Pega o endereço virtual do PIO que controla o seletor da ULA.
     * No seu arquivo hps_0.h não há PIO_SELETOR, mas sim PIO_SW e PIO_LED.
     * Se o seletor da ULA estiver conectado ao pio_sw, usamos PIO_SW_BASE.
     * (Altere se o seletor estiver em outro PIO, como pio_seletor).
     */
    SELETOR_ptr = (int *)(LW_virtual + PIO_SW_BASE);

    // Menu de seleção
    int opcao;
    printf("Selecione o algoritmo da ULA:\n");
    printf(" 0x0 - Replicação 2x\n");
    printf(" 0x1 - Decimação 2x\n");
    printf(" 0x2 - Vizinho mais próximo 2x\n");
    printf(" 0x3 - Média de blocos 2x\n");
    printf(" 0x4 - Cópia direta (imagem original)\n");
    printf(" 0x8 - Replicação 4x\n");
    printf(" 0x9 - Decimação 4x\n");
    printf(" 0xA - Vizinho mais próximo 4x\n");
    printf(" 0xB - Média de blocos 4x\n");
    printf("Digite o código em hexadecimal (0x0 a 0xB): ");
    scanf("%x", &opcao);

    // Envia o valor para o PIO que controla o seletor da ULA
    *SELETOR_ptr = opcao & 0xF;

    printf("\nAlgoritmo %X enviado para a ULA!\n", opcao & 0xF);

    // Desfaz o mapeamento
    if (munmap(LW_virtual, LW_BRIDGE_SPAN) != 0) {
        printf("ERRO: munmap falhou\n");
        close(fd);
        return -1;
    }

    close(fd);
    return 0;
}

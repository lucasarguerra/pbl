#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <termios.h>  // <-- Necessário para leitura sem Enter
#include "./hps_0.h"
#include "api.h"

#define IMAGE_PATH "/home/aluno/TEC499/TP02/SirioeGuerra/imagem.mif"

// Função auxiliar para ler tecla sem precisar de Enter
char lerTecla() {
    struct termios oldt, newt;
    char ch;
    tcgetattr(STDIN_FILENO, &oldt);
    newt = oldt;
    newt.c_lflag &= ~(ICANON | ECHO); // Desativa modo canônico e eco
    tcsetattr(STDIN_FILENO, TCSANOW, &newt);
    ch = getchar();
    tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
    return ch;
}

void exibirControles() {
    printf("\n=== Controles ===\n");
    printf("[+] Zoom In\n");
    printf("[-] Zoom Out\n");
    printf("[0] Sair\n");
}

int main() {
    IMAGE_MEM_BASE_VAL = ONCHIP_MEMORY2_1_BASE;
    CONTROL_PIO_BASE_VAL = PIO_LED_BASE;

    int bytes = carregarImagemMIF(IMAGE_PATH);
    if (bytes < 0) {
        perror("Erro ao carregar imagem");
        return 1;
    }

    if (mapearPonte() < 0) {
        perror("Erro ao mapear ponte lightweight");
        limparRecursos();
        return 1;
    }

    transferirImagemFPGA(bytes);

    // =======================
    // Escolhas iniciais
    // =======================
    int zoomInAlg, zoomOutAlg;

    printf("\nEscolha o algoritmo de ZOOM IN:\n");
    printf("1 - Replicação de pixels\n");
    printf("2 - Vizinho mais próximo\n");
    printf("Seleção: ");
    scanf("%d", &zoomInAlg);

    printf("\nEscolha o algoritmo de ZOOM OUT:\n");
    printf("1 - Decimação\n");
    printf("2 - Média de blocos\n");
    printf("Seleção: ");
    scanf("%d", &zoomOutAlg);

    // =======================
    // Sequência inicial
    // =======================
    int estadoAtual = ST_COPIA_DIRETA;
    enviarComando(estadoAtual);
    printf("\n🖼️ Iniciando com Cópia Direta\n");

    exibirControles();

    while (1) {
        printf("\nDigite comando (+, -, 0): ");
        char comando = lerTecla();
        printf("%c\n", comando); // Mostra tecla pressionada

        if (comando == '0') break;

        int proximoEstado = estadoAtual;

        if (comando == '+') {
            switch (estadoAtual) {
                case ST_DECIMACAO4:
                case ST_MED4:
                    proximoEstado = (zoomOutAlg == 1) ? ST_DECIMACAO : ST_MEDIA;
                    break;
                case ST_DECIMACAO:
                case ST_MEDIA:
                    proximoEstado = ST_COPIA_DIRETA;
                    break;
                case ST_COPIA_DIRETA:
                    proximoEstado = (zoomInAlg == 1) ? ST_REPLICACAO : ST_ZOOMNN;
                    break;
                case ST_REPLICACAO:
                case ST_ZOOMNN:
                    proximoEstado = (zoomInAlg == 1) ? ST_REPLICACAO4 : ST_ZOOMNN4;
                    break;
                default:
                    printf("🚫 Já está no zoom máximo!\n");
                    continue;
            }
        } 
        else if (comando == '-') {
            switch (estadoAtual) {
                case ST_REPLICACAO4:
                case ST_ZOOMNN4:
                    proximoEstado = (zoomInAlg == 1) ? ST_REPLICACAO : ST_ZOOMNN;
                    break;
                case ST_REPLICACAO:
                case ST_ZOOMNN:
                    proximoEstado = ST_COPIA_DIRETA;
                    break;
                case ST_COPIA_DIRETA:
                    proximoEstado = (zoomOutAlg == 1) ? ST_DECIMACAO : ST_MEDIA;
                    break;
                case ST_DECIMACAO:
                case ST_MEDIA:
                    proximoEstado = (zoomOutAlg == 1) ? ST_DECIMACAO4 : ST_MED4;
                    break;
                default:
                    printf("🚫 Já está no zoom mínimo!\n");
                    continue;
            }
        } 
        else {
            printf("Entrada inválida.\n");
            continue;
        }

        // ==============================
        // 🚫 BLOQUEIO DE TRANSIÇÃO DIRETA
        // ==============================
        if ((estadoAtual == ST_REPLICACAO4 || estadoAtual == ST_ZOOMNN4) &&
            (proximoEstado == ST_DECIMACAO4 || proximoEstado == ST_MED4)) {
            printf("⚠️ Transição direta de Zoom In 4x para Zoom Out 4x não permitida!\n");
            continue;
        }

        if ((estadoAtual == ST_DECIMACAO4 || estadoAtual == ST_MED4) &&
            (proximoEstado == ST_REPLICACAO4 || proximoEstado == ST_ZOOMNN4)) {
            printf("⚠️ Transição direta de Zoom Out 4x para Zoom In 4x não permitida!\n");
            continue;
        }

        estadoAtual = proximoEstado;
        enviarComando(estadoAtual);

        printf("✅ Mudou para estado %d\n", estadoAtual);
    }

    limparRecursos();
    printf("\nSistema encerrado!\n");
    return 0;
}

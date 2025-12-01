#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <pthread.h>
#include <linux/input.h>
#include <errno.h>
#include <sys/ioctl.h>
#include "./hps_0.h"
#include "api.h"

#define IMAGE_PATH "/home/aluno/TEC499/TP02/SirioeGuerra/imagem.mif"
#define CURSOR_SIZE 2           // Tamanho do cursor em pixels da imagem 160x120
#define IMG_WIDTH 160           // Largura da imagem original
#define IMG_HEIGHT 120          // Altura da imagem original
#define SCREEN_WIDTH 640        // Largura da tela (para mapear mouse)
#define SCREEN_HEIGHT 480       // Altura da tela (para mapear mouse)
#define CURSOR_COLOR 0xFF       // Branco - cursor

// ============================================================================
// ESTRUTURAS E VARIÁVEIS GLOBAIS
// ============================================================================

typedef struct {
    int x_screen;               // Posição X na tela 640x480
    int y_screen;               // Posição Y na tela 640x480
    int x_img;                  // Posição X na imagem 160x120
    int y_img;                  // Posição Y na imagem 160x120
    int button_left;
    int button_right;
    pthread_mutex_t lock;
} CursorState;

typedef struct {
    int x_inicio;
    int y_inicio;
    int x_fim;
    int y_fim;
    int ativa;
    int arrastando;
} SelecaoRegiao;

static CursorState cursor = {
    .x_screen = SCREEN_WIDTH / 2,
    .y_screen = SCREEN_HEIGHT / 2,
    .x_img = IMG_WIDTH / 2,
    .y_img = IMG_HEIGHT / 2,
    .button_left = 0,
    .button_right = 0,
    .lock = PTHREAD_MUTEX_INITIALIZER
};

static SelecaoRegiao selecao = {
    .x_inicio = 0,
    .y_inicio = 0,
    .x_fim = 0,
    .y_fim = 0,
    .ativa = 0,
    .arrastando = 0
};

volatile int programa_rodando = 1;
volatile int modo_selecao = 0;           // Ativo quando está em cópia direta
volatile int mostrar_cursor = 0;         // 1 = desenha cursor, 0 = não desenha
pthread_t thread_mouse, thread_desenho;
unsigned char* imagem_original = NULL;   // Imagem atual de trabalho
unsigned char* imagem_backup = NULL;     // Backup ORIGINAL (nunca muda)
unsigned char* fpga_framebuffer = NULL;  // Ponteiro para RAM compartilhada
int imagem_carregada = 0;

// ============================================================================
// FUNÇÕES DE CONVERSÃO DE COORDENADAS
// ============================================================================

void atualizarCoordenadaImagem() {
    // Converte coordenadas da tela 640x480 para imagem 160x120
    cursor.x_img = (cursor.x_screen * IMG_WIDTH) / SCREEN_WIDTH;
    cursor.y_img = (cursor.y_screen * IMG_HEIGHT) / SCREEN_HEIGHT;
    
    // Garante limites
    if (cursor.x_img < 0) cursor.x_img = 0;
    if (cursor.x_img >= IMG_WIDTH) cursor.x_img = IMG_WIDTH - 1;
    if (cursor.y_img < 0) cursor.y_img = 0;
    if (cursor.y_img >= IMG_HEIGHT) cursor.y_img = IMG_HEIGHT - 1;
}

// ============================================================================
// FUNÇÕES DE MOUSE - CAPTURA DE EVENTOS USB
// ============================================================================

char* encontrarMouse() {
    static char device_path[256];
    char name[256] = "Unknown";
    int fd;
    
    for (int i = 0; i < 10; i++) {
        snprintf(device_path, sizeof(device_path), "/dev/input/event%d", i);
        
        fd = open(device_path, O_RDONLY | O_NONBLOCK);
        if (fd < 0) continue;
        
        ioctl(fd, EVIOCGNAME(sizeof(name)), name);
        
        if (strstr(name, "Mouse") || strstr(name, "mouse") || 
            strstr(name, "USB") || strstr(name, "usb")) {
            printf("Mouse encontrado: %s em %s\n", name, device_path);
            close(fd);
            return device_path;
        }
        
        close(fd);
    }
    
    return NULL;
}

void* threadLeituraMouseUSB(void* arg) {
    const char* device_path = (const char*)arg;
    struct input_event ev;
    int fd;
    
    fd = open(device_path, O_RDONLY);
    if (fd < 0) {
        perror("Erro ao abrir dispositivo do mouse");
        return NULL;
    }
    
    printf("Thread do mouse iniciada. Aguardando modo seleção...\n");
    
    int btn_left_anterior = 0;
    
    while (programa_rodando) {
        ssize_t bytes = read(fd, &ev, sizeof(struct input_event));
        
        if (bytes < (ssize_t)sizeof(struct input_event)) {
            if (errno == EAGAIN || errno == EINTR) {
                usleep(1000);
                continue;
            }
            break;
        }
        
        pthread_mutex_lock(&cursor.lock);
        
        if (ev.type == EV_REL) {
            if (ev.code == REL_X) {
                cursor.x_screen += ev.value;
                if (cursor.x_screen < 0) cursor.x_screen = 0;
                if (cursor.x_screen >= SCREEN_WIDTH) cursor.x_screen = SCREEN_WIDTH - 1;
                atualizarCoordenadaImagem();
            }
            else if (ev.code == REL_Y) {
                cursor.y_screen += ev.value;
                if (cursor.y_screen < 0) cursor.y_screen = 0;
                if (cursor.y_screen >= SCREEN_HEIGHT) cursor.y_screen = SCREEN_HEIGHT - 1;
                atualizarCoordenadaImagem();
            }
        }
        else if (ev.type == EV_KEY) {
            if (ev.code == BTN_LEFT) {
                cursor.button_left = ev.value;
                
                // Só processa cliques quando está em modo seleção
                if (modo_selecao) {
                    if (ev.value && !btn_left_anterior) {
                        // Início do arrasto
                        selecao.x_inicio = cursor.x_img;
                        selecao.y_inicio = cursor.y_img;
                        selecao.arrastando = 1;
                        printf("\n[SELEÇÃO] Iniciando em (%d, %d)\n", 
                               cursor.x_img, cursor.y_img);
                    }
                    else if (!ev.value && btn_left_anterior) {
                        // Fim do arrasto
                        selecao.x_fim = cursor.x_img;
                        selecao.y_fim = cursor.y_img;
                        selecao.arrastando = 0;
                        selecao.ativa = 1;
                        printf("[SELEÇÃO] Finalizada em (%d, %d)\n", 
                               cursor.x_img, cursor.y_img);
                        printf("[REGIÃO] De (%d,%d) até (%d,%d)\n", 
                               selecao.x_inicio, selecao.y_inicio,
                               selecao.x_fim, selecao.y_fim);
                    }
                }
                
                btn_left_anterior = ev.value;
            }
            else if (ev.code == BTN_RIGHT) {
                cursor.button_right = ev.value;
                
                // Botão direito cancela seleção
                if (ev.value && modo_selecao) {
                    selecao.ativa = 0;
                    selecao.arrastando = 0;
                    printf("\n[SELEÇÃO] Cancelada\n");
                }
            }
        }
        
        pthread_mutex_unlock(&cursor.lock);
    }
    
    close(fd);
    return NULL;
}

// ============================================================================
// FUNÇÕES DE DESENHO - CURSOR E FRAMEBUFFER (160x120)
// ============================================================================

void desenharCursor(unsigned char* framebuffer, int x, int y) {
    // Desenha cruz pequena na imagem 160x120
    for (int i = -CURSOR_SIZE; i <= CURSOR_SIZE; i++) {
        // Linha horizontal
        int px = x + i;
        if (px >= 0 && px < IMG_WIDTH && y >= 0 && y < IMG_HEIGHT) {
            framebuffer[y * IMG_WIDTH + px] = CURSOR_COLOR;
        }
        
        // Linha vertical
        int py = y + i;
        if (x >= 0 && x < IMG_WIDTH && py >= 0 && py < IMG_HEIGHT) {
            framebuffer[py * IMG_WIDTH + x] = CURSOR_COLOR;
        }
    }
    
    // Ponto central
    if (x >= 0 && x < IMG_WIDTH && y >= 0 && y < IMG_HEIGHT) {
        framebuffer[y * IMG_WIDTH + x] = CURSOR_COLOR;
    }
}

void desenharRetanguloSelecao(unsigned char* framebuffer, int x1, int y1, int x2, int y2) {
    int x_min = (x1 < x2) ? x1 : x2;
    int x_max = (x1 < x2) ? x2 : x1;
    int y_min = (y1 < y2) ? y1 : y2;
    int y_max = (y1 < y2) ? y2 : y1;
    
    // Desenha borda do retângulo
    for (int x = x_min; x <= x_max; x++) {
        if (x >= 0 && x < IMG_WIDTH) {
            if (y_min >= 0 && y_min < IMG_HEIGHT)
                framebuffer[y_min * IMG_WIDTH + x] = CURSOR_COLOR;
            if (y_max >= 0 && y_max < IMG_HEIGHT)
                framebuffer[y_max * IMG_WIDTH + x] = CURSOR_COLOR;
        }
    }
    
    for (int y = y_min; y <= y_max; y++) {
        if (y >= 0 && y < IMG_HEIGHT) {
            if (x_min >= 0 && x_min < IMG_WIDTH)
                framebuffer[y * IMG_WIDTH + x_min] = CURSOR_COLOR;
            if (x_max >= 0 && x_max < IMG_WIDTH)
                framebuffer[y * IMG_WIDTH + x_max] = CURSOR_COLOR;
        }
    }
}

void aplicarMascaraSelecao(unsigned char* destino, unsigned char* fonte, 
                           int x1, int y1, int x2, int y2) {
    int x_min = (x1 < x2) ? x1 : x2;
    int x_max = (x1 < x2) ? x2 : x1;
    int y_min = (y1 < y2) ? y1 : y2;
    int y_max = (y1 < y2) ? y2 : y1;
    
    if (x_min < 0) x_min = 0;
    if (x_max >= IMG_WIDTH) x_max = IMG_WIDTH - 1;
    if (y_min < 0) y_min = 0;
    if (y_max >= IMG_HEIGHT) y_max = IMG_HEIGHT - 1;
    
    int largura_regiao = x_max - x_min + 1;
    int altura_regiao = y_max - y_min + 1;
    
    printf("\n[REGIÃO] Dimensões: %dx%d pixels\n", largura_regiao, altura_regiao);
    
    int offset_x = (IMG_WIDTH - largura_regiao) / 2;
    int offset_y = (IMG_HEIGHT - altura_regiao) / 2;
    
    printf("[CENTRALIZAÇÃO] Offset: (%d, %d)\n", offset_x, offset_y);
    
    // Preenche com preto
    memset(destino, 0x00, IMG_WIDTH * IMG_HEIGHT);
    
    // Copia região centralizada
    for (int y = 0; y < altura_regiao; y++) {
        for (int x = 0; x < largura_regiao; x++) {
            int src_x = x_min + x;
            int src_y = y_min + y;
            int dst_x = offset_x + x;
            int dst_y = offset_y + y;
            
            if (src_x >= 0 && src_x < IMG_WIDTH && 
                src_y >= 0 && src_y < IMG_HEIGHT &&
                dst_x >= 0 && dst_x < IMG_WIDTH && 
                dst_y >= 0 && dst_y < IMG_HEIGHT) {
                destino[dst_y * IMG_WIDTH + dst_x] = fonte[src_y * IMG_WIDTH + src_x];
            }
        }
    }
}

void* threadAtualizacaoDisplay(void* arg) {
    printf("[THREAD] Iniciando atualização contínua...\n");
    int contador_fps = 0;
    
    while (programa_rodando) {
        if (!imagem_carregada) {
            usleep(50000);
            continue;
        }
        
        // Só atualiza se cursor deve ser mostrado (modo seleção ativo)
        if (mostrar_cursor) {
            pthread_mutex_lock(&cursor.lock);
            int x = cursor.x_img;
            int y = cursor.y_img;
            int arrastando = selecao.arrastando;
            pthread_mutex_unlock(&cursor.lock);
            
            // Copia imagem limpa para RAM compartilhada
            memcpy(fpga_framebuffer, imagem_original, IMG_WIDTH * IMG_HEIGHT);
            
            // Desenha retângulo se arrastando
            if (arrastando && modo_selecao) {
                desenharRetanguloSelecao(fpga_framebuffer, selecao.x_inicio, 
                                        selecao.y_inicio, x, y);
            }
            // Desenha retângulo finalizado
            else if (selecao.ativa && modo_selecao) {
                desenharRetanguloSelecao(fpga_framebuffer, selecao.x_inicio, 
                                        selecao.y_inicio, selecao.x_fim, selecao.y_fim);
            }
            
            // SEMPRE desenha cursor quando modo_selecao ativo
            desenharCursor(fpga_framebuffer, x, y);
            
            // Mostra coordenadas em tempo real (a cada 15 frames = ~4x por segundo)
            if (contador_fps % 15 == 0) {
                printf("\r🖱️  Posição: [%3d, %3d] (160x120)   ", x, y);
                fflush(stdout);
            }
            contador_fps++;
            
            usleep(16000); // ~60 FPS
        } else {
            // Sem cursor: apenas garante que imagem está na RAM
            contador_fps = 0;
            usleep(100000); // 100ms (economia de CPU)
        }
    }
    
    printf("\n");
    return NULL;
}

// ============================================================================
// MENU E INTERFACE
// ============================================================================

void exibirMenu() {
    printf("\n╔════════════════════════════════════════════════╗\n");
    printf("║     Algoritmos de Processamento de Imagem     ║\n");
    printf("╚════════════════════════════════════════════════╝\n");
    printf("  1  - Reset (Volta imagem original)\n");
    printf("  2  - Replicação 2x\n");
    printf("  3  - Decimação 2x\n");
    printf("  4  - Zoom NN 2x\n");
    printf("  5  - Média 2x\n");
    printf("  6  - Cópia Direta (Modo Seleção de Região)\n");
    printf("  7  - Replicação 4x\n");
    printf("  8  - Decimação 4x\n");
    printf("  9  - Zoom NN 4x\n");
    printf("  10 - Média 4x\n");
    printf("  0  - Sair\n");
    printf("════════════════════════════════════════════════\n");
    
    if (modo_selecao) {
        printf("\n┌─────────────────────────────────────────────┐\n");
        printf("│   🖱️  MODO SELEÇÃO ATIVO                   │\n");
        printf("├─────────────────────────────────────────────┤\n");
        printf("│ • Posição: (%3d, %3d) na imagem 160x120   │\n", cursor.x_img, cursor.y_img);
        if (selecao.ativa) {
            printf("│ • Região: (%3d,%3d) até (%3d,%3d)         │\n",
                   selecao.x_inicio, selecao.y_inicio, 
                   selecao.x_fim, selecao.y_fim);
            printf("│ • Digite 6 para aplicar foco              │\n");
        } else if (selecao.arrastando) {
            printf("│ • ARRASTANDO: (%3d,%3d) → (%3d,%3d)       │\n",
                   selecao.x_inicio, selecao.y_inicio,
                   cursor.x_img, cursor.y_img);
        } else {
            printf("│ • Arraste o mouse para selecionar         │\n");
        }
        printf("│ • Botão direito: Cancelar                  │\n");
        printf("└─────────────────────────────────────────────┘\n");
    }
    
    printf("\n▶ Opção: ");
}

// ============================================================================
// FUNÇÃO PRINCIPAL
// ============================================================================

int main() {
    char* mouse_path;
        
    IMAGE_MEM_BASE_VAL = ONCHIP_MEMORY2_1_BASE;
    CONTROL_PIO_BASE_VAL = PIO_LED_BASE;
  
    int bytes = carregarImagemMIF(IMAGE_PATH);     
    transferirImagemFPGA(bytes);    
    fpga_framebuffer = (unsigned char*)IMAGE_MEM_ptr;
    imagem_original = (unsigned char*)malloc(IMG_WIDTH * IMG_HEIGHT);
    imagem_backup = (unsigned char*)malloc(IMG_WIDTH * IMG_HEIGHT);
    memcpy(imagem_original, fpga_framebuffer, IMG_WIDTH * IMG_HEIGHT);
    memcpy(imagem_backup, fpga_framebuffer, IMG_WIDTH * IMG_HEIGHT);
    imagem_carregada = 1;
    mouse_path = encontrarMouse();
    
    if (!mouse_path) {
        fprintf(stderr, "\n⚠️  Mouse USB não detectado!\n");
        fprintf(stderr, "Modo de seleção não estará disponível.\n\n");
    } else {
        printf("✅ Mouse USB detectado\n\n");
        
        if (pthread_create(&thread_mouse, NULL, threadLeituraMouseUSB, (void*)mouse_path) != 0) {
            perror("Erro ao criar thread do mouse");
            mouse_path = NULL;
        }
        
        if (pthread_create(&thread_desenho, NULL, threadAtualizacaoDisplay, NULL) != 0) {
            perror("Erro ao criar thread de desenho");
        }
    }
    
    // Loop do menu
    int opcao = -1;
    while (opcao != 0) {
        exibirMenu();
        
        if (scanf("%d", &opcao) != 1) {
            while (getchar() != '\n');
            printf("❌ Entrada inválida!\n");
            continue;
        }
        
        if (opcao == 0) {
            printf("\n👋 Encerrando...\n");
            break;
        }
        
        // === RESET (Opção 1) ===
        if (opcao == 1) {
            printf("\n🔄 RESET - Restaurando imagem original...\n");
            
            memcpy(imagem_original, imagem_backup, IMG_WIDTH * IMG_HEIGHT);
            memcpy(fpga_framebuffer, imagem_backup, IMG_WIDTH * IMG_HEIGHT);
            
            modo_selecao = 0;
            mostrar_cursor = 0;
            selecao.ativa = 0;
            selecao.arrastando = 0;
            
            enviarComando(obterCodigoEstado(1));
            
            printf("✅ Imagem original restaurada!\n");
            continue;
        }
        
        // === CÓPIA DIRETA (Opção 6) - Modo Seleção ===
        if (opcao == 6) {
            if (!mouse_path) {
                printf("❌ Mouse USB não disponível!\n");
                continue;
            }
            
            if (!modo_selecao) {
                // ============================================================
                // PASSO 1: Mostra PRIMEIRO a imagem original 160x120
                // ============================================================
                printf("\n╔════════════════════════════════════════════════╗\n");
                printf("║   📸 CÓPIA DIRETA - Modo Seleção de Região   ║\n");
                printf("╚════════════════════════════════════════════════╝\n\n");
                
                printf("▶ PASSO 1: Carregando imagem original 160x120...\n");
                
                // Restaura e MOSTRA imagem original
                memcpy(imagem_original, imagem_backup, IMG_WIDTH * IMG_HEIGHT);
                memcpy(fpga_framebuffer, imagem_backup, IMG_WIDTH * IMG_HEIGHT);
                
                printf("  ✅ Imagem original exibida na tela VGA.\n");
                printf("  ⏳ Visualizando por 3 segundos...\n\n");
                
                // Aguarda para visualizar a imagem
                for (int i = 3; i > 0; i--) {
                    printf("  ⏱️  %d...\n", i);
                    sleep(1);
                }
                
                // ============================================================
                // PASSO 2: Agora ativa o modo seleção com cursor
                // ============================================================
                printf("\n▶ PASSO 2: Ativando modo de seleção...\n");
                
                modo_selecao = 1;
                mostrar_cursor = 1;  // ATIVA o cursor
                selecao.ativa = 0;
                selecao.arrastando = 0;
                
                printf("  ✅ Cursor ativado!\n");
                printf("  ✅ Mouse USB conectado e funcionando.\n\n");
                
                printf("╔════════════════════════════════════════════════╗\n");
                printf("║  🖱️  INSTRUÇÕES DE USO:                        ║\n");
                printf("║                                                ║\n");
                printf("║  1. Mova o mouse para ver o cursor na tela    ║\n");
                printf("║  2. As coordenadas aparecerão em tempo real   ║\n");
                printf("║  3. Clique e arraste para selecionar região   ║\n");
                printf("║  4. Solte o botão para finalizar seleção      ║\n");
                printf("║  5. Digite 6 novamente para aplicar foco      ║\n");
                printf("║                                                ║\n");
                printf("║  Botão direito: Cancelar seleção              ║\n");
                printf("╚════════════════════════════════════════════════╝\n\n");
                
                printf("🖱️  Aguardando movimento do mouse...\n");
                printf("📍 Coordenadas em tempo real:\n");
                
                continue;
                
            } else if (!selecao.ativa) {
                printf("\n⚠️  Nenhuma região selecionada ainda.\n");
                printf("📍 Posição atual: (%d, %d)\n", cursor.x_img, cursor.y_img);
                printf("🖱️  Arraste o mouse para selecionar uma região.\n");
                continue;
                
            } else {
                // ============================================================
                // PASSO 3: Aplica foco na região selecionada
                // ============================================================
                printf("\n\n╔════════════════════════════════════════════════╗\n");
                printf("║        🎯 Aplicando Foco na Região            ║\n");
                printf("╚════════════════════════════════════════════════╝\n\n");
                
                printf("▶ Região selecionada:\n");
                printf("  Início: (%d, %d)\n", selecao.x_inicio, selecao.y_inicio);
                printf("  Fim:    (%d, %d)\n", selecao.x_fim, selecao.y_fim);
                
                aplicarMascaraSelecao(imagem_original, imagem_backup,
                                     selecao.x_inicio, selecao.y_inicio,
                                     selecao.x_fim, selecao.y_fim);
                
                memcpy(fpga_framebuffer, imagem_original, IMG_WIDTH * IMG_HEIGHT);
                
                printf("\n  ✅ Foco aplicado com sucesso!\n");
                printf("  ✅ Região centralizada na imagem.\n");
                printf("  ✅ Resto da imagem preenchido com preto.\n\n");
                
                printf("▶ Agora você pode aplicar algoritmos (opções 2-10).\n\n");
                
                // Desativa modo seleção e cursor
                modo_selecao = 0;
                mostrar_cursor = 0;  // DESATIVA o cursor
                selecao.ativa = 0;
                
                continue;
            }
        }
        
        // === OUTRAS OPÇÕES (2-5, 7-10) ===
        int codigo = obterCodigoEstado(opcao);
        if (codigo < 0) {
            printf("❌ Opção inválida!\n");
            continue;
        }
        
        // Se estava em modo seleção, sai dele
        if (modo_selecao) {
            printf("Saindo do modo seleção...\n");
            modo_selecao = 0;
            mostrar_cursor = 0;
            selecao.ativa = 0;
        }
        
        printf("⚙️  Aplicando transformação...\n");
        enviarComando(codigo);
        usleep(150000);
        
        memcpy(imagem_original, fpga_framebuffer, IMG_WIDTH * IMG_HEIGHT);
        
        printf("✅ Transformação aplicada!\n");
    }
    
    // Cleanup
    programa_rodando = 0;
    
    if (mouse_path) {
        pthread_join(thread_mouse, NULL);
        pthread_join(thread_desenho, NULL);
    }
    
    if (imagem_original) free(imagem_original);
    if (imagem_backup) free(imagem_backup);
    
    pthread_mutex_destroy(&cursor.lock);
    limparRecursos();
    
    printf("\n========================================================\n");
    printf(" ✅ Sistema encerrado com sucesso!\n");
    printf("========================================================\n");
    
    return 0;
}

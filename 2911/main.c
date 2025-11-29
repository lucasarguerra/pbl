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
#include <termios.h>
#include <time.h>
#include "./hps_0.h"
#include "api.h"

#define IMAGE_PATH "/home/aluno/TEC499/TP02/SirioeGuerra/imagem.mif"
#define CURSOR_SIZE 2
#define IMG_WIDTH 160
#define IMG_HEIGHT 120
#define SCREEN_WIDTH 640
#define SCREEN_HEIGHT 480
#define CURSOR_COLOR 0xFF

// ============================================================================
// ESTRUTURAS E ENUMS
// ============================================================================

typedef struct {
    int x_screen;               
    int y_screen;               
    int x_img;                  
    int y_img;                  
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

typedef enum {
    ZOOM_IN_4X = 0,
    ZOOM_IN_2X = 1,
    COPIA_DIRETA = 2,
    ZOOM_OUT_2X = 3,
    ZOOM_OUT_4X = 4
} NivelZoom;

// ============================================================================
// VARIÁVEIS GLOBAIS
// ============================================================================

static CursorState cursor = {
    .x_screen = SCREEN_WIDTH / 2,
    .y_screen = SCREEN_HEIGHT / 2,
    .x_img = IMG_WIDTH / 2,
    .y_img = IMG_HEIGHT / 2,
    .button_left = 0,
    .button_right = 0,
    .lock = PTHREAD_MUTEX_INITIALIZER
};

static SelecaoRegiao selecao = {0};

volatile int programa_rodando = 1;
volatile int mostrar_cursor = 0;
pthread_t thread_mouse, thread_desenho, thread_keyboard;

unsigned char* imagem_original = NULL;
unsigned char* imagem_backup = NULL;
unsigned char* fpga_framebuffer = NULL;
int imagem_carregada = 0;
int imagem_cortada = 0;

int algoritmo_zoom_in = -1;   // 2=Replicação, 4=NN
int algoritmo_zoom_out = -1;  // 3=Decimação, 5=Média

NivelZoom nivel_atual = COPIA_DIRETA;

struct termios orig_termios;

// ============================================================================
// FUNÇÕES DE TERMINAL (modo sem buffer)
// ============================================================================

void desabilitarBufferTerminal() {
    struct termios raw;
    tcgetattr(STDIN_FILENO, &orig_termios);
    raw = orig_termios;
    raw.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}

void restaurarTerminal() {
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
}

// ============================================================================
// FUNÇÕES DE COORDENADAS
// ============================================================================

void atualizarCoordenadaImagem() {
    cursor.x_img = (cursor.x_screen * IMG_WIDTH) / SCREEN_WIDTH;
    cursor.y_img = (cursor.y_screen * IMG_HEIGHT) / SCREEN_HEIGHT;
    
    if (cursor.x_img < 0) cursor.x_img = 0;
    if (cursor.x_img >= IMG_WIDTH) cursor.x_img = IMG_WIDTH - 1;
    if (cursor.y_img < 0) cursor.y_img = 0;
    if (cursor.y_img >= IMG_HEIGHT) cursor.y_img = IMG_HEIGHT - 1;
}

// ============================================================================
// MOUSE
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
    int fd = open(device_path, O_RDONLY);
    if (fd < 0) return NULL;
    
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
                
                if (nivel_atual == COPIA_DIRETA) {
                    if (imagem_cortada) {
                        if (ev.value && !btn_left_anterior) {
                            printf("\n⚠️  Não é possível selecionar região em imagem cortada!\n");
                            printf("    Digite '1' para resetar e escolher nova região.\n");
                        }
                    } else {
                        if (ev.value && !btn_left_anterior) {
                            selecao.x_inicio = cursor.x_img;
                            selecao.y_inicio = cursor.y_img;
                            selecao.arrastando = 1;
                        }
                        else if (!ev.value && btn_left_anterior) {
                            selecao.x_fim = cursor.x_img;
                            selecao.y_fim = cursor.y_img;
                            selecao.arrastando = 0;
                            selecao.ativa = 1;
                            printf("\n[✓] Região selecionada: (%d,%d) → (%d,%d)\n", 
                                   selecao.x_inicio, selecao.y_inicio,
                                   selecao.x_fim, selecao.y_fim);
                            printf("    Pressione ENTER para aplicar foco.\n");
                        }
                    }
                }
                btn_left_anterior = ev.value;
            }
            else if (ev.code == BTN_RIGHT) {
                cursor.button_right = ev.value;
                if (ev.value && nivel_atual == COPIA_DIRETA && !imagem_cortada) {
                    selecao.ativa = 0;
                    selecao.arrastando = 0;
                    printf("\n[✗] Seleção cancelada.\n");
                }
            }
        }
        
        pthread_mutex_unlock(&cursor.lock);
    }
    
    close(fd);
    return NULL;
}

// ============================================================================
// DESENHO
// ============================================================================

void desenharCursor(unsigned char* framebuffer, int x, int y) {
    for (int i = -CURSOR_SIZE; i <= CURSOR_SIZE; i++) {
        int px = x + i;
        if (px >= 0 && px < IMG_WIDTH && y >= 0 && y < IMG_HEIGHT) {
            framebuffer[y * IMG_WIDTH + px] = CURSOR_COLOR;
        }
        int py = y + i;
        if (x >= 0 && x < IMG_WIDTH && py >= 0 && py < IMG_HEIGHT) {
            framebuffer[py * IMG_WIDTH + x] = CURSOR_COLOR;
        }
    }
    if (x >= 0 && x < IMG_WIDTH && y >= 0 && y < IMG_HEIGHT) {
        framebuffer[y * IMG_WIDTH + x] = CURSOR_COLOR;
    }
}

void desenharRetanguloSelecao(unsigned char* framebuffer, int x1, int y1, int x2, int y2) {
    int x_min = (x1 < x2) ? x1 : x2;
    int x_max = (x1 < x2) ? x2 : x1;
    int y_min = (y1 < y2) ? y1 : y2;
    int y_max = (y1 < y2) ? y2 : y1;
    
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
    
    int largura = x_max - x_min + 1;
    int altura = y_max - y_min + 1;
    int offset_x = (IMG_WIDTH - largura) / 2;
    int offset_y = (IMG_HEIGHT - altura) / 2;
    
    memset(destino, 0x00, IMG_WIDTH * IMG_HEIGHT);
    
    for (int y = 0; y < altura; y++) {
        for (int x = 0; x < largura; x++) {
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
    int contador = 0;
    
    while (programa_rodando) {
        if (!imagem_carregada) {
            usleep(50000);
            continue;
        }
        
        if (mostrar_cursor && nivel_atual == COPIA_DIRETA) {
            pthread_mutex_lock(&cursor.lock);
            int x = cursor.x_img;
            int y = cursor.y_img;
            int arrastando = selecao.arrastando;
            pthread_mutex_unlock(&cursor.lock);
            
            memcpy(fpga_framebuffer, imagem_original, IMG_WIDTH * IMG_HEIGHT);
            
            if (arrastando && !imagem_cortada) {
                desenharRetanguloSelecao(fpga_framebuffer, selecao.x_inicio, 
                                        selecao.y_inicio, x, y);
            }
            else if (selecao.ativa && !imagem_cortada) {
                desenharRetanguloSelecao(fpga_framebuffer, selecao.x_inicio, 
                                        selecao.y_inicio, selecao.x_fim, selecao.y_fim);
            }
            
            desenharCursor(fpga_framebuffer, x, y);
            
            if (contador % 15 == 0) {
                printf("\r🖱️  [%3d, %3d]", x, y);
                fflush(stdout);
            }
            contador++;
            
            usleep(16000);
        } else {
            contador = 0;
            usleep(100000);
        }
    }
    
    return NULL;
}

// ============================================================================
// NAVEGAÇÃO
// ============================================================================

const char* getNomeNivel(NivelZoom nivel) {
    switch(nivel) {
        case ZOOM_IN_4X: return "Zoom In 4x";
        case ZOOM_IN_2X: return "Zoom In 2x";
        case COPIA_DIRETA: return "Cópia Direta";
        case ZOOM_OUT_2X: return "Zoom Out 2x";
        case ZOOM_OUT_4X: return "Zoom Out 4x";
        default: return "?";
    }
}

int getCodigoAlgoritmo(NivelZoom nivel) {
    switch(nivel) {
        case ZOOM_IN_4X: return (algoritmo_zoom_in == 2) ? 7 : 9;
        case ZOOM_IN_2X: return (algoritmo_zoom_in == 2) ? 2 : 4;
        case COPIA_DIRETA: return 6;
        case ZOOM_OUT_2X: return (algoritmo_zoom_out == 3) ? 3 : 5;
        case ZOOM_OUT_4X: return (algoritmo_zoom_out == 3) ? 8 : 10;
        default: return -1;
    }
}

void aplicarNivel(NivelZoom nivel) {
    int codigo = getCodigoAlgoritmo(nivel);
    if (codigo < 0) return;
    
    enviarComando(obterCodigoEstado(codigo));
    usleep(150000);
    
    mostrar_cursor = (nivel == COPIA_DIRETA) ? 1 : 0;
}

void mostrarMenu() {
    printf("\n╔════════════════════════════════════════════════╗\n");
    printf("║              NAVEGAÇÃO DE ZOOM                 ║\n");
    printf("╚════════════════════════════════════════════════╝\n");
    
    for (int i = 0; i <= 4; i++) {
        if (i == nivel_atual) {
            printf("  >>> %s <<<\n", getNomeNivel((NivelZoom)i));
        } else {
            printf("      %s\n", getNomeNivel((NivelZoom)i));
        }
    }
    
    printf("\n  +  → Aumentar zoom\n");
    printf("  -  → Diminuir zoom\n");
    printf("  1  → Reset\n");
    printf("  0  → Sair\n");
    printf("════════════════════════════════════════════════\n\n");
}

void* threadTeclado(void* arg) {
    char c;
    
    while (programa_rodando) {
        c = getchar();
        
        if (c == '+' || c == '=') {
            if (nivel_atual > ZOOM_IN_4X) {
                nivel_atual--;
                aplicarNivel(nivel_atual);
                
                if (nivel_atual != COPIA_DIRETA) {
                    printf("\n\n⬆️  %s\n", getNomeNivel(nivel_atual));
                    mostrarMenu();
                } else {
                    printf("\n\n");
                }
            }
        }
        else if (c == '-' || c == '_') {
            if (nivel_atual < ZOOM_OUT_4X) {
                nivel_atual++;
                aplicarNivel(nivel_atual);
                
                if (nivel_atual != COPIA_DIRETA) {
                    printf("\n\n⬇️  %s\n", getNomeNivel(nivel_atual));
                    mostrarMenu();
                } else {
                    printf("\n\n");
                }
            }
        }
        else if (c == '1') {
            printf("\n\n🔄 Reset...\n");
            
            memcpy(imagem_original, imagem_backup, IMG_WIDTH * IMG_HEIGHT);
            memcpy(fpga_framebuffer, imagem_backup, IMG_WIDTH * IMG_HEIGHT);
            
            selecao.ativa = 0;
            selecao.arrastando = 0;
            imagem_cortada = 0;
            
            nivel_atual = COPIA_DIRETA;
            aplicarNivel(nivel_atual);
            
            printf("✅ Imagem restaurada\n\n");
        }
        else if (c == '0') {
            programa_rodando = 0;
            break;
        }
        else if (c == '\n' && nivel_atual == COPIA_DIRETA && selecao.ativa && !imagem_cortada) {
            printf("\n\n🎯 Aplicando foco...\n");
            
            // Para a thread de desenho temporariamente
            mostrar_cursor = 0;
            usleep(100000); // Aguarda 100ms para thread parar de desenhar
            
            // Aplica o corte na região selecionada
            aplicarMascaraSelecao(imagem_original, imagem_backup,
                                 selecao.x_inicio, selecao.y_inicio,
                                 selecao.x_fim, selecao.y_fim);
            
            // IMPORTANTE: Copia diretamente para a RAM do FPGA
            memcpy(fpga_framebuffer, imagem_original, IMG_WIDTH * IMG_HEIGHT);
            
            printf("✅ Região centralizada e aplicada ao FPGA\n");
            printf("    Atualizando display...\n");
            
            // Pequena pausa para garantir que dados foram escritos
            usleep(50000);
            
            printf("✅ Imagem cortada visível na tela\n\n");
            
            imagem_cortada = 1;
            selecao.ativa = 0;
            selecao.arrastando = 0;
            
            // Religa o cursor para mostrar a imagem cortada
            mostrar_cursor = 1;
        }
    }
    
    return NULL;
}

// ============================================================================
// MAIN
// ============================================================================

int main() {
    char* mouse_path;
    
    printf("════════════════════════════════════════════════\n");
    printf("  Sistema de Processamento com Navegação\n");
    printf("════════════════════════════════════════════════\n\n");
    
    IMAGE_MEM_BASE_VAL = ONCHIP_MEMORY2_1_BASE;
    CONTROL_PIO_BASE_VAL = PIO_LED_BASE;
    
    // Configuração inicial
    printf("CONFIGURAÇÃO DOS ALGORITMOS\n\n");
    
    printf("Zoom IN (2=Replicação, 4=NN): ");
    while (1) {
        scanf("%d", &algoritmo_zoom_in);
        if (algoritmo_zoom_in == 2 || algoritmo_zoom_in == 4) break;
        printf("Inválido! Digite 2 ou 4: ");
    }
    
    printf("Zoom OUT (3=Decimação, 5=Média): ");
    while (1) {
        scanf("%d", &algoritmo_zoom_out);
        if (algoritmo_zoom_out == 3 || algoritmo_zoom_out == 5) break;
        printf("Inválido! Digite 3 ou 5: ");
    }
    
    while (getchar() != '\n');
    
    printf("\n✅ Configurado: Zoom IN=%s, Zoom OUT=%s\n\n",
           algoritmo_zoom_in == 2 ? "Replicação" : "NN",
           algoritmo_zoom_out == 3 ? "Decimação" : "Média");
    
    // Carrega imagem
    printf("Carregando imagem...\n");
    int bytes = carregarImagemMIF(IMAGE_PATH);
    if (bytes < 0) {
        perror("Erro ao carregar imagem");
        return 1;
    }
    
    if (mapearPonte() < 0) {
        perror("Erro ao mapear memória");
        limparRecursos();
        return 1;
    }
    
    transferirImagemFPGA(bytes);
    fpga_framebuffer = (unsigned char*)IMAGE_MEM_ptr;
    
    imagem_original = (unsigned char*)malloc(IMG_WIDTH * IMG_HEIGHT);
    imagem_backup = (unsigned char*)malloc(IMG_WIDTH * IMG_HEIGHT);
    
    if (!imagem_original || !imagem_backup) {
        perror("Erro ao alocar memória");
        limparRecursos();
        return 1;
    }
    
    memcpy(imagem_original, fpga_framebuffer, IMG_WIDTH * IMG_HEIGHT);
    memcpy(imagem_backup, fpga_framebuffer, IMG_WIDTH * IMG_HEIGHT);
    imagem_carregada = 1;
    
    printf("✅ Imagem carregada\n\n");
    
    // Mouse
    mouse_path = encontrarMouse();
    if (mouse_path) {
        pthread_create(&thread_mouse, NULL, threadLeituraMouseUSB, (void*)mouse_path);
        pthread_create(&thread_desenho, NULL, threadAtualizacaoDisplay, NULL);
        printf("✅ Mouse ativo\n\n");
    }
    
    // Inicia em cópia direta
    nivel_atual = COPIA_DIRETA;
    aplicarNivel(nivel_atual);
    
    printf("════════════════════════════════════════════════\n");
    printf("  Sistema iniciado em CÓPIA DIRETA\n");
    printf("════════════════════════════════════════════════\n\n");
    
    if (mouse_path) {
        printf("🖱️  Use o mouse para selecionar região (opcional)\n");
    }
    printf("⌨️  Use + / - para navegar entre zooms\n");
    printf("    Digite 1 para reset, 0 para sair\n\n");
    
    // Habilita modo sem buffer
    desabilitarBufferTerminal();
    
    // Thread de teclado
    pthread_create(&thread_keyboard, NULL, threadTeclado, NULL);
    
    // Loop principal
    while (programa_rodando) {
        sleep(1);
    }
    
    // Cleanup
    restaurarTerminal();
    
    if (mouse_path) {
        pthread_cancel(thread_mouse);
        pthread_cancel(thread_desenho);
        pthread_join(thread_mouse, NULL);
        pthread_join(thread_desenho, NULL);
    }
    
    pthread_cancel(thread_keyboard);
    pthread_join(thread_keyboard, NULL);
    
    free(imagem_original);
    free(imagem_backup);
    pthread_mutex_destroy(&cursor.lock);
    limparRecursos();
    
    printf("\n\n════════════════════════════════════════════════\n");
    printf("  ✅ Sistema encerrado\n");
    printf("════════════════════════════════════════════════\n");
    
    return 0;
}
}

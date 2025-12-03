#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 199309L  // Para nanosleep

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>      // Para usleep
#include <pthread.h>
#include <linux/input.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <stdint.h>      // Para uint32_t
#include <time.h>        // Para nanosleep
#include "./hps_0.h"
#include "api.h"
#define IMAGE_PATH "/home/aluno/TEC499/TP02/SirioeGuerra/imagem.mif"
#define IMG_WIDTH 160
#define IMG_HEIGHT 120
#define SCREEN_WIDTH 640
#define SCREEN_HEIGHT 480

// ============================================================================
// VARIÁVEIS GLOBAIS
// ============================================================================

typedef struct {
    int x_screen, y_screen;
    int x_img, y_img;
    int button_left, button_right;
    pthread_mutex_t lock;
} CursorState;

typedef struct {
    int x_inicio, y_inicio;
    int x_fim, y_fim;
    int ativa, arrastando;
} SelecaoRegiao;

static CursorState cursor = {
    .x_screen = SCREEN_WIDTH / 2,
    .y_screen = SCREEN_HEIGHT / 2,
    .x_img = IMG_WIDTH / 2,
    .y_img = IMG_HEIGHT / 2,
    .lock = PTHREAD_MUTEX_INITIALIZER
};

static SelecaoRegiao selecao = {0};

// Configurações do usuário
static int algoritmo_zoom_in = 0;     // 0=replicação, 1=vizinho
static int algoritmo_zoom_out = 0;    // 0=decimação, 1=média
static int nivel_zoom = 2;            // 0=0.25x, 1=0.5x, 2=Cópia Direta, 3=2x, 4=4x
static int precisa_copia_direta = 0;  // Flag: precisa aplicar cópia direta

volatile int programa_rodando = 1;
volatile int modo_selecao = 0;
volatile int mostrar_cursor = 0;
volatile int mouse_detectado = 0;
volatile int em_modo_corte = 0;
pthread_t thread_mouse, thread_desenho;
unsigned char* imagem_original = NULL;
unsigned char* imagem_backup = NULL;
unsigned char* fpga_framebuffer = NULL;
int imagem_carregada = 0;

// Ponteiros PIO
volatile uint32_t* cursor_enable_ptr = NULL;
volatile uint32_t* cursor_x_ptr = NULL;
volatile uint32_t* cursor_y_ptr = NULL;
volatile uint32_t* selection_enable_ptr = NULL;
volatile uint32_t* sel_x1_ptr = NULL;
volatile uint32_t* sel_y1_ptr = NULL;
volatile uint32_t* sel_x2_ptr = NULL;
volatile uint32_t* sel_y2_ptr = NULL;
void* lw_bridge_map = NULL;

// ============================================================================
// FUNÇÕES BÁSICAS
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
// FUNÇÕES DE ZOOM CORRIGIDAS
// ============================================================================

void aplicarAlgoritmo(int codigo) {
    enviarComando(codigo);
    struct timespec ts = {0, 200000000};
    nanosleep(&ts, NULL);
}

// ============================================================================
// FUNÇÕES DE ZOOM CORRIGIDAS PARA A SEQUÊNCIA
// ============================================================================

void aplicarZoomIn() {
    printf("\nZOOM IN (aumenta): ");
    
    if (nivel_zoom >= 4) {
        printf("Maximo atingido (4x)\n");
        return;
    }
    
    nivel_zoom++;
    
    if (em_modo_corte) {
        em_modo_corte = 0;
        selecao.arrastando = 0;
        selecao.ativa = 0;
        precisa_copia_direta = 0;
    }
    
    int codigo = -1;
    
    switch(nivel_zoom) {
        case 0: // 0.25x (1/4)
            if (algoritmo_zoom_out == 0) {
                printf("Decimacao 4x\n");
                codigo = obterCodigoEstado(8);
            } else {
                printf("Media 4x\n");
                codigo = obterCodigoEstado(10);
            }
            modo_selecao = 0;
            mostrar_cursor = 0;
            break;
            
        case 1: // 0.5x (1/2)
            if (algoritmo_zoom_out == 0) {
                printf("Decimacao 2x\n");
                codigo = obterCodigoEstado(3);
            } else {
                printf("Media 2x\n");
                codigo = obterCodigoEstado(5);
            }
            modo_selecao = 0;
            mostrar_cursor = 0;
            break;
            
        case 2: // Cópia Direta (1x)
            printf("Imagem original\n");
            codigo = obterCodigoEstado(6);
            modo_selecao = 1;
            mostrar_cursor = 1;
            break;
            
        case 3: // 2x
            if (algoritmo_zoom_in == 0) {
                printf("Replicacao 2x\n");
                codigo = obterCodigoEstado(2);
            } else {
                printf("Zoom NN 2x\n");
                codigo = obterCodigoEstado(4);
            }
            modo_selecao = 0;
            mostrar_cursor = 0;
            break;
            
        case 4: // 4x
            if (algoritmo_zoom_in == 0) {
                printf("Replicacao 4x\n");
                codigo = obterCodigoEstado(7);
            } else {
                printf("Zoom NN 4x\n");
                codigo = obterCodigoEstado(9);
            }
            modo_selecao = 0;
            mostrar_cursor = 0;
            break;
    }
    
    if (codigo >= 0) {
        aplicarAlgoritmo(codigo);
        memcpy(imagem_original, fpga_framebuffer, IMG_WIDTH * IMG_HEIGHT);
    }
}

void aplicarZoomOut() {
    printf("\nZOOM OUT (reduz): ");
    
    if (nivel_zoom <= 0) {
        printf("Minimo atingido (0.25x)\n");
        return;
    }
    
    nivel_zoom--;
    
    if (em_modo_corte) {
        em_modo_corte = 0;
        selecao.arrastando = 0;
        selecao.ativa = 0;
        precisa_copia_direta = 0;
    }
    
    int codigo = -1;
    
    switch(nivel_zoom) {
        case 0: // 0.25x (1/4)
            if (algoritmo_zoom_out == 0) {
                printf("Decimacao 4x\n");
                codigo = obterCodigoEstado(8);
            } else {
                printf("Media 4x\n");
                codigo = obterCodigoEstado(10);
            }
            modo_selecao = 0;
            mostrar_cursor = 0;
            break;
            
        case 1: // 0.5x (1/2)
            if (algoritmo_zoom_out == 0) {
                printf("Decimacao 2x\n");
                codigo = obterCodigoEstado(3);
            } else {
                printf("Media 2x\n");
                codigo = obterCodigoEstado(5);
            }
            modo_selecao = 0;
            mostrar_cursor = 0;
            break;
            
        case 2: // Cópia Direta (1x)
            printf("Copia Direta (160x120)\n");
            codigo = obterCodigoEstado(6);
            modo_selecao = 1;
            mostrar_cursor = 1;
            break;
            
        case 3: // 2x
            if (algoritmo_zoom_in == 0) {
                printf("Replicacao 2x\n");
                codigo = obterCodigoEstado(2);
            } else {
                printf("Zoom NN 2x\n");
                codigo = obterCodigoEstado(4);
            }
            modo_selecao = 0;
            mostrar_cursor = 0;
            break;
            
        case 4: // 4x
            if (algoritmo_zoom_in == 0) {
                printf("Replicacao 4x\n");
                codigo = obterCodigoEstado(7);
            } else {
                printf("Zoom NN 4x\n");
                codigo = obterCodigoEstado(9);
            }
            modo_selecao = 0;
            mostrar_cursor = 0;
            break;
    }
    
    if (codigo >= 0) {
        aplicarAlgoritmo(codigo);
        memcpy(imagem_original, fpga_framebuffer, IMG_WIDTH * IMG_HEIGHT);
    }
}

void aplicarCopiaDireta() {
    printf("\nAplicando COPIA DIRETA (160x120)\n");
    int codigo = obterCodigoEstado(6);
    if (codigo >= 0) {
        aplicarAlgoritmo(codigo);
        memcpy(imagem_original, fpga_framebuffer, IMG_WIDTH * IMG_HEIGHT);
    }
    nivel_zoom = 2;
    modo_selecao = 1;
    mostrar_cursor = 1;
}

void resetParaOriginal() {
    printf("\nRESET para imagem original\n");
    
    int codigo = obterCodigoEstado(1);
    if (codigo >= 0) {
        aplicarAlgoritmo(codigo);
    }
    
    memcpy(imagem_original, imagem_backup, IMG_WIDTH * IMG_HEIGHT);
    memcpy(fpga_framebuffer, imagem_backup, IMG_WIDTH * IMG_HEIGHT);
    
    aplicarCopiaDireta();
    
    modo_selecao = 1;
    mostrar_cursor = 1;
    em_modo_corte = 0;
    selecao.ativa = 0;
    selecao.arrastando = 0;
    precisa_copia_direta = 0;
    
    printf("Imagem original (160x120) restaurada\n");
}

// ============================================================================
// FUNÇÕES DE CORTE
// ============================================================================

void aplicarCorte() {
    printf("\nAPLICANDO CORTE\n");
    printf("Regiao: (%d,%d) ate (%d,%d)\n",
           selecao.x_inicio, selecao.y_inicio,
           selecao.x_fim, selecao.y_fim);
    
    int x_min = (selecao.x_inicio < selecao.x_fim) ? selecao.x_inicio : selecao.x_fim;
    int x_max = (selecao.x_inicio < selecao.x_fim) ? selecao.x_fim : selecao.x_inicio;
    int y_min = (selecao.y_inicio < selecao.y_fim) ? selecao.y_inicio : selecao.y_fim;
    int y_max = (selecao.y_inicio < selecao.y_fim) ? selecao.y_fim : selecao.y_inicio;
    
    int largura = x_max - x_min + 1;
    int altura = y_max - y_min + 1;
    
    printf("Dimensoes: %dx%d pixels\n", largura, altura);
    
    memset(imagem_original, 0x00, IMG_WIDTH * IMG_HEIGHT);
    
    int offset_x = (IMG_WIDTH - largura) / 2;
    int offset_y = (IMG_HEIGHT - altura) / 2;
    
    for (int y = 0; y < altura; y++) {
        for (int x = 0; x < largura; x++) {
            int src_x = x_min + x;
            int src_y = y_min + y;
            int dst_x = offset_x + x;
            int dst_y = offset_y + y;
            
            if (dst_x >= 0 && dst_x < IMG_WIDTH && dst_y >= 0 && dst_y < IMG_HEIGHT) {
                imagem_original[dst_y * IMG_WIDTH + dst_x] = 
                    imagem_backup[src_y * IMG_WIDTH + src_x];
            }
        }
    }
    
    memcpy(fpga_framebuffer, imagem_original, IMG_WIDTH * IMG_HEIGHT);
    aplicarCopiaDireta();
    
    printf("Corte aplicado e centralizado\n");
    
    selecao.ativa = 0;
    em_modo_corte = 0;
}

// ============================================================================
// MAPEAMENTO PIOs
// ============================================================================

int mapear_pios_cursor() {
    int fd = open("/dev/mem", O_RDWR | O_SYNC);
    if (fd < 0) return -1;
    
    lw_bridge_map = mmap(NULL, 0x00200000, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0xFF200000);
    close(fd);
    
    if (lw_bridge_map == MAP_FAILED) return -1;
    
    cursor_enable_ptr = (volatile uint32_t*)((char*)lw_bridge_map + CURSOR_ENABLE_PIO_BASE);
    cursor_x_ptr = (volatile uint32_t*)((char*)lw_bridge_map + CURSOR_X_PIO_BASE);
    cursor_y_ptr = (volatile uint32_t*)((char*)lw_bridge_map + CURSOR_Y_PIO_BASE);
    selection_enable_ptr = (volatile uint32_t*)((char*)lw_bridge_map + SELECTION_ENABLE_PIO_BASE);
    sel_x1_ptr = (volatile uint32_t*)((char*)lw_bridge_map + SEL_X1_PIO_BASE);
    sel_y1_ptr = (volatile uint32_t*)((char*)lw_bridge_map + SEL_Y1_PIO_BASE);
    sel_x2_ptr = (volatile uint32_t*)((char*)lw_bridge_map + SEL_X2_PIO_BASE);
    sel_y2_ptr = (volatile uint32_t*)((char*)lw_bridge_map + SEL_Y2_PIO_BASE);
    
    *cursor_enable_ptr = 0;
    *selection_enable_ptr = 0;
    *cursor_x_ptr = IMG_WIDTH / 2;
    *cursor_y_ptr = IMG_HEIGHT / 2;
    
    return 0;
}

void desmapear_pios_cursor() {
    if (lw_bridge_map != NULL && lw_bridge_map != MAP_FAILED) {
        munmap(lw_bridge_map, 0x00200000);
    }
}

// ============================================================================
// ATUALIZAÇÃO HARDWARE
// ============================================================================

void atualizarCursorHardware() {
    pthread_mutex_lock(&cursor.lock);
    if (cursor_x_ptr && cursor_y_ptr) {
        *cursor_x_ptr = cursor.x_img;
        *cursor_y_ptr = cursor.y_img;
        if (cursor_enable_ptr) *cursor_enable_ptr = modo_selecao ? 1 : 0;
    }
    pthread_mutex_unlock(&cursor.lock);
}

void atualizarSelecaoHardware() {
    pthread_mutex_lock(&cursor.lock);
    if (selection_enable_ptr && sel_x1_ptr && sel_y1_ptr && sel_x2_ptr && sel_y2_ptr) {
        if (selecao.arrastando || selecao.ativa) {
            *sel_x1_ptr = selecao.x_inicio;
            *sel_y1_ptr = selecao.y_inicio;
            *sel_x2_ptr = selecao.arrastando ? cursor.x_img : selecao.x_fim;
            *sel_y2_ptr = selecao.arrastando ? cursor.y_img : selecao.y_fim;
            *selection_enable_ptr = 1;
        } else {
            *selection_enable_ptr = 0;
        }
    }
    pthread_mutex_unlock(&cursor.lock);
}

// ============================================================================
// THREADS
// ============================================================================

void* threadAtualizacaoDisplay(void* arg) {
    int contador = 0;
    
    while (programa_rodando) {
        if (!imagem_carregada) {
            struct timespec ts = {0, 50000000};
            nanosleep(&ts, NULL);
            continue;
        }
        
        if (precisa_copia_direta) {
            aplicarCopiaDireta();
            precisa_copia_direta = 0;
        }
        
        if (mostrar_cursor) {
            atualizarCursorHardware();
            atualizarSelecaoHardware();
            
            if (contador % 30 == 0 && nivel_zoom == 2) {
                pthread_mutex_lock(&cursor.lock);
                printf("\r[%3d, %3d]  ", cursor.x_img, cursor.y_img);
                if (selecao.arrastando) {
                    printf("Arrastando...");
                } else if (selecao.ativa) {
                    printf("Regiao pronta");
                }
                fflush(stdout);
                pthread_mutex_unlock(&cursor.lock);
            }
            contador++;
            struct timespec ts = {0, 30000000};
            nanosleep(&ts, NULL);
            
        } else {
            struct timespec ts = {0, 100000000};
            nanosleep(&ts, NULL);
        }
    }
    return NULL;
}

char* encontrarMouse() {
    static char device_path[256];
    char name[256];
    int fd;
    
    for (int i = 0; i < 20; i++) {
        snprintf(device_path, sizeof(device_path), "/dev/input/event%d", i);
        fd = open(device_path, O_RDONLY | O_NONBLOCK);
        if (fd < 0) continue;
        
        if (ioctl(fd, EVIOCGNAME(sizeof(name)), name) >= 0) {
            if (strstr(name, "ouse") || strstr(name, "Mouse") || 
                strstr(name, "MOUSE") || strstr(name, "Pointer")) {
                close(fd);
                return device_path;
            }
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
               struct timespec ts;
               ts.tv_sec = 0;
               ts.tv_nsec = 1000000;
               nanosleep(&ts, NULL);
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
            else if (ev.code == REL_WHEEL) {
                if (ev.value > 0) {
                    pthread_mutex_unlock(&cursor.lock);
                    printf("\nSCROLL UP: ");
                    aplicarZoomIn();
                    pthread_mutex_lock(&cursor.lock);
                } else if (ev.value < 0) {
                    pthread_mutex_unlock(&cursor.lock);
                    printf("\nSCROLL DOWN: ");
                    aplicarZoomOut();
                    pthread_mutex_lock(&cursor.lock);
                }
            }
        }
        else if (ev.type == EV_KEY) {
            if (ev.code == BTN_LEFT && nivel_zoom == 2) {
                if (ev.value && !btn_left_anterior) {
                    selecao.x_inicio = cursor.x_img;
                    selecao.y_inicio = cursor.y_img;
                    selecao.arrastando = 1;
                    selecao.ativa = 0;
                    em_modo_corte = 1;
                    printf("\nIniciando corte em (%d, %d)\n", cursor.x_img, cursor.y_img);
                }
                else if (!ev.value && btn_left_anterior && selecao.arrastando) {
                    selecao.x_fim = cursor.x_img;
                    selecao.y_fim = cursor.y_img;
                    selecao.arrastando = 0;
                    selecao.ativa = 1;
                    
                    pthread_mutex_unlock(&cursor.lock);
                    aplicarCorte();
                    pthread_mutex_lock(&cursor.lock);
                }
                btn_left_anterior = ev.value;
            }
            else if (ev.code == BTN_RIGHT && ev.value && em_modo_corte) {
                selecao.arrastando = 0;
                selecao.ativa = 0;
                em_modo_corte = 0;
                printf("\nCorte cancelado\n");
            }
        }
        
        pthread_mutex_unlock(&cursor.lock);
    }
    
    close(fd);
    return NULL;
}

// ============================================================================
// CONFIGURAÇÃO INICIAL
// ============================================================================

void configurarAlgoritmos() {
    int opcao;
    
    printf("================================================\n");
    printf("        ESCOLHA OS ALGORITMOS\n");
    printf("================================================\n\n");
    
    printf("Para ZOOM IN\n");
    printf("1. Replicacao\n");
    printf("2. Vizinho Mais Próximo\n");
    printf("Escolha (1 ou 2): ");
    
    scanf("%d", &opcao);
    while (getchar() != '\n');
    
    if (opcao == 2) {
        algoritmo_zoom_in = 1;
        printf("Zoom IN: Vizinho Mais Proximo\n");
    } else {
        algoritmo_zoom_in = 0;
        printf("Zoom IN: Replicacao\n");
    }
    
    printf("\n");
    
    printf("Para ZOOM OUT\n");
    printf("1. Decimacao\n");
    printf("2. Media\n");
    printf("Escolha (1 ou 2): ");
    
    scanf("%d", &opcao);
    while (getchar() != '\n');
    
    if (opcao == 2) {
        algoritmo_zoom_out = 1;
        printf("Zoom OUT: Media\n");
    } else {
        algoritmo_zoom_out = 0;
        printf("Zoom OUT: Decimacao\n");
    }
    
    printf("Pressione ENTER para iniciar...");
    getchar();
}

// ============================================================================
// FUNÇÃO PRINCIPAL
// ============================================================================

int main() {
    printf("================================================\n");
    printf("   SISTEMA DE ZOOM DINAMICO\n");
    printf("================================================\n\n");
    
    configurarAlgoritmos();
    
    IMAGE_MEM_BASE_VAL = ONCHIP_MEMORY2_1_BASE;
    CONTROL_PIO_BASE_VAL = PIO_LED_BASE;
    
    int bytes = carregarImagemMIF(IMAGE_PATH);
    if (bytes < 0) {
        perror("Erro ao carregar imagem");
        return 1;
    }
    printf("Imagem carregada: %d bytes\n", bytes);
    
    if (mapearPonte() < 0) {
        perror("Erro ao mapear ponte");
        return 1;
    }
    printf("Ponte FPGA mapeada\n");
    
    if (mapear_pios_cursor() < 0) {
        printf("PIOs do cursor nao mapeados\n");
    } else {
        printf("PIOs do cursor mapeados\n");
    }
    
    transferirImagemFPGA(bytes);
    printf("Imagem transferida para FPGA\n\n");
    
    fpga_framebuffer = (unsigned char*)IMAGE_MEM_ptr;
    imagem_original = (unsigned char*)malloc(IMG_WIDTH * IMG_HEIGHT);
    imagem_backup = (unsigned char*)malloc(IMG_WIDTH * IMG_HEIGHT);
    
    if (!imagem_original || !imagem_backup) {
        perror("Erro ao alocar buffers");
        desmapear_pios_cursor();
        limparRecursos();
        return 1;
    }
    
    memcpy(imagem_original, fpga_framebuffer, IMG_WIDTH * IMG_HEIGHT);
    memcpy(imagem_backup, fpga_framebuffer, IMG_WIDTH * IMG_HEIGHT);
    imagem_carregada = 1;
    
    char* mouse_path = encontrarMouse();
    if (mouse_path) {
        printf("Mouse detectado: %s\n", mouse_path);
        pthread_create(&thread_mouse, NULL, threadLeituraMouseUSB, (void*)mouse_path);
        pthread_create(&thread_desenho, NULL, threadAtualizacaoDisplay, NULL);
        
        modo_selecao = 1;
        mostrar_cursor = 1;
    } else {
        printf("Mouse nao detectado\n");
    }
    
    printf("\n================================================\n");
    printf("   EXIBINDO IMAGEM ORIGINAL 160x120\n");
    printf("   (Modo Copia Direta)\n");
    printf("================================================\n");
    
    aplicarCopiaDireta();
    
    if (mouse_path) {
        printf("\nCoordenadas do mouse:\n");
        printf("• Corte disponivel apenas em 160x120 (Copia Direta)\n");
        printf("• Scroll UP: Aumenta zoom\n");
        printf("• Scroll DOWN: Diminui zoom\n");
    }
    
    int opcao = -1;
    
    while (opcao != 0) {
        printf("\n================================================\n");
        printf("STATUS: ");
        switch(nivel_zoom) {
            case 0: printf("40x30 (0.25x)"); break;
            case 1: printf("80x60 (0.5x)"); break;
            case 2: printf("160x120 (ORIGINAL - Copia Direta)"); break;
            case 3: printf("320x240 (2x)"); break;
            case 4: printf("640x480 (4x)"); break;
            default: printf("Desconhecido (%d)", nivel_zoom); break;
        }
        
        if (nivel_zoom == 2 && mouse_path) {
            printf(" | CORTE DISPONIVEL");
            if (selecao.arrastando) printf(" | ARRASTANDO");
            else if (selecao.ativa) printf(" | REGIAO PRONTA");
        }
        
        printf("\n================================================\n");
        printf("COMANDOS:\n");
        printf("1 - Reset para imagem original completa\n");
        printf("0 - Sair\n");
        printf("================================================\n");
        printf("Opcao: ");
        
        if (scanf("%d", &opcao) != 1) {
            while (getchar() != '\n');
            printf("\nEntrada invalida. Digite 0 ou 1.\n");
            continue;
        }
        while (getchar() != '\n');
        
        if (opcao == 0) {
            printf("\nEncerrando sistema...\n");
            break;
        }
        
        if (opcao == 1) {
            resetParaOriginal();
        } else {
            printf("\nOpcao invalida. Digite 0 ou 1.\n");
        }
    }
    
    printf("\nFinalizando threads...\n");
    programa_rodando = 0;
    
    if (mouse_path) {
        pthread_join(thread_mouse, NULL);
        pthread_join(thread_desenho, NULL);
        printf("Threads finalizadas\n");
    }
    
    if (imagem_original) {
        free(imagem_original);
        printf("Buffer imagem_original liberado\n");
    }
    
    if (imagem_backup) {
        free(imagem_backup);
        printf("Buffer imagem_backup liberado\n");
    }
    
    desmapear_pios_cursor();
    pthread_mutex_destroy(&cursor.lock);
    limparRecursos();
    
    printf("\n================================================\n");
    printf("Sistema encerrado com sucesso\n");
    printf("================================================\n");
    
    return 0;
}

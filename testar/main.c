// main.c - Sistema Simplificado Corrigido

#define _DEFAULT_SOURCE

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
static int nivel_zoom = 0;            // 0=160x120, 1=2x, 2=4x, -1=1/2x, -2=1/4x
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
    printf("[HARDWARE] Enviando código: 0x%04X\n", codigo);
    enviarComando(codigo);
    usleep(200000); // Mais tempo para processar
}

void aplicarZoomIn() {
    printf("\n🔍 ZOOM IN: ");
    
    if (nivel_zoom >= 2) {
        printf("Máximo atingido (4x)\n");
        return;
    }
    
    nivel_zoom++;
    
    // Sai do modo de corte quando muda o zoom
    if (em_modo_corte) {
        em_modo_corte = 0;
        selecao.arrastando = 0;
        selecao.ativa = 0;
        precisa_copia_direta = 0;
    }
    
    // Desativa cursor quando não está em 160x120
    if (nivel_zoom != 0) {
        modo_selecao = 0;
        mostrar_cursor = 0;
    }
    
    int codigo = -1;
    
    if (nivel_zoom == 1) {
        // Zoom 2x
        if (algoritmo_zoom_in == 0) {
            printf("Replicação 2x\n");
            codigo = obterCodigoEstado(2);  // ST_REPLICACAO
        } else {
            printf("Zoom NN 2x\n");
            codigo = obterCodigoEstado(4);  // ST_ZOOMNN
        }
    } else if (nivel_zoom == 2) {
        // Zoom 4x
        if (algoritmo_zoom_in == 0) {
            printf("Replicação 4x\n");
            codigo = obterCodigoEstado(7);  // ST_REPLICACAO4
        } else {
            printf("Zoom NN 4x\n");
            codigo = obterCodigoEstado(9);  // ST_ZOOMNN4
        }
    }
    
    if (codigo >= 0) {
        aplicarAlgoritmo(codigo);
        memcpy(imagem_original, fpga_framebuffer, IMG_WIDTH * IMG_HEIGHT);
    }
}

void aplicarZoomOut() {
    printf("\n🔍 ZOOM OUT: ");
    
    if (nivel_zoom <= -2) {
        printf("Mínimo atingido (1/4x)\n");
        return;
    }
    
    nivel_zoom--;
    
    // Sai do modo de corte quando muda o zoom
    if (em_modo_corte) {
        em_modo_corte = 0;
        selecao.arrastando = 0;
        selecao.ativa = 0;
        precisa_copia_direta = 0;
    }
    
    // Ativa cursor quando volta para 160x120
    if (nivel_zoom == 0) {
        modo_selecao = 1;
        mostrar_cursor = 1;
        precisa_copia_direta = 1; // Precisa aplicar cópia direta
    } else {
        modo_selecao = 0;
        mostrar_cursor = 0;
    }
    
    int codigo = -1;
    
    if (nivel_zoom == -1) {
        // Zoom 1/2x
        if (algoritmo_zoom_out == 0) {
            printf("Decimação 2x\n");
            codigo = obterCodigoEstado(3);  // ST_DECIMACAO
        } else {
            printf("Média 2x\n");
            codigo = obterCodigoEstado(5);  // ST_MEDIA
        }
    } else if (nivel_zoom == -2) {
        // Zoom 1/4x
        if (algoritmo_zoom_out == 0) {
            printf("Decimação 4x\n");
            codigo = obterCodigoEstado(8);  // ST_DECIMACAO4
        } else {
            printf("Média 4x\n");
            codigo = obterCodigoEstado(10); // ST_MED4
        }
    }
    
    if (codigo >= 0) {
        aplicarAlgoritmo(codigo);
        memcpy(imagem_original, fpga_framebuffer, IMG_WIDTH * IMG_HEIGHT);
    }
}

void aplicarCopiaDireta() {
    printf("\n📸 Aplicando CÓPIA DIRETA (160x120)\n");
    int codigo = obterCodigoEstado(6); // ST_COPIA_DIRETA
    if (codigo >= 0) {
        aplicarAlgoritmo(codigo);
        memcpy(imagem_original, fpga_framebuffer, IMG_WIDTH * IMG_HEIGHT);
    }
}

void resetParaOriginal() {
    printf("\n🔄 RESET para imagem original\n");
    
    // Aplica reset no hardware
    int codigo = obterCodigoEstado(1); // ST_RESET
    if (codigo >= 0) {
        aplicarAlgoritmo(codigo);
    }
    
    // Restaura buffers
    memcpy(imagem_original, imagem_backup, IMG_WIDTH * IMG_HEIGHT);
    memcpy(fpga_framebuffer, imagem_backup, IMG_WIDTH * IMG_HEIGHT);
    
    // Aplica cópia direta para exibir
    aplicarCopiaDireta();
    
    // Reseta variáveis
    nivel_zoom = 0;
    modo_selecao = 1;
    mostrar_cursor = 1;
    em_modo_corte = 0;
    selecao.ativa = 0;
    selecao.arrastando = 0;
    precisa_copia_direta = 0;
    
    printf("✅ Imagem original (160x120) restaurada\n");
}

// ============================================================================
// FUNÇÕES DE CORTE
// ============================================================================

void aplicarCorte() {
    printf("\n✂️  APLICANDO CORTE\n");
    printf("Região: (%d,%d) até (%d,%d)\n",
           selecao.x_inicio, selecao.y_inicio,
           selecao.x_fim, selecao.y_fim);
    
    int x_min = (selecao.x_inicio < selecao.x_fim) ? selecao.x_inicio : selecao.x_fim;
    int x_max = (selecao.x_inicio < selecao.x_fim) ? selecao.x_fim : selecao.x_inicio;
    int y_min = (selecao.y_inicio < selecao.y_fim) ? selecao.y_inicio : selecao.y_fim;
    int y_max = (selecao.y_inicio < selecao.y_fim) ? selecao.y_fim : selecao.y_inicio;
    
    int largura = x_max - x_min + 1;
    int altura = y_max - y_min + 1;
    
    printf("Dimensões: %dx%d pixels\n", largura, altura);
    
    // Corta e centraliza
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
    
    // Envia para FPGA e exibe com cópia direta
    memcpy(fpga_framebuffer, imagem_original, IMG_WIDTH * IMG_HEIGHT);
    aplicarCopiaDireta();
    
    printf("✅ Corte aplicado e centralizado\n");
    
    // Limpa seleção
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
            usleep(50000);
            continue;
        }
        
        // Aplica cópia direta se necessário
        if (precisa_copia_direta) {
            aplicarCopiaDireta();
            precisa_copia_direta = 0;
        }
        
        if (mostrar_cursor) {
            atualizarCursorHardware();
            atualizarSelecaoHardware();
            
            // Mostra coordenadas periodicamente
            if (contador % 30 == 0 && nivel_zoom == 0) {
                pthread_mutex_lock(&cursor.lock);
                printf("\r🖱️  [%3d, %3d]  ", cursor.x_img, cursor.y_img);
                if (selecao.arrastando) {
                    printf("🔄 Arrastando...");
                } else if (selecao.ativa) {
                    printf("✅ Região pronta");
                }
                fflush(stdout);
                pthread_mutex_unlock(&cursor.lock);
            }
            contador++;
            
            usleep(16000);
        } else {
            usleep(100000);
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
            else if (ev.code == REL_WHEEL) {
                // Scroll do mouse
                if (ev.value > 0) {
                    // Scroll UP = Zoom IN
                    pthread_mutex_unlock(&cursor.lock);
                    aplicarZoomIn();
                    pthread_mutex_lock(&cursor.lock);
                } else if (ev.value < 0) {
                    // Scroll DOWN = Zoom OUT
                    pthread_mutex_unlock(&cursor.lock);
                    aplicarZoomOut();
                    pthread_mutex_lock(&cursor.lock);
                }
            }
        }
        else if (ev.type == EV_KEY) {
            if (ev.code == BTN_LEFT && nivel_zoom == 0) {
                // Só processa cliques quando na resolução original
                if (ev.value && !btn_left_anterior) {
                    // Início do arrasto
                    selecao.x_inicio = cursor.x_img;
                    selecao.y_inicio = cursor.y_img;
                    selecao.arrastando = 1;
                    selecao.ativa = 0;
                    em_modo_corte = 1;
                    printf("\n✂️  Iniciando corte em (%d, %d)\n", cursor.x_img, cursor.y_img);
                }
                else if (!ev.value && btn_left_anterior && selecao.arrastando) {
                    // Fim do arrasto
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
                // Botão direito cancela
                selecao.arrastando = 0;
                selecao.ativa = 0;
                em_modo_corte = 0;
                printf("\n❌ Corte cancelado\n");
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
    
    printf("════════════════════════════════════════════════\n");
    printf("        ESCOLHA OS ALGORITMOS\n");
    printf("════════════════════════════════════════════════\n\n");
    
    // Zoom IN
    printf("Para ZOOM IN (aumentar resolução):\n");
    printf("1. Replicação (mais rápido, menos qualidade)\n");
    printf("2. Vizinho Mais Próximo (mais lento, melhor qualidade)\n");
    printf("Escolha (1 ou 2): ");
    
    scanf("%d", &opcao);
    while (getchar() != '\n');
    
    if (opcao == 2) {
        algoritmo_zoom_in = 1;
        printf("✅ Zoom IN: Vizinho Mais Próximo\n");
    } else {
        algoritmo_zoom_in = 0;
        printf("✅ Zoom IN: Replicação\n");
    }
    
    printf("\n");
    
    // Zoom OUT
    printf("Para ZOOM OUT (reduzir resolução):\n");
    printf("1. Decimação (mais rápido, menos qualidade)\n");
    printf("2. Média (mais lento, melhor qualidade)\n");
    printf("Escolha (1 ou 2): ");
    
    scanf("%d", &opcao);
    while (getchar() != '\n');
    
    if (opcao == 2) {
        algoritmo_zoom_out = 1;
        printf("✅ Zoom OUT: Média\n");
    } else {
        algoritmo_zoom_out = 0;
        printf("✅ Zoom OUT: Decimação\n");
    }
    
    printf("\n════════════════════════════════════════════════\n");
    printf("CONFIGURAÇÃO FINAL:\n");
    printf("• Zoom IN: %s\n", algoritmo_zoom_in ? "Vizinho Mais Próximo" : "Replicação");
    printf("• Zoom OUT: %s\n", algoritmo_zoom_out ? "Média" : "Decimação");
    printf("════════════════════════════════════════════════\n\n");
    
    printf("INSTRUÇÕES:\n");
    printf("• Sistema inicia em 160x120 (Cópia Direta)\n");
    printf("• Scroll UP: Aumenta zoom (usando algoritmo escolhido)\n");
    printf("• Scroll DOWN: Diminui zoom (usando algoritmo escolhido)\n");
    printf("• Quando em 160x120: Arraste para cortar região\n");
    printf("• Corte é aplicado automaticamente ao soltar\n");
    printf("• Botão direito: Cancela corte\n");
    printf("• Tecla 1: Reset para imagem original\n");
    printf("• Tecla 0: Sair\n");
    printf("════════════════════════════════════════════════\n\n");
    
    printf("Pressione ENTER para iniciar...");
    getchar();
}

// ============================================================================
// FUNÇÃO PRINCIPAL
// ============================================================================

int main() {
    printf("════════════════════════════════════════════════\n");
    printf("   SISTEMA DE ZOOM DINÂMICO\n");
    printf("════════════════════════════════════════════════\n\n");
    
    // Usuário escolhe algoritmos
    configurarAlgoritmos();
    
    // Inicialização do sistema
    IMAGE_MEM_BASE_VAL = ONCHIP_MEMORY2_1_BASE;
    CONTROL_PIO_BASE_VAL = PIO_LED_BASE;
    
    // Carrega imagem
    int bytes = carregarImagemMIF(IMAGE_PATH);
    if (bytes < 0) {
        perror("Erro ao carregar imagem");
        return 1;
    }
    printf("✅ Imagem carregada: %d bytes\n", bytes);
    
    // Mapeia memória FPGA
    if (mapearPonte() < 0) {
        perror("Erro ao mapear ponte");
        return 1;
    }
    printf("✅ Ponte FPGA mapeada\n");
    
    // Mapeia PIOs do cursor
    if (mapear_pios_cursor() < 0) {
        printf("⚠️  PIOs do cursor não mapeados\n");
    } else {
        printf("✅ PIOs do cursor mapeados\n");
    }
    
    // Transfere imagem
    transferirImagemFPGA(bytes);
    printf("✅ Imagem transferida para FPGA\n\n");
    
    // Inicializa buffers
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
    
    // Inicializa mouse
    char* mouse_path = encontrarMouse();
    if (mouse_path) {
        printf("✅ Mouse detectado: %s\n", mouse_path);
        pthread_create(&thread_mouse, NULL, threadLeituraMouseUSB, (void*)mouse_path);
        pthread_create(&thread_desenho, NULL, threadAtualizacaoDisplay, NULL);
        
        // Ativa cursor
        modo_selecao = 1;
        mostrar_cursor = 1;
    } else {
        printf("⚠️  Mouse não detectado\n");
    }
    
    // Mostra imagem inicial (160x120) com Cópia Direta
    printf("\n════════════════════════════════════════════════\n");
    printf("   EXIBINDO IMAGEM ORIGINAL 160x120\n");
    printf("   (Modo Cópia Direta)\n");
    printf("════════════════════════════════════════════════\n");
    
    aplicarCopiaDireta();
    
    if (mouse_path) {
        printf("\nCoordenadas do mouse:\n");
    }
    
    // Loop principal
    int opcao = -1;
    
    while (opcao != 0) {
        printf("\n════════════════════════════════════════════════\n");
        printf("STATUS: ");
        switch(nivel_zoom) {
            case -2: printf("40x30 (1/4x)"); break;
            case -1: printf("80x60 (1/2x)"); break;
            case 0: printf("160x120 (ORIGINAL - Cópia Direta)"); break;
            case 1: printf("320x240 (2x)"); break;
            case 2: printf("640x480 (4x)"); break;
        }
        
        if (nivel_zoom == 0 && mouse_path) {
            printf(" | ✂️  CORTE DISPONÍVEL");
            if (selecao.arrastando) printf(" | 🔄 ARRASTANDO");
            else if (selecao.ativa) printf(" | ✅ REGIÃO PRONTA");
        }
        
        printf("\n════════════════════════════════════════════════\n");
        printf("1 - Reset para imagem original\n");
        printf("0 - Sair\n");
        printf("════════════════════════════════════════════════\n");
        printf("Opção: ");
        
        if (scanf("%d", &opcao) != 1) {
            while (getchar() != '\n');
            continue;
        }
        while (getchar() != '\n');
        
        if (opcao == 0) break;
        
        if (opcao == 1) {
            resetParaOriginal();
        }
    }
    
    // Cleanup
    programa_rodando = 0;
    
    if (mouse_path) {
        pthread_join(thread_mouse, NULL);
        pthread_join(thread_desenho, NULL);
    }
    
    if (imagem_original) free(imagem_original);
    if (imagem_backup) free(imagem_backup);
    
    desmapear_pios_cursor();
    pthread_mutex_destroy(&cursor.lock);
    limparRecursos();
    
    printf("\n✅ Sistema encerrado\n");
    return 0;
}

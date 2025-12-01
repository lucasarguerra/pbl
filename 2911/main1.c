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
#define CURSOR_SIZE 2           
#define IMG_WIDTH 160           
#define IMG_HEIGHT 120          
#define SCREEN_WIDTH 640        
#define SCREEN_HEIGHT 480       
#define CURSOR_COLOR 0xFF       


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
volatile int modo_selecao = 0; 
volatile int mostrar_cursor = 0; 
pthread_t thread_mouse, thread_desenho;
unsigned char* imagem_original = NULL;
unsigned char* imagem_backup = NULL;
unsigned char* fpga_framebuffer = NULL;
int imagem_carregada = 0;

// Variáveis para controle de navegação por escala
int algoritmo_zoom_in = 2;   // 2=Replicação, 4=Vizinho
int algoritmo_zoom_out = 3;  // 3=Decimação, 5=Média
int escala_atual = 0;        // -2=zoom_out_4x, -1=zoom_out_2x, 0=copia_direta, 1=zoom_in_2x, 2=zoom_in_4x


void atualizarCoordenadaImagem() {
    cursor.x_img = (cursor.x_screen * IMG_WIDTH) / SCREEN_WIDTH;
    cursor.y_img = (cursor.y_screen * IMG_HEIGHT) / SCREEN_HEIGHT;
    
    if (cursor.x_img < 0) cursor.x_img = 0;
    if (cursor.x_img >= IMG_WIDTH) cursor.x_img = IMG_WIDTH - 1;
    if (cursor.y_img < 0) cursor.y_img = 0;
    if (cursor.y_img >= IMG_HEIGHT) cursor.y_img = IMG_HEIGHT - 1;
}

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
        
    int btn_left_anterior = 0;
    
    while (programa_rodando) {
        ssize_t bytes = read(fd, &ev, sizeof(struct input_event));
        
        if (bytes < (ssize_t)sizeof(struct input_event)) {
            if (errno == EAGAIN || errno == EINTR) {
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
                
                if (modo_selecao) {
                    if (ev.value && !btn_left_anterior) {
                        selecao.x_inicio = cursor.x_img;
                        selecao.y_inicio = cursor.y_img;
                        selecao.arrastando = 1;
                        printf("\nIniciando em (%d, %d)\n", 
                               cursor.x_img, cursor.y_img);
                    }
                    else if (!ev.value && btn_left_anterior) {
                        selecao.x_fim = cursor.x_img;
                        selecao.y_fim = cursor.y_img;
                        selecao.arrastando = 0;
                        selecao.ativa = 1;
                        printf("Finalizando em (%d, %d)\n", 
                               cursor.x_img, cursor.y_img);
                        printf("Seleção de (%d,%d) até (%d,%d)\n", 
                               selecao.x_inicio, selecao.y_inicio,
                               selecao.x_fim, selecao.y_fim);
                    }
                }
                
                btn_left_anterior = ev.value;
            }
            else if (ev.code == BTN_RIGHT) {
                cursor.button_right = ev.value;
                
                if (ev.value && modo_selecao) {
                    selecao.ativa = 0;
                    selecao.arrastando = 0;
                    printf("\nSeleção Cancelada\n");
                }
            }
        }
        
        pthread_mutex_unlock(&cursor.lock);
    }
    
    close(fd);
    return NULL;
}


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
    
    int largura_regiao = x_max - x_min + 1;
    int altura_regiao = y_max - y_min + 1;
        
    int offset_x = (IMG_WIDTH - largura_regiao) / 2;
    int offset_y = (IMG_HEIGHT - altura_regiao) / 2;
    
    memset(destino, 0x00, IMG_WIDTH * IMG_HEIGHT);
    
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
    int contador_fps = 0;
    
    while (programa_rodando) {
        if (!imagem_carregada) {
            usleep(50000);
            continue;
        }
        
        if (mostrar_cursor) {
            pthread_mutex_lock(&cursor.lock);
            int x = cursor.x_img;
            int y = cursor.y_img;
            int arrastando = selecao.arrastando;
            pthread_mutex_unlock(&cursor.lock);
            
            memcpy(fpga_framebuffer, imagem_original, IMG_WIDTH * IMG_HEIGHT);
            
            if (arrastando && modo_selecao) {
                desenharRetanguloSelecao(fpga_framebuffer, selecao.x_inicio, 
                                        selecao.y_inicio, x, y);
            }
            else if (selecao.ativa && modo_selecao) {
                desenharRetanguloSelecao(fpga_framebuffer, selecao.x_inicio, 
                                        selecao.y_inicio, selecao.x_fim, selecao.y_fim);
            }
            
            desenharCursor(fpga_framebuffer, x, y);
            
            if (contador_fps % 15 == 0) {
                printf("\r🖱️  Posição: [%3d, %3d] (160x120)   ", x, y);
                fflush(stdout);
            }
            contador_fps++;            
        } else {
            contador_fps = 0;
        }
    }
    
    printf("\n");
    return NULL;
}

const char* obterNomeEscala() {
    switch(escala_atual) {
        case 2: return "Zoom In 4x";
        case 1: return "Zoom In 2x";
        case 0: return "Cópia Direta (1:1)";
        case -1: return "Zoom Out 2x";
        case -2: return "Zoom Out 4x";
        default: return "Desconhecido";
    }
}

const char* obterNomeAlgoritmo(int codigo) {
    switch(codigo) {
        case 2: return "Replicação";
        case 3: return "Decimação";
        case 4: return "Vizinho Mais Próximo";
        case 5: return "Média";
        default: return "";
    }
}

void exibirMenu() {
    printf("\n╔════════════════════════════════════════════════╗\n");
    printf("║     Sistema de Navegação por Escalas          ║\n");
    printf("╚════════════════════════════════════════════════╝\n");
    printf("\n📊 ESCALA ATUAL: %s\n", obterNomeEscala());
    printf("   Zoom In:  %s\n", obterNomeAlgoritmo(algoritmo_zoom_in));
    printf("   Zoom Out: %s\n", obterNomeAlgoritmo(algoritmo_zoom_out));
    printf("\n════════════════════════════════════════════════\n");
    printf("  +  - Zoom In (aumentar escala)\n");
    printf("  -  - Zoom Out (diminuir escala)\n");
    printf("  6  - Modo Seleção de Região\n");
    printf("  1  - Reset (Volta imagem original)\n");
    printf("  0  - Sair\n");
    printf("════════════════════════════════════════════════\n");
    
    if (modo_selecao) {
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
    }
    
    printf("\n▶ Opção: ");
}

void aplicarEscala() {
    int codigo;
    
    if (escala_atual == 2) {
        // Zoom In 4x
        codigo = (algoritmo_zoom_in == 2) ? 7 : 9; // Replicação 4x : Vizinho 4x
    } else if (escala_atual == 1) {
        // Zoom In 2x
        codigo = algoritmo_zoom_in; // 2 ou 4
    } else if (escala_atual == 0) {
        // Cópia Direta - não faz nada
        return;
    } else if (escala_atual == -1) {
        // Zoom Out 2x
        codigo = algoritmo_zoom_out; // 3 ou 5
    } else if (escala_atual == -2) {
        // Zoom Out 4x
        codigo = (algoritmo_zoom_out == 3) ? 8 : 10; // Decimação 4x : Média 4x
    }
    
    printf("⚙️  Aplicando %s...\n", obterNomeEscala());
    enviarComando(obterCodigoEstado(codigo));
    usleep(150000);
    memcpy(imagem_original, fpga_framebuffer, IMG_WIDTH * IMG_HEIGHT);
    printf("✅ Transformação aplicada!\n");
}


int main() {
    char* mouse_path;
    char opcao_char;
        
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
    
    printf("\n▶ Escolha o algoritmo para ZOOM IN:\n");
    printf("  1 - Replicação\n");
    printf("  2 - Vizinho Mais Próximo\n");
    printf("Opção: ");
    
    int escolha;
    scanf("%d", &escolha);
    algoritmo_zoom_in = (escolha == 2) ? 4 : 2;
    
    printf("\n▶ Escolha o algoritmo para ZOOM OUT:\n");
    printf("  1 - Decimação\n");
    printf("  2 - Média\n");
    printf("Opção: ");
    
    scanf("%d", &escolha);
    algoritmo_zoom_out = (escolha == 2) ? 5 : 3;
    
    printf("   Zoom In:  %s\n", obterNomeAlgoritmo(algoritmo_zoom_in));
    printf("   Zoom Out: %s\n", obterNomeAlgoritmo(algoritmo_zoom_out));
    
    mouse_path = encontrarMouse();
    
    if (!mouse_path) {
        fprintf(stderr, "\nMouse não detectado!\n");
    } else {        
        if (pthread_create(&thread_mouse, NULL, threadLeituraMouseUSB, (void*)mouse_path) != 0) {
            perror("Erro ao criar thread do mouse");
            mouse_path = NULL;
        }
        if (pthread_create(&thread_desenho, NULL, threadAtualizacaoDisplay, NULL) != 0) {
            perror("Erro ao criar thread de desenho");
        }
    }
    
    while (1) {
        exibirMenu();
        
        scanf(" %c", &opcao_char);
        
        if (opcao_char == '0') {
            break;
        }
        
        if (opcao_char == '1') {
            memcpy(imagem_original, imagem_backup, IMG_WIDTH * IMG_HEIGHT);
            memcpy(fpga_framebuffer, imagem_backup, IMG_WIDTH * IMG_HEIGHT);
            
            modo_selecao = 0;
            mostrar_cursor = 0;
            selecao.ativa = 0;
            selecao.arrastando = 0;
            escala_atual = 0;
            
            enviarComando(obterCodigoEstado(1));
            continue;
        }
        
        if (opcao_char == '+') {
            if (escala_atual < 2) {
                escala_atual++;
                aplicarEscala();
            } else {
                printf("\nImagem no zoom máximo\n");
            }
            continue;
        }
        
        if (opcao_char == '-') {
            if (escala_atual > -2) {
                escala_atual--;
                aplicarEscala();
            } else {
                printf("\nImagem está no zoom mínimo\n");
            }
            continue;
        }
        
        if (opcao_char == '6') {
            if (!mouse_path) {
                continue;
            }
            
            if (!modo_selecao) {
                
                memcpy(imagem_original, imagem_backup, IMG_WIDTH * IMG_HEIGHT);
                memcpy(fpga_framebuffer, imagem_backup, IMG_WIDTH * IMG_HEIGHT);
                                
                modo_selecao = 1;
                mostrar_cursor = 1;
                selecao.ativa = 0;
                selecao.arrastando = 0;
                escala_atual = 0;
                 
                continue;
                
            } else if (!selecao.ativa) {
                printf("\nNenhuma região selecionada ainda.\n");
                printf("Posição atual: (%d, %d)\n", cursor.x_img, cursor.y_img);
                printf("Arraste o mouse para selecionar uma região.\n");
                continue;
                
            } else {                
                printf("▶ Região selecionada:\n");
                printf("  Início: (%d, %d)\n", selecao.x_inicio, selecao.y_inicio);
                printf("  Fim:    (%d, %d)\n", selecao.x_fim, selecao.y_fim);
                
                aplicarMascaraSelecao(imagem_original, imagem_backup,
                                     selecao.x_inicio, selecao.y_inicio,
                                     selecao.x_fim, selecao.y_fim);
                
                memcpy(fpga_framebuffer, imagem_original, IMG_WIDTH * IMG_HEIGHT);
                               
                modo_selecao = 0;
                mostrar_cursor = 0;
                selecao.ativa = 0;
                
                continue;
            }
        }
    }
    
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

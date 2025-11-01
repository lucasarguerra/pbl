.syntax unified
    .arch armv7-a
    .text
    .align 2
    
@ ============================================================================
@ Definições de constantes
@ ============================================================================

    .equ LW_BRIDGE_BASE, 0xFF200000
    .equ LW_BRIDGE_SPAN, 0x30000
    .equ IMAGE_MEM_SIZE_DEFAULT, 0xC000    @ 49152 bytes - AJUSTE SE NECESSÁRIO
    
    .equ ST_RESET, 7
    .equ ST_REPLICACAO, 0
    .equ ST_DECIMACAO, 1
    .equ ST_ZOOMNN, 2
    .equ ST_MEDIA, 3
    .equ ST_COPIA_DIRETA, 4
    .equ ST_REPLICACAO4, 8
    .equ ST_DECIMACAO4, 9
    .equ ST_ZOOMNN4, 10
    .equ ST_MED4, 11
    
    .equ O_RDWR, 0x2
    .equ O_SYNC, 0x101000
    .equ PROT_READ, 0x1
    .equ PROT_WRITE, 0x2
    .equ MAP_SHARED, 0x1

@ ============================================================================
@ Declarações de funções exportadas
@ ============================================================================

    .global carregarImagemMIF
    .global mapearPonte
    .global transferirImagemFPGA
    .global enviarComando
    .global limparRecursos
    .global obterCodigoEstado
    .type carregarImagemMIF, %function
    .type mapearPonte, %function
    .type transferirImagemFPGA, %function
    .type enviarComando, %function
    .type limparRecursos, %function
    .type obterCodigoEstado, %function

@ ============================================================================
@ Função: carregarImagemMIF
@ Argumentos: r0 = path (const char*)
@ Retorno: r0 = número de bytes carregados ou -1 em erro
@ ============================================================================

carregarImagemMIF:
    @ Argumentos: r0 = path (const char*)
    @ Retorno: r0 = número de bytes carregados ou -1 em erro
    
    push    {r4-r11, lr}        @ Salva registradores
    sub     sp, sp, #144        @ Aloca espaço na stack
    
    mov     r4, r0              @ r4 = path
    
    @ Carrega IMAGE_MEM_SIZE (do hps_0.h via define)
    ldr     r11, =0xC000        @ r11 = IMAGE_MEM_SIZE (49152 bytes típico)
                                 @ AJUSTE ESTE VALOR SE NECESSÁRIO!
    
    @ FILE *img_file = fopen(path, "r");
    mov     r0, r4
    ldr     r1, =mode_r
    bl      fopen
    mov     r5, r0              @ r5 = img_file
    
    @ if (!img_file) return -1;
    cmp     r5, #0
    beq     return_error
    
    @ hps_img_buffer = malloc(IMAGE_MEM_SIZE);
    mov     r0, r11             @ Usa o valor carregado
    bl      malloc
    ldr     r6, =hps_img_buffer
    str     r0, [r6]            @ Salva o ponteiro em hps_img_buffer
    mov     r7, r0              @ r7 = hps_img_buffer (ponteiro local)
    
    @ if (!hps_img_buffer) { fclose(img_file); return -1; }
    cmp     r7, #0
    beq     close_and_error
    
    @ int index = 0
    mov     r8, #0              @ r8 = index
    
loop_fgets:
    @ while (fgets(line, sizeof(line), img_file))
    mov     r0, sp
    mov     r1, #128
    mov     r2, r5
    bl      fgets
    
    cmp     r0, #0
    beq     loop_end
    
    @ Verificar strings a ignorar
    mov     r0, sp
    ldr     r1, =str_content
    bl      strstr
    cmp     r0, #0
    bne     loop_fgets
    
    mov     r0, sp
    ldr     r1, =str_begin
    bl      strstr
    cmp     r0, #0
    bne     loop_fgets
    
    mov     r0, sp
    ldr     r1, =str_end
    bl      strstr
    cmp     r0, #0
    bne     loop_fgets
    
    mov     r0, sp
    ldr     r1, =str_addr_radix
    bl      strstr
    cmp     r0, #0
    bne     loop_fgets
    
    mov     r0, sp
    ldr     r1, =str_data_radix
    bl      strstr
    cmp     r0, #0
    bne     loop_fgets
    
    mov     r0, sp
    ldr     r1, =str_width
    bl      strstr
    cmp     r0, #0
    bne     loop_fgets
    
    mov     r0, sp
    ldr     r1, =str_depth
    bl      strstr
    cmp     r0, #0
    bne     loop_fgets
    
    @ if (sscanf(line, "%*x : %x", &value) == 1)
    mov     r0, sp
    ldr     r1, =format_str
    add     r2, sp, #128
    bl      sscanf
    
    cmp     r0, #1
    bne     loop_fgets
    
    @ if (index < IMAGE_MEM_SIZE)
    cmp     r8, r11             @ Compara com IMAGE_MEM_SIZE carregado
    bge     loop_fgets
    
    @ hps_img_buffer[index++] = (unsigned char)value;
    ldr     r9, [sp, #128]
    strb    r9, [r7, r8]
    add     r8, r8, #1
    
    b       loop_fgets
    
loop_end:
    @ fclose(img_file);
    mov     r0, r5
    bl      fclose
    
    @ return index;
    mov     r0, r8
    add     sp, sp, #144
    pop     {r4-r11, pc}
    
close_and_error:
    mov     r0, r5
    bl      fclose
    
return_error:
    mvn     r0, #0
    add     sp, sp, #144
    pop     {r4-r11, pc}

    .size carregarImagemMIF, .-carregarImagemMIF

@ ============================================================================
@ Função: mapearPonte
@ Retorno: 0 em sucesso, -1 em erro
@ ============================================================================

mapearPonte:
    push    {r4-r7, lr}         @ Salva registradores
    
    @ Carrega constantes
    ldr     r4, =0xFF200000     @ LW_BRIDGE_BASE
    ldr     r5, =0x30000        @ LW_BRIDGE_SPAN
    
    @ fd = open("/dev/mem", O_RDWR | O_SYNC);
    ldr     r0, =dev_mem_path   @ r0 = "/dev/mem"
    ldr     r1, =0x101002       @ r1 = O_RDWR | O_SYNC (0x2 | 0x101000)
    bl      open
    ldr     r6, =fd             @ r6 = &fd
    str     r0, [r6]            @ fd = resultado do open
    
    @ if (fd == -1) return -1;
    cmn     r0, #1              @ Compara com -1
    beq     mapear_error
    
    mov     r7, r0              @ r7 = fd (salva para usar depois)
    
    @ LW_virtual = mmap(NULL, LW_BRIDGE_SPAN, PROT_READ | PROT_WRITE, MAP_SHARED, fd, LW_BRIDGE_BASE);
    mov     r0, #0              @ NULL
    mov     r1, r5              @ LW_BRIDGE_SPAN
    mov     r2, #3              @ PROT_READ | PROT_WRITE (1 | 2)
    mov     r3, #1              @ MAP_SHARED
    str     r7, [sp, #-8]!      @ fd (5º argumento na stack)
    str     r4, [sp, #4]        @ LW_BRIDGE_BASE (6º argumento na stack)
    bl      mmap
    add     sp, sp, #8          @ Limpa argumentos da stack
    
    ldr     r6, =LW_virtual     @ r6 = &LW_virtual
    str     r0, [r6]            @ LW_virtual = resultado do mmap
    
    @ if (LW_virtual == MAP_FAILED) return -1;
    cmn     r0, #1              @ Compara com MAP_FAILED (-1)
    beq     mapear_error
    
    mov     r4, r0              @ r4 = LW_virtual (base)
    
    @ IMAGE_MEM_ptr = (volatile unsigned char *)(LW_virtual + IMAGE_MEM_BASE);
    ldr     r1, =IMAGE_MEM_BASE_VAL
    ldr     r1, [r1]            @ Carrega IMAGE_MEM_BASE
    add     r1, r4, r1          @ LW_virtual + IMAGE_MEM_BASE
    ldr     r2, =IMAGE_MEM_ptr
    str     r1, [r2]            @ IMAGE_MEM_ptr = resultado
    
    @ CONTROL_PIO_ptr = (volatile unsigned int *)(LW_virtual + CONTROL_PIO_BASE);
    ldr     r1, =CONTROL_PIO_BASE_VAL
    ldr     r1, [r1]            @ Carrega CONTROL_PIO_BASE
    add     r1, r4, r1          @ LW_virtual + CONTROL_PIO_BASE
    ldr     r2, =CONTROL_PIO_ptr
    str     r1, [r2]            @ CONTROL_PIO_ptr = resultado
    
    @ return 0;
    mov     r0, #0
    pop     {r4-r7, pc}
    
mapear_error:
    mvn     r0, #0              @ r0 = -1
    pop     {r4-r7, pc}

    .size mapearPonte, .-mapearPonte

@ ============================================================================
@ Função: transferirImagemFPGA
@ Argumentos: r0 = tamanho (int)
@ Retorno: void
@ ============================================================================

transferirImagemFPGA:
    push    {r4-r6, lr}         @ Salva registradores
    
    mov     r4, r0              @ r4 = tamanho
    
    @ memcpy((void *)IMAGE_MEM_ptr, hps_img_buffer, tamanho);
    @ r0 = destino (IMAGE_MEM_ptr)
    @ r1 = origem (hps_img_buffer)
    @ r2 = tamanho
    
    @ Carrega IMAGE_MEM_ptr (destino)
    ldr     r5, =IMAGE_MEM_ptr
    ldr     r0, [r5]            @ r0 = IMAGE_MEM_ptr (valor do ponteiro)
    
    @ Carrega hps_img_buffer (origem)
    ldr     r5, =hps_img_buffer
    ldr     r1, [r5]            @ r1 = hps_img_buffer (valor do ponteiro)
    
    @ Tamanho
    mov     r2, r4              @ r2 = tamanho
    
    @ Chama memcpy
    bl      memcpy
    
    @ Retorno void (sem valor de retorno)
    pop     {r4-r6, pc}

    .size transferirImagemFPGA, .-transferirImagemFPGA

@ ============================================================================
@ Função: enviarComando
@ Argumentos: r0 = codigo (int)
@ Retorno: void
@ ============================================================================

enviarComando:
    push    {r4, lr}            @ Salva registradores
    
    mov     r4, r0              @ r4 = codigo (salva o argumento)
    
    @ *CONTROL_PIO_ptr = codigo;
    ldr     r0, =CONTROL_PIO_ptr    @ Carrega endereço de CONTROL_PIO_ptr
    ldr     r0, [r0]                @ r0 = valor do ponteiro CONTROL_PIO_ptr
    str     r4, [r0]                @ Escreve o codigo na memória
    
    @ Memory barrier (asm volatile("" ::: "memory");)
    dmb     sy                  @ Data Memory Barrier - garante ordem das escritas
    
    @ usleep(10000);
    ldr     r0, =10000          @ r0 = 10000 microssegundos
    bl      usleep
    
    @ Retorno void
    pop     {r4, pc}

    .size enviarComando, .-enviarComando

@ ============================================================================
@ Função: limparRecursos
@ Retorno: void
@ ============================================================================

limparRecursos:
    push    {r4-r6, lr}         @ Salva registradores
    
    @ if (hps_img_buffer) free(hps_img_buffer);
    ldr     r4, =hps_img_buffer     @ Carrega endereço de hps_img_buffer
    ldr     r0, [r4]                @ r0 = valor do ponteiro hps_img_buffer
    cmp     r0, #0                  @ Verifica se é NULL
    beq     limpar_skip_free        @ Se NULL, pula o free
    
    bl      free                    @ Chama free(hps_img_buffer)
    
    mov     r0, #0                  @ Zera o ponteiro
    str     r0, [r4]                @ hps_img_buffer = NULL
    
limpar_skip_free:
    @ if (LW_virtual != MAP_FAILED) munmap(LW_virtual, LW_BRIDGE_SPAN);
    ldr     r5, =LW_virtual         @ Carrega endereço de LW_virtual
    ldr     r0, [r5]                @ r0 = valor de LW_virtual
    cmn     r0, #1                  @ Compara com MAP_FAILED (-1)
    beq     limpar_skip_munmap      @ Se MAP_FAILED, pula o munmap
    
    ldr     r1, =0x30000            @ r1 = LW_BRIDGE_SPAN
    bl      munmap                  @ Chama munmap(LW_virtual, LW_BRIDGE_SPAN)
    
    mvn     r0, #0                  @ r0 = -1 (MAP_FAILED)
    str     r0, [r5]                @ LW_virtual = MAP_FAILED
    
limpar_skip_munmap:
    @ if (fd != -1) close(fd);
    ldr     r6, =fd                 @ Carrega endereço de fd
    ldr     r0, [r6]                @ r0 = valor de fd
    cmn     r0, #1                  @ Compara com -1
    beq     limpar_skip_close       @ Se -1, pula o close
    
    bl      close                   @ Chama close(fd)
    
    mvn     r0, #0                  @ r0 = -1
    str     r0, [r6]                @ fd = -1
    
limpar_skip_close:
    @ Retorno void
    pop     {r4-r6, pc}

    .size limparRecursos, .-limparRecursos

@ ============================================================================
@ Função: obterCodigoEstado
@ Argumentos: r0 = opcao (int)
@ Retorno: r0 = código do estado ou -1 se inválido
@ ============================================================================

obterCodigoEstado:
    @ Verifica limites: opcao deve estar entre 1 e 10
    cmp     r0, #1
    blt     codigo_invalido         @ Se opcao < 1, retorna -1
    cmp     r0, #10
    bgt     codigo_invalido         @ Se opcao > 10, retorna -1
    
    @ Usa tabela de lookup
    @ r0 contém o índice (1-10), ajusta para 0-9
    sub     r0, r0, #1              @ r0 = opcao - 1
    
    @ Carrega endereço da tabela
    ldr     r1, =tabela_codigos
    
    @ Carrega o código correspondente (4 bytes por entrada)
    ldr     r0, [r1, r0, lsl #2]    @ r0 = tabela_codigos[opcao-1]
    
    bx      lr
    
codigo_invalido:
    mvn     r0, #0                  @ r0 = -1
    bx      lr

    .size obterCodigoEstado, .-obterCodigoEstado

@ ============================================================================
@ Tabela de códigos (lookup table)
@ ============================================================================

    .align 2
tabela_codigos:
    .word   7       @ opcao 1:  ST_RESET
    .word   0       @ opcao 2:  ST_REPLICACAO
    .word   1       @ opcao 3:  ST_DECIMACAO
    .word   2       @ opcao 4:  ST_ZOOMNN
    .word   3       @ opcao 5:  ST_MEDIA
    .word   4       @ opcao 6:  ST_COPIA_DIRETA
    .word   8       @ opcao 7:  ST_REPLICACAO4
    .word   9       @ opcao 8:  ST_DECIMACAO4
    .word   10      @ opcao 9:  ST_ZOOMNN4
    .word   11      @ opcao 10: ST_MED4

@ ============================================================================
@ Dados (strings e constantes)
@ ============================================================================

    .align 2
mode_r:
    .asciz "r"

dev_mem_path:
    .asciz "/dev/mem"
    
str_content:
    .asciz "CONTENT"
    
str_begin:
    .asciz "BEGIN"
    
str_end:
    .asciz "END"
    
str_addr_radix:
    .asciz "ADDRESS_RADIX"
    
str_data_radix:
    .asciz "DATA_RADIX"
    
str_width:
    .asciz "WIDTH"
    
str_depth:
    .asciz "DEPTH"
    
format_str:
    .asciz "%*x : %x"

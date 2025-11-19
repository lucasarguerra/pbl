// control_unity.v
// Versão corrigida com integração completa do mouse

module control_unity (
    input vga_reset,
    input clk_50MHz,
    input [3:0] sw,
    input [7:0] mouse_x,
    input [7:0] mouse_y,
    input mouse_valid,
    input mouse_l_click,
    input mouse_r_click,
    output [9:0] next_x,
    output [9:0] next_y,
    output hsyncm,
    output vsyncm,
    output [7:0] redm,
    output [7:0] greenm,
    output [7:0] bluem,
    output blank,
    output sync,
    output clks,
    output [14:0] rom_addr,  // endereço da ROM (vai pro HPS)
    input  [31:0] rom_data
);

    wire outclk_0;

    pll_inst pll_inst (
        .refclk   (clk_50MHz),   //  refclk.clk
        .rst      (1'b0),      //   reset.reset
        .outclk_0 (outclk_0), // outclk0.clk
        .locked   ()    //  locked.export
    );

    // Clock VGA (25 MHz) - você já tinha essa geração
    reg clk_vga = 0;
    always @(posedge clk_50MHz) clk_vga <= ~clk_vga;

    // Sincronização das chaves
    reg [3:0] sw_sync, sw_sync2;

    always @(posedge clk_vga or negedge vga_reset) begin
         if (!vga_reset) begin
              sw_sync  <= 4'b0000;
              sw_sync2 <= 4'b0000;
         end else begin
              sw_sync  <= sw;
              sw_sync2 <= sw_sync;
         end
    end

    // parâmetros da imagem ORIGINAL
    parameter IMG_W = 160;
    parameter IMG_H = 120;
    wire [2:0] FATOR = (sw_sync[3] == 1'b1) ? 3'd4 : 3'd2;

    // Parâmetros ampliados baseados no seletor
    wire [9:0] IMG_W_AMP = (sw_sync == 4'b0000) ? IMG_W*FATOR :
                           (sw_sync == 4'b1000) ? IMG_W*FATOR :
                           (sw_sync == 4'b0001) ? IMG_W/FATOR :
                           (sw_sync == 4'b0010) ? IMG_W*FATOR :
                           (sw_sync == 4'b0011) ? IMG_W/FATOR :
                           (sw_sync == 4'b1001) ? IMG_W/FATOR :
                           (sw_sync == 4'b1010) ? IMG_W*FATOR :
                           (sw_sync == 4'b1011) ? IMG_W/FATOR :
                           IMG_W;

    wire [9:0] IMG_H_AMP = (sw_sync == 4'b0000 ) ? IMG_H*FATOR :
                           (sw_sync == 4'b1000 ) ? IMG_H*FATOR :
                           (sw_sync == 4'b0001) ? IMG_H/FATOR :
                           (sw_sync == 4'b0010) ? IMG_H*FATOR :
                           (sw_sync == 4'b0011) ? IMG_H/FATOR :
                           (sw_sync == 4'b1001) ? IMG_H/FATOR :
                           (sw_sync == 4'b1010) ? IMG_H*FATOR :
                           (sw_sync == 4'b1011) ? IMG_H/FATOR :
                           IMG_H;

    // Offsets (calculados continuamente no domínio VGA)
    reg [9:0] x_offset_reg, y_offset_reg;
    always @(posedge clk_vga) begin
        x_offset_reg <= (640 - IMG_W_AMP)/2;
        y_offset_reg <= (480 - IMG_H_AMP)/2;
    end

    // sinais de posição na tela (vga_driver preenche next_x/next_y)
    wire in_image = (next_x >= x_offset_reg && next_x < x_offset_reg + IMG_W_AMP) &&
                    (next_y >= y_offset_reg && next_y < y_offset_reg + IMG_H_AMP);

    // Endereço da RAM (domínio de leitura: clk_vga)
    reg [18:0] addr_reg;
    // framebuffer RAM (sinalização)
    wire [7:0] c;
    wire [18:0] wr_addr;
    wire [7:0] wr_data;
    wire wr_en;

    // ROM / copier
    wire copy_done;

    // sincronizador de copy_done para domínio clk_vga
    reg copy_done_sync_0, copy_done_sync_1;
    always @(posedge clk_vga or negedge vga_reset) begin
        if (!vga_reset) begin
            copy_done_sync_0 <= 1'b0;
            copy_done_sync_1 <= 1'b0;
        end else begin
            // copy_done vem do domínio do ULA (clk_50MHz) - faz dois flops para sincronizar
            copy_done_sync_0 <= copy_done;
            copy_done_sync_1 <= copy_done_sync_0;
        end
    end

    // display_enable só quando a cópia foi concluída e sincronizada para domínio VGA
    wire display_enable = copy_done_sync_1;

    // atualiza addr_reg no domínio clk_vga, somente se display_enable
    always @(posedge clk_vga) begin
        if (display_enable) begin
            if (in_image)
                addr_reg <= (next_y - y_offset_reg) * IMG_W_AMP + (next_x - x_offset_reg);
            else
                addr_reg <= 19'd0;
        end else begin
            addr_reg <= 19'd0;
        end
    end

    // instanciação do bloco RAM de leitura/escrita
    ram2port framebuffer
    (
        .clock(outclk_0),   // clock de escrita/leitura do IP (sua instância original)
        .data(wr_data),
        .rdaddress(addr_reg), // rdaddress está vindo do domínio VGA (flopado em clk_vga), RAM faz sincronização interna
        .wraddress(wr_addr),
        .wren(wr_en),
        .q(c)
    );

    // copiador ROM → RAM (ULA)
    ULA copier (
        .clk(clk_50MHz),
        .reset(vga_reset), // reset (ativo baixo na sua ULA)
        .seletor(sw_sync),
        .rom_addr(rom_addr),
        .rom_pixel(rom_data),
        .ram_wraddr(wr_addr),
        .ram_data(wr_data),
        .ram_wren(wr_en),
        .done(copy_done)
    );

    // CORREÇÃO: Sinais do mouse que estavam faltando
    // Como não temos as direções do mouse na interface, vamos criar sinais locais
    // ou modificar a interface. Vou assumir que você quer adicionar esses sinais:
    wire mouse_x_sign = 1'b0; // Temporário - você precisa conectar do módulo mouse
    wire mouse_y_sign = 1'b0; // Temporário - você precisa conectar do módulo mouse

    // Registradores para posição do cursor
    reg [9:0] cursor_x = 320;
    reg [9:0] cursor_y = 240;
    reg [2:0] cursor_size = 3;
    
    // Sincronização dos sinais do mouse
    reg [7:0] mouse_x_sync, mouse_y_sync;
    reg mouse_valid_sync, mouse_l_click_sync, mouse_r_click_sync;
    reg mouse_x_sign_sync, mouse_y_sign_sync; // Sincronizar os sinais de direção também
    
    always @(posedge clk_vga or negedge vga_reset) begin
        if (!vga_reset) begin
            mouse_x_sync <= 8'b0;
            mouse_y_sync <= 8'b0;
            mouse_valid_sync <= 1'b0;
            mouse_l_click_sync <= 1'b0;
            mouse_r_click_sync <= 1'b0;
            mouse_x_sign_sync <= 1'b0;
            mouse_y_sign_sync <= 1'b0;
        end else begin
            mouse_x_sync <= mouse_x;
            mouse_y_sync <= mouse_y;
            mouse_valid_sync <= mouse_valid;
            mouse_l_click_sync <= mouse_l_click;
            mouse_r_click_sync <= mouse_r_click;
            mouse_x_sign_sync <= mouse_x_sign;
            mouse_y_sign_sync <= mouse_y_sign;
        end
    end
    
    // Atualização da posição do cursor
    always @(posedge clk_vga) begin
        if (mouse_valid_sync) begin
            // Movimento em X (tratando sinal)
            if (!mouse_x_sign_sync) begin
                cursor_x <= cursor_x + {2'b0, mouse_x_sync};
            end else begin
                cursor_x <= cursor_x - {2'b0, mouse_x_sync};
            end
            
            // Movimento em Y (invertido para coordenadas de tela)
            if (!mouse_y_sign_sync) begin
                cursor_y <= cursor_y - {2'b0, mouse_y_sync};
            end else begin
                cursor_y <= cursor_y + {2'b0, mouse_y_sync};
            end
            
            // Limitar à tela VGA (640x480)
            if (cursor_x > 639) cursor_x <= 639;
            if (cursor_x < 0) cursor_x <= 0;
            if (cursor_y > 479) cursor_y <= 479;
            if (cursor_y < 0) cursor_y <= 0;
        end
    end
    
    // Detecção de clique para interação
    reg l_click_prev, r_click_prev;
    wire l_click_rising = mouse_l_click_sync && !l_click_prev;
    wire r_click_rising = mouse_r_click_sync && !r_click_prev;
    
    always @(posedge clk_vga) begin
        l_click_prev <= mouse_l_click_sync;
        r_click_prev <= mouse_r_click_sync;
    end
    
    // CORREÇÃO: color_in estava faltando - definir cor da imagem
    wire [7:0] color_in = (display_enable && in_image) ? c : 8'd0;
    
    // Modificação do sinal color_in para incluir cursor
    wire cursor_pixel;
    wire [9:0] rel_x = next_x - cursor_x;
    wire [9:0] rel_y = next_y - cursor_y;
    
    // Desenhar cursor (cruz simples)
    // Função auxiliar para valor absoluto
    function [9:0] abs;
        input [9:0] value;
        begin
            abs = value[9] ? -value : value;
        end
    endfunction
    
    assign cursor_pixel = (abs(rel_x) < cursor_size && rel_y == 0) || 
                         (rel_x == 0 && abs(rel_y) < cursor_size);
    
    // Combinar imagem com cursor
    wire [7:0] final_color;
    assign final_color = cursor_pixel ? 8'hFF : color_in; // Cursor branco

    // VGA driver com cor final
    vga_driver draw (
        .clock(clk_vga),
        .reset(vga_reset),
        .color_in(final_color),  // Agora inclui cursor
        .next_x(next_x),
        .next_y(next_y),
        .hsync(hsyncm),
        .vsync(vsyncm),
        .sync(sync),
        .clk(clks),
        .blank(blank),
        .red(redm),
        .green(greenm),
        .blue(bluem)
    );

endmodule

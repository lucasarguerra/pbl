// control_unity.v
// Versão modificada: sincroniza copy_done e bloqueia leitura do framebuffer enquanto copia está em andamento.
// Mantive sua estrutura e nomes originais o mais possível.

module control_unity (
    input vga_reset,
    input clk_50MHz,
    input [3:0] sw,
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
    input  [31:0] rom_data,
	 input  [7:0] mouse_data,
    input        mouse_valid
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
    // addr_reg será atualizado somente quando display_enable = 1 (ou seja, cópia concluída e sincronizada).
    // Caso contrário mantemos endereço 0 para evitar leituras inválidas durante escrita.
    // (isso reduz glitch; se preferir manter a imagem antiga em tela, implementar double-buffer)
    
    // framebuffer RAM (sinalização)
    reg [18:0] addr_reg_next;
    // wires do ram2port
    wire [7:0] c;
    wire [18:0] wr_addr;
    wire [7:0] wr_data;
    wire wr_en;

    // ROM / copier
    wire copy_done;
    // rom_addr já é saída do ULA (vai para HPS ROM)
    // rom_addr está declarado na instância ULA (saída), por isso temos rom_addr port no top

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

    // color_in para VGA: só usa conteúdo da RAM quando display_enable == 1
    wire [7:0] color_in = (display_enable && in_image) ? c : 8'd0;

    // VGA driver (sem alteração)
    vga_driver draw (
        .clock(clk_vga),
        .reset(vga_reset),
        .color_in(color_in),
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
	 reg [1:0] byte_count = 0;
reg signed [7:0] delta_x, delta_y;
reg [7:0] status_byte;
reg new_packet = 0;

	always @(posedge clk_50MHz) begin
		 if(mouse_valid) begin
			  case(byte_count)
					0: begin
						 status_byte <= mouse_data;
						 byte_count <= 1;
					end
					1: begin
						 delta_x <= mouse_data;
						 byte_count <= 2;
					end
					2: begin
						 delta_y <= mouse_data;
						 byte_count <= 0;
						 new_packet <= 1;  // Pacote completo chegou
					end
			  endcase
		 end else begin
			  new_packet <= 0;
		 end
	end
	
	
	reg [9:0] mouse_x = 320;
	reg [9:0] mouse_y = 240;

	always @(posedge clk_50MHz) begin
		 if(new_packet) begin
			  mouse_x <= mouse_x + delta_x;
			  mouse_y <= mouse_y - delta_y;
		 end
	end


endmodule

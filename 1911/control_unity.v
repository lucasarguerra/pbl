// control_unity.v  (modificado para overlay do cursor PS/2)
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
    input  ps2_clk,
    input  ps2_data,
    input  reset_n   // optional: if you prefer external reset; otherwise vga_reset affects some logic
);

    wire outclk_0;
    wire signed [7:0] dx, dy;
    wire new_data;
    wire [9:0] cursor_x, cursor_y;

    pll_inst pll_inst (
        .refclk   (clk_50MHz),   //  refclk.clk
        .rst      (1'b0),        //   reset.reset
        .outclk_0 (outclk_0),    // outclk0.clk
        .locked   ()             //  locked.export
    );

    // Clock VGA (25 MHz)
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

    // instantiate PS/2 reader and mouse position tracker
    ps2_mouse mouse_read (
        .clk(clk_50MHz),
        .reset_n(vga_reset),    // use vga_reset as global reset; change if you have separate reset
        .ps2_clk(ps2_clk),
        .ps2_data(ps2_data),
        .dx(dx),
        .dy(dy),
        .left_click(),          // unused for now
        .right_click(),
        .new_data(new_data)
    );

    mouse_position mouse_pos (
        .clk(clk_50MHz),
        .reset_n(vga_reset),
        .new_data(new_data),
        .dx(dx),
        .dy(dy),
        .pos_x(cursor_x),
        .pos_y(cursor_y)
    );

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

    // Offsets (no domínio VGA)
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
    reg [18:0] addr_reg_next;
    wire [7:0] c;
    wire [18:0] wr_addr;
    wire [7:0] wr_data;
    wire wr_en;

    // ROM / copier
    wire copy_done;

    // sincroniza copy_done (já existente)
    reg copy_done_sync_0, copy_done_sync_1;
    always @(posedge clk_vga or negedge vga_reset) begin
        if (!vga_reset) begin
            copy_done_sync_0 <= 1'b0;
            copy_done_sync_1 <= 1'b0;
        end else begin
            copy_done_sync_0 <= copy_done;
            copy_done_sync_1 <= copy_done_sync_0;
        end
    end

    wire display_enable = copy_done_sync_1;

    // atualiza addr_reg no domínio clk_vga
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

    // instanciação do bloco RAM (preservada)
    ram2port framebuffer
    (
        .clock(outclk_0),
        .data(wr_data),
        .rdaddress(addr_reg),
        .wraddress(wr_addr),
        .wren(wr_en),
        .q(c)
    );

    // copiador ROM → RAM (ULA) (preservado)
    ULA copier (
        .clk(clk_50MHz),
        .reset(vga_reset),
        .seletor(sw_sync),
        .rom_addr(rom_addr),
        .rom_pixel(rom_data),
        .ram_wraddr(wr_addr),
        .ram_data(wr_data),
        .ram_wren(wr_en),
        .done(copy_done)
    );

    // color_in para VGA
    wire [7:0] color_in = (display_enable && in_image) ? c : 8'd0;

    // overlay: cursor 5x5 filled square (screen coords)
    // ensure the cursor is within screen range to avoid wrap
    wire cursor_hit = (next_x >= cursor_x && next_x < cursor_x + 5 &&
                       next_y >= cursor_y && next_y < cursor_y + 5);

    wire [7:0] color_final = cursor_hit ? 8'hFF : color_in;

    // VGA driver (usa color_final)
    vga_driver draw (
        .clock(clk_vga),
        .reset(vga_reset),
        .color_in(color_final),
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

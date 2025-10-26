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
    input  [31:0] rom_data  // dados vindos da ROM (HPS)
);

	wire outclk_0;

	pll_inst pll_inst (
	.refclk   (clk_50MHz),   //  refclk.clk
	.rst      (1'b0),      //   reset.reset
	.outclk_0 (outclk_0), // outclk0.clk
	.locked   ()    //  locked.export
	);


    // Clock VGA (25 MHz)
    reg clk_vga = 0;
    always @(posedge clk_50MHz) clk_vga <= ~clk_vga;

    // Sincronização das chaves
	reg [3:0] sw_sync, sw_sync2;

	always @(posedge clk_vga or negedge vga_reset) begin
		 if (!vga_reset) begin
			  sw_sync  <= 3'b000;
			  sw_sync2 <= 3'b000;
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
	wire [9:0] IMG_W_AMP = (sw_sync == 4'b0000) ? IMG_W*FATOR :   // replicação
	(sw_sync == 4'b1000) ? IMG_W*FATOR :
									(sw_sync == 4'b0001) ? IMG_W/FATOR :   // decimação
									(sw_sync == 4'b0010) ? IMG_W*FATOR :   // zoom_nn (2x)
									(sw_sync == 4'b0011) ? IMG_W/FATOR :
	(sw_sync == 4'b1001) ? IMG_W/FATOR :
	(sw_sync == 4'b1010) ? IMG_W*FATOR :
	  (sw_sync == 4'b1011) ? IMG_W/FATOR :
									IMG_W;

		wire [9:0] IMG_H_AMP = (sw_sync == 4'b0000 ) ? IMG_H*FATOR :
									(sw_sync == 4'b1000 ) ? IMG_H*FATOR :   // replicação
									(sw_sync == 4'b0001) ? IMG_H/FATOR :   // decimação
									(sw_sync == 4'b0010) ? IMG_H*FATOR :   // zoom_nn (2x)
									(sw_sync == 4'b0011) ? IMG_H/FATOR :
	(sw_sync == 4'b1001) ? IMG_H/FATOR :
	(sw_sync == 4'b1010) ? IMG_H*FATOR :
	(sw_sync == 4'b1011) ? IMG_H/FATOR :
                        IMG_H;
    // Offsets
    reg [9:0] x_offset_reg, y_offset_reg;
    always @(posedge clk_vga) begin
        x_offset_reg <= (640 - IMG_W_AMP)/2;
        y_offset_reg <= (480 - IMG_H_AMP)/2;
    end

    wire in_image = (next_x >= x_offset_reg && next_x < x_offset_reg + IMG_W_AMP) &&
                    (next_y >= y_offset_reg && next_y < y_offset_reg + IMG_H_AMP);

    // Endereço da RAM
    reg [18:0] addr_reg;
    always @(posedge clk_vga) begin
        if (in_image)
            addr_reg <= (next_y - y_offset_reg) * IMG_W_AMP + (next_x - x_offset_reg);
        else
            addr_reg <= 0;
    end

    // framebuffer RAM
    wire [7:0] c;
    wire [18:0] wr_addr;
    wire [7:0] wr_data;
    wire wr_en;
    wire copy_done;

    ram2port framebuffer
	(
        .clock(outclk_0),
        .data(wr_data),
        .rdaddress(addr_reg),
        .wraddress(wr_addr),
        .wren(wr_en),
        .q(c)
    );

    // ROM
   // wire [7:0] rom_pixel;
    //wire [18:0] rom_addr;

	 /*
    ram_primaria rom_ram_inst (
		.clock(outclk_0),
		.data(8'd0),             // não vamos escrever nada
		.rdaddress(rom_addr[14:0]), // truncar para 15 bits (endereço máximo da IP)
		.wraddress(15'd0),       // não usado
		.wren(1'b0),             // leitura apenas
		.q(rom_pixel)
	);*/

    // copiador ROM → RAM
    ULA copier (
        .clk(clk_vga),
        .reset(vga_reset), // aqui só depende do reset físico
        .seletor(sw_sync),
        .rom_addr(rom_addr),
        .rom_data(rom_data),
        .ram_wraddr(wr_addr),
        .ram_data(wr_data),
        .ram_wren(wr_en),
        .done(copy_done)
    );

    // color_in para VGA
    wire [7:0] color_in = (in_image) ? c : 8'd0;

    // VGA driver
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

endmodule
	

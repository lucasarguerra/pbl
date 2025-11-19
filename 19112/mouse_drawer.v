module mouse_drawer (
    input  wire        clk,
    input  wire        reset,

    // posição atual do mouse
    input  wire [9:0]  mouse_x,
    input  wire [9:0]  mouse_y,

    // coordenadas atuais do VGA
    input  wire [9:0]  vga_x,
    input  wire [9:0]  vga_y,

    // pixel vindo do pipeline anterior
    input  wire [11:0] rgb_in,

    // pixel final (saída)
    output reg  [11:0] rgb_out
);

    // Cursor 8x8 (formato bitmap)
    reg [7:0] cursor_bitmap [0:7];

    initial begin
        cursor_bitmap[0] = 8'b10000000;
        cursor_bitmap[1] = 8'b11000000;
        cursor_bitmap[2] = 8'b11100000;
        cursor_bitmap[3] = 8'b11110000;
        cursor_bitmap[4] = 8'b11111000;
        cursor_bitmap[5] = 8'b11100000;
        cursor_bitmap[6] = 8'b11000000;
        cursor_bitmap[7] = 8'b10000000;
    end

    // Cor do cursor (branco)
    wire [11:0] CURSOR_COLOR = 12'hFFF;

    // offsets devem ser definidos FORA do always
    wire [9:0] dx = vga_x - mouse_x;
    wire [9:0] dy = vga_y - mouse_y;

    always @(posedge clk) begin
        if (reset) begin
            rgb_out <= rgb_in;
        end else begin
            // verifica se pixel atual está dentro da área do cursor
            if (dx < 8 && dy < 8) begin
                if (cursor_bitmap[dy][7 - dx] == 1'b1)
                    rgb_out <= CURSOR_COLOR;
                else
                    rgb_out <= rgb_in;
            end else begin
                rgb_out <= rgb_in;
            end
        end
    end

endmodule

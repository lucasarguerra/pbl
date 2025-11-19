module ps2_mouse (
    input  wire        clk,
    input  wire        reset_n,
    input  wire        ps2_clk,
    input  wire        ps2_data,
    output reg  signed [7:0] dx,
    output reg  signed [7:0] dy,
    output reg         left_click,
    output reg         right_click,
    output reg         new_data
);
    reg [10:0] shift_reg;
    reg [3:0]  bit_count;
    reg [1:0]  byte_count;
    reg [7:0]  b0, b1, b2;

    reg [2:0] ps2_clk_sync;

    // falling edge detect
    always @(posedge clk)
        ps2_clk_sync <= {ps2_clk_sync[1:0], ps2_clk};

    wire ps2_clk_falling = (ps2_clk_sync[2:1] == 2'b10);

    always @(posedge clk or negedge reset_n) begin
        if (!reset_n) begin
            shift_reg  <= 0;
            bit_count  <= 0;
            byte_count <= 0;
            b0 <= 0; b1 <= 0; b2 <= 0;
            dx <= 0; dy <= 0;
            left_click <= 0; right_click <= 0;
            new_data <= 0;
        end else begin
            new_data <= 0;

            if (ps2_clk_falling) begin

                shift_reg <= {ps2_data, shift_reg[10:1]};
                bit_count <= bit_count + 1'b1;

                // quando recebe 11 bits
                if (bit_count == 10) begin
                    case (byte_count)
                        2'd0: begin
                            b0 <= shift_reg[8:1];
                            byte_count <= 2'd1;
                        end

                        2'd1: begin
                            b1 <= shift_reg[8:1];
                            byte_count <= 2'd2;
                        end

                        2'd2: begin
                            b2 <= shift_reg[8:1];

                            // agora sim montar o pacote
                            left_click  <= b0[0];
                            right_click <= b0[1];

                            dx <= $signed(b1);
                            dy <= -$signed(shift_reg[8:1]);  // inverte Y

                            new_data <= 1'b1;
                            byte_count <= 2'd0;
                        end
                    endcase

                    bit_count <= 0;
                end
            end
        end
    end
endmodule

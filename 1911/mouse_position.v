// mouse_position.v
// Accumulates signed dx/dy into absolute screen coordinates (0..639, 0..479).
// Inputs: clk (50MHz), reset_n, new_data pulse, dx, dy
// Outputs: pos_x,pos_y (10-bit)

module mouse_position (
    input  wire        clk,
    input  wire        reset_n,
    input  wire        new_data,
    input  wire signed [7:0] dx,
    input  wire signed [7:0] dy,
    output reg  [9:0]  pos_x,
    output reg  [9:0]  pos_y
);
    // initialize at center
    initial begin
        pos_x = 10'd320;
        pos_y = 10'd240;
    end

    // temp for signed arithmetic
    reg signed [10:0] tmpx;
    reg signed [10:0] tmpy;

    always @(posedge clk or negedge reset_n) begin
        if (!reset_n) begin
            pos_x <= 10'd320;
            pos_y <= 10'd240;
        end else begin
            if (new_data) begin
                tmpx = $signed({1'b0, pos_x}) + $signed(dx); // pos_x treated unsigned -> extend with 0
                tmpy = $signed({1'b0, pos_y}) + $signed(dy);

                // clamp X
                if (tmpx < 0) pos_x <= 10'd0;
                else if (tmpx > 10'd639) pos_x <= 10'd639;
                else pos_x <= tmpx[9:0];

                // clamp Y
                if (tmpy < 0) pos_y <= 10'd0;
                else if (tmpy > 10'd479) pos_y <= 10'd479;
                else pos_y <= tmpy[9:0];
            end
        end
    end
endmodule

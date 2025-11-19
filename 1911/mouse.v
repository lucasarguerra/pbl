`timescale 1ns / 1ps

module ps2_validator(
                     input wire [10:0] i_word1,
                     input wire [10:0] i_word2,
                     input wire [10:0] i_word3,
                     input wire [10:0] i_word4,
                     output wire [7:0] o_signal1,
                     output wire [7:0] o_signal2,
                     output wire [7:0] o_signal3,
                     output wire [7:0] o_signal4,
                     output wire       o_valid
                     );
   // Provides (imperfect) validation of the PS2 signal. This is done by
   // checking for parity and ensuring the start and stop bits are correct. If
   // either of these cases fail to occur, the 'valid' output will be 0. Note
   // that this is entirely combinational.


   wire                                parity1, parity2, parity3, parity4, parity;
   wire                                start1, start2, start3, start4, start;
   wire                                stop1, stop2, stop3, stop4, stop;
   wire                                valid1, valid2, valid3, valid4;

   // Separate packets into segments
   assign {start1, o_signal1, parity1, stop1} = i_word1;
   assign {start2, o_signal2, parity2, stop2} = i_word2;
   assign {start3, o_signal3, parity3, stop3} = i_word3;
   assign {start4, o_signal4, parity4, stop4} = i_word4;

   // XNOR words together and compare to parity bit
   assign valid1 = ~^o_signal1 == parity1;
   assign valid2 = ~^o_signal2 == parity2;
   assign valid3 = ~^o_signal3 == parity3;
   assign valid4 = ~^o_signal4 == parity4;

   assign parity = valid1 && valid2 && valid3 && valid4;
   assign start = (!start1 && !start2 && !start3 && !start4);
   assign stop = (stop1 && stop2 && stop3 && stop4);
   assign o_valid = (start && stop && parity);

endmodule

`timescale 1ns / 1ps

module ps2_mouse_map(
                     input wire        i_clk,
                     input wire        i_reset,
                     input wire [7:0]  i_signal1,
                     input wire [7:0]  i_signal2,
                     input wire [7:0]  i_signal3,
                     input wire [7:0]  i_signal4,
                     output wire [7:0] o_x,
                     output wire [7:0] o_y,
                     output wire       o_x_sign,
                     output wire       o_y_sign,
                     output wire       o_l_click,
                     output wire       o_r_click,
                     output wire       o_x_overflow,
                     output wire       o_y_overflow
                     );
   // This module takes the 8-bit words (after they have been validated) and
   // converts them into cohesive signals that can be operated on. Output names
   // are indicative of the mappings. Note that other signals of importance may
   // not be accounted for here. Namely, the Microsoft Intellimouse type
   // extension for supporting a scroll wheel mouse. This may be added in the
   // future (feel free to open a PR).

   assign o_x = i_signal2;
   assign o_y = i_signal3;
   assign {o_x_overflow, o_y_overflow} = i_signal1[1:0];
   assign {o_x_sign, o_y_sign} = i_signal1[3:2];
   assign {o_l_click, o_r_click} = i_signal1[7:6];
endmodule

`timescale 1ns / 1ps

module ps2_signal(
                  input wire         i_clk,
                  input wire         i_reset,
                  input wire         i_PS2Clk,
                  input wire         i_PS2Data,
                  output wire [10:0] o_word1,
                  output wire [10:0] o_word2,
                  output wire [10:0] o_word3,
                  output wire [10:0] o_word4,
                  output wire        o_ready
                  );
   // To avoid crossing clock domains, we detect the negative edge of the PS2Clk -
   // by keeping track of it in a 2-bit state variable. A negative edge has
   // occurred if and only if the state variable equals 2'b10. Interestingly
   // (perhaps), you can see in the digits themselves how this gives us the
   // negative edge. Consider the "wave form": 0/1\0/1\0/1\...


   reg [43:0]                        fifo;
   reg [43:0]                        buffer;
   reg [5:0]                         counter;
   reg [1:0]                         PS2Clk_sync;
   reg                               ready;
   reg                               PS2Data;
   wire                              PS2Clk_negedge;

   assign o_word1 = fifo[33 +: 11];
   assign o_word2 = fifo[22 +: 11];
   assign o_word3 = fifo[11 +: 11];
   assign o_word4 = fifo[0 +: 11];
   assign o_ready = ready;
   assign PS2Clk_negedge = (PS2Clk_sync == 2'b10);

   initial
     begin
        fifo <= 44'b0;
        buffer <= 44'b0;
        counter <= 6'b0;
        PS2Clk_sync <= 2'b1;
        ready <= 1'b0;
        PS2Data <= 1'b0;
     end // initial begin


   always @(posedge i_clk)
     // Enter Base Clock
     begin
        if(i_reset)
          // Provide a nice default start
          begin
             fifo <= 44'b0;
             buffer <= 44'b0;
             counter <= 6'b0;
             ready <= 1'b0;
             PS2Clk_sync <= 2'b1;
             PS2Data <= 1'b0;
          end
        else
          begin
             // Sync the PS2Clk with our Base Clock
             PS2Clk_sync <= {PS2Clk_sync[0], i_PS2Clk};
             PS2Data <= i_PS2Data;

             if(PS2Clk_negedge)
               // Negative edge => Data is ready!
               begin
                  buffer <= {buffer, PS2Data};
                  counter <= counter + 6'b1;
               end

             if(counter == 6'd44)
               // Counter==44 => Buffer is full!
               begin
                  fifo <= buffer;
                  buffer <= 44'b0;
                  counter <= 6'b0;
                  ready <= 1'b1;
               end
             else
               // Counter!=44 => (!ready && clear(FIFO))
               begin
                  ready <= 1'b0;
                  fifo <= 44'b0;
               end
          end
     end
endmodule

`timescale 1ns / 1ps


module ps2_mouse(
                 input wire        i_clk,
                 input wire        i_reset,
                 input wire        i_PS2Data,
                 input wire        i_PS2Clk,
                 output wire [7:0] o_x,
                 output wire       o_x_ov,
                 output wire       o_x_sign,
                 output wire [7:0] o_y,
                 output wire       o_y_ov,
                 output wire       o_y_sign,
                 output wire       o_r_click,
                 output wire       o_l_click,
                 output wire       o_valid
                 );
   // Top level module that takes in the raw PS2 signal (clock and data) and
   // transforms them into actionable signals.


   wire [10:0]                     word1, word2, word3, word4;
   wire [7:0]                      signal1, signal2, signal3, signal4;
   wire                            valid, ready;

   assign o_valid = ready && valid;

   // signal processing -> validation -> map to output
   //----------------------------------------------------------------------
   ps2_signal ps2_signal(
                         .i_clk(i_clk),
                         .i_reset(i_reset),
                         .i_PS2Clk(i_PS2Clk),
                         .i_PS2Data(i_PS2Data),
                         .o_word1(word1),
                         .o_word2(word2),
                         .o_word3(word3),
                         .o_word4(word4),
                         .o_ready(ready)
                         );
   //----------------------------------------------------------------------
   ps2_validator ps2_validator(
                               .i_word1(word1),
                               .i_word2(word2),
                               .i_word3(word3),
                               .i_word4(word4),
                               .o_signal1(signal1),
                               .o_signal2(signal2),
                               .o_signal3(signal3),
                               .o_signal4(signal4),
                               .o_valid(valid)
                               );

   //----------------------------------------------------------------------
   ps2_mouse_map ps2_mouse_map(
                               .i_clk(i_clk),
                               .i_reset(i_reset),
                               .i_signal1(signal1),
                               .i_signal2(signal2),
                               .i_signal3(signal3),
                               .i_signal4(signal4),
                               .o_x(o_x),
                               .o_y(o_y),
                               .o_x_overflow(o_x_ov),
                               .o_y_overflow(o_y_ov),
                               .o_x_sign(o_x_sign),
                               .o_y_sign(o_y_sign),
                               .o_l_click(o_l_click),
                               .o_r_click(o_r_click)
                               );
endmodule

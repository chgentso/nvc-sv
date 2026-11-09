// Reads of four-state vectors of all widths around the byte and word
// boundaries, in every format, from signals and from a value change
// callback.  The C side is test/vpi/vpi17.c.

module vpi17;
   logic [0:0] s1;
   logic [1:0] s2;
   logic [6:0] s7;
   logic [7:0] s8;
   logic [8:0] s9;
   logic [14:0] s15;
   logic [15:0] s16;
   logic [16:0] s17;
   logic [30:0] s31;
   logic [31:0] s32;
   logic [32:0] s33;
   logic [62:0] s63;
   logic [63:0] s64;
   logic [64:0] s65;
   logic [71:0] s72;
   logic [126:0] s127;
   logic [127:0] s128;
   logic [128:0] s129;
   logic [199:0] s200;
   int phase = 0;

   function automatic logic bitof(int i, int ph);
      // Phase 1 uses all four values, phase 2 only defined ones
      if (ph == 1) begin
         case (i % 4)
         0: return 1'b0;
         1: return 1'b1;
         2: return 1'bz;
         default: return 1'bx;
         endcase
      end
      else
         return (i % 3) == 0;
   endfunction

   initial begin
      #1 phase = 1;
      for (int i = 0; i < 1; i++) s1[i] = bitof(i, 1);
      for (int i = 0; i < 2; i++) s2[i] = bitof(i, 1);
      for (int i = 0; i < 7; i++) s7[i] = bitof(i, 1);
      for (int i = 0; i < 8; i++) s8[i] = bitof(i, 1);
      for (int i = 0; i < 9; i++) s9[i] = bitof(i, 1);
      for (int i = 0; i < 15; i++) s15[i] = bitof(i, 1);
      for (int i = 0; i < 16; i++) s16[i] = bitof(i, 1);
      for (int i = 0; i < 17; i++) s17[i] = bitof(i, 1);
      for (int i = 0; i < 31; i++) s31[i] = bitof(i, 1);
      for (int i = 0; i < 32; i++) s32[i] = bitof(i, 1);
      for (int i = 0; i < 33; i++) s33[i] = bitof(i, 1);
      for (int i = 0; i < 63; i++) s63[i] = bitof(i, 1);
      for (int i = 0; i < 64; i++) s64[i] = bitof(i, 1);
      for (int i = 0; i < 65; i++) s65[i] = bitof(i, 1);
      for (int i = 0; i < 72; i++) s72[i] = bitof(i, 1);
      for (int i = 0; i < 127; i++) s127[i] = bitof(i, 1);
      for (int i = 0; i < 128; i++) s128[i] = bitof(i, 1);
      for (int i = 0; i < 129; i++) s129[i] = bitof(i, 1);
      for (int i = 0; i < 200; i++) s200[i] = bitof(i, 1);
      #2 phase = 2;
      for (int i = 0; i < 1; i++) s1[i] = bitof(i, 2);
      for (int i = 0; i < 2; i++) s2[i] = bitof(i, 2);
      for (int i = 0; i < 7; i++) s7[i] = bitof(i, 2);
      for (int i = 0; i < 8; i++) s8[i] = bitof(i, 2);
      for (int i = 0; i < 9; i++) s9[i] = bitof(i, 2);
      for (int i = 0; i < 15; i++) s15[i] = bitof(i, 2);
      for (int i = 0; i < 16; i++) s16[i] = bitof(i, 2);
      for (int i = 0; i < 17; i++) s17[i] = bitof(i, 2);
      for (int i = 0; i < 31; i++) s31[i] = bitof(i, 2);
      for (int i = 0; i < 32; i++) s32[i] = bitof(i, 2);
      for (int i = 0; i < 33; i++) s33[i] = bitof(i, 2);
      for (int i = 0; i < 63; i++) s63[i] = bitof(i, 2);
      for (int i = 0; i < 64; i++) s64[i] = bitof(i, 2);
      for (int i = 0; i < 65; i++) s65[i] = bitof(i, 2);
      for (int i = 0; i < 72; i++) s72[i] = bitof(i, 2);
      for (int i = 0; i < 127; i++) s127[i] = bitof(i, 2);
      for (int i = 0; i < 128; i++) s128[i] = bitof(i, 2);
      for (int i = 0; i < 129; i++) s129[i] = bitof(i, 2);
      for (int i = 0; i < 200; i++) s200[i] = bitof(i, 2);
   end
endmodule

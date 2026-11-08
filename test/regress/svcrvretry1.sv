module svcrvretry1;
  class ins;
    typedef enum bit [2:0] {kOp, kCOp, kF3, kF7, kRes, kHint, kSys} ex_e;
    bit [6:0] legal[$] = '{7'b0000011, 7'b0001111, 7'b0010011, 7'b0010111, 7'b0100011,
                           7'b0110111, 7'b1100011, 7'b0110011, 7'b1100111, 7'b1110011,
                           7'b1101111};
    rand ex_e ex;
    rand bit [31:0] bin;
    rand bit [6:0] op, f7;
    rand bit [2:0] f3, cm;
    rand bit [1:0] cop;
    rand bit comp, h3, h7;
    constraint dist_c { ex dist {kOp := 3, kCOp := 1, kF3 := 1, kF7 := 1, kRes := 1, kHint := 3, kSys := 3}; }
    constraint bits_c {
      solve op before bin; solve f3 before bin; solve f7 before bin;
      solve cm before bin; solve cop before bin;
      if (comp) { bin[1:0] == cop; bin[15:13] == cm; }
      else {
        bin[6:0] == op;
        if (h7) bin[31:25] == f7;
        if (h3) bin[14:12] == f3;
      }
    }
    constraint type_c {
      if (comp) ex inside {kRes, kCOp, kHint};
      else ex inside {kOp, kF3, kF7, kSys};
      if (!h7) ex != kF7;
      if (!h3) ex != kF3;
      cop != 2'b11;
    }
    constraint opcode_c {
      solve ex before op;
      if (ex == kOp) { !(op inside {legal}); op[1:0] == 2'b11; }
      else op inside {legal};
    }
    constraint f_c {
      h7 == ((op == 7'b0010011 && f3 inside {3'b001, 3'b101}) || op inside {7'b0110011, 7'b0111011});
      h3 == !(op inside {7'b0110111, 7'b1101111, 7'b0010111});
      if (!comp) {
        if (ex == kF7) f7 inside {7'd0, 7'b0100000, 7'd1} == 0; else f7 inside {7'd0, 7'b0100000, 7'd1};
      }
    }
    constraint sys_c {
      if (ex == kSys) {
        op == 7'b1110011;
        if (f3 == 0) { bin[19:15] != 0; bin[11:7] != 0; !(bin[31:20] inside {12'h0, 12'h1, 12'h2, 12'h102}); }
      }
    }
    constraint res_c {
      if (ex == kRes) { cm == 3'b100 || cm == 3'b011; cop == 2'b01 || cop == 2'b00; }
      if (ex == kHint) { cm == 3'b000 && cop == 2'b01 && {bin[12], bin[6:2]} == 6'b0; }
    }
  endclass
  initial begin
    ins p = new;
    int bad = 0;
    repeat (300) begin
      if (!p.randomize() with { ex != kHint; }) bad++;
    end
    $display("failures=%0d", bad);
  end
endmodule

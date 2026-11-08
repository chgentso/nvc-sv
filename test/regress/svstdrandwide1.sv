// Expected values: IEEE 1800-2017 18.12 (std::randomize with inline constraints on wide packed struct fields),
// 18.5 (equality constraints fix the value, inside range limits it), 7.2.1/11.5.1 (packed struct member and
// part selects)
typedef struct packed {
  logic        a_valid;
  logic [2:0]  a_opcode;
  logic [31:0] a_address;
  logic [31:0] a_data;
  logic [31:0] a_user;
  logic [3:0]  a_mask;
  logic        d_ready;
} h2d_t;
typedef struct packed {
  logic [39:0] lo;
  logic [4:0]  mid;
  logic [59:0] hi;
} wide_t;
module svstdrandwide1;
  bit cb_ready = 1;
  int errs = 0;
  initial begin
    h2d_t h;
    wide_t w;
    for (int i = 0; i < 3; i++) begin
      if (!std::randomize(h) with { h.a_valid == 1'b0; h.d_ready == cb_ready;
            h.a_address == 32'hdeadbeef; h.a_data == 32'h12345678;
            h.a_user inside {[32'h100:32'h10f]}; h.a_opcode == 3'd5; h.a_mask == 4'hA; })
        begin $error("randomize h failed"); errs++; end
      if (h.a_valid !== 1'b0 || h.d_ready !== 1'b1 || h.a_address !== 32'hdeadbeef ||
          h.a_data !== 32'h12345678 || h.a_opcode !== 3'd5 || h.a_mask !== 4'hA) begin
        $error("fixed fields wrong: %b %b %h %h %h %h", h.a_valid, h.d_ready, h.a_address, h.a_data, h.a_opcode, h.a_mask);
        errs++;
      end
      if (!(h.a_user >= 32'h100 && h.a_user <= 32'h10f) || h.a_user[31:4] !== 28'h10) begin
        $error("a_user=%h outside 100..10f", h.a_user); errs++;
      end
    end
    if (!std::randomize(w) with { w.lo == 40'h12_3456_789a; w.mid == 5'h15; w.hi[59:56] == 4'h9; })
      begin $error("randomize w failed"); errs++; end
    if (w.lo !== 40'h12_3456_789a || w.mid !== 5'h15 || w.hi[59:56] !== 4'h9) begin
      $error("w fields wrong: %h %h %h", w.lo, w.mid, w.hi[59:56]); errs++;
    end
    if (!std::randomize(w) with { w.mid < 5; w.lo[39:30] == 10'h3ff; })
      begin $error("randomize w (2) failed"); errs++; end
    if (!(w.mid < 5) || w.lo[39:30] !== 10'h3ff) begin
      $error("w constraint wrong: mid=%0d lo[39:30]=%h", w.mid, w.lo[39:30]); errs++;
    end
    if (errs == 0) $display("PASSED");
  end
endmodule

// Expected values: IEEE 1800-2017 27.5 (conditional generate: gen_big exists only when W > 8),
// 13.4 (function with a parameter-dependent return width), 11.5.1 (indexed part-select).
// For W=16, hi(v) = v[8+:8] = bits 15:8 of 64'hdeadbeef_cafebabe = 8'hba. For W=8 no check runs.
module sub #(parameter int W = 8) ();
  logic [63:0] v = 64'hdeadbeef_cafebabe;
  int nchk = 0;
  int bad = 0;
  if (W > 8) begin : gen_big
    function automatic logic [W-9:0] hi(logic [63:0] in);
      return in[8 +: (W-8)];
    endfunction
    initial begin
      nchk++;
      if (hi(v) !== 8'hba || $bits(hi(v)) != W-8) begin $error("hi=%h bits=%0d", hi(v), $bits(hi(v))); bad++; end
    end
  end
endmodule
module svgenpartw1;
  sub #(.W(8)) a();
  sub #(.W(16)) b();
  initial begin
    #1;
    if (a.nchk != 0 || b.nchk != 1 || a.bad != 0 || b.bad != 0) begin
      $error("nchk %0d %0d bad %0d %0d", a.nchk, b.nchk, a.bad, b.bad);
      $display("FAILED");
    end else $display("PASSED");
    $finish;
  end
endmodule

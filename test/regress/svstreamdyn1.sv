// Expected values: IEEE 1800-2017 11.4.14 (streaming: >> keeps order, << reverses slices of the given size,
// the slice size does not affect >>; dynamic arrays and queues as targets are sized to the stream;
// element widths of the target determine how the stream is cut), 7.5/7.10 (dynamic arrays, queues)
module svstreamdyn1;
  logic [31:0] vec = 32'hdeadbeef;
  logic [15:0] v16 = 16'hcafe;
  byte dyn[];
  byte q[$];
  byte fx[4];
  int di[];
  int errs = 0;
  task automatic ckb(string w, byte got[$], byte exp[$]);
    if (got.size() != exp.size()) begin $error("%s: size %0d expected %0d", w, got.size(), exp.size()); errs++; end
    else foreach (got[i]) if (got[i] !== exp[i]) begin $error("%s[%0d]: %h expected %h", w, i, got[i], exp[i]); errs++; end
  endtask
  task automatic cki(string w, int got[$], int exp[$]);
    if (got.size() != exp.size()) begin $error("%s: size %0d expected %0d", w, got.size(), exp.size()); errs++; end
    else foreach (got[i]) if (got[i] !== exp[i]) begin $error("%s[%0d]: %h expected %h", w, i, got[i], exp[i]); errs++; end
  endtask
  byte t[$];
  int ti[$];
  initial begin
    dyn = {>>8{vec}};  t = dyn; ckb("L1", t, '{8'hde, 8'had, 8'hbe, 8'hef});
    dyn = {<<8{vec}};  t = dyn; ckb("L2", t, '{8'hef, 8'hbe, 8'had, 8'hde});
    q = {>>8{vec}};    ckb("L3", q, '{8'hde, 8'had, 8'hbe, 8'hef});
    q = {<<8{vec}};    ckb("L4", q, '{8'hef, 8'hbe, 8'had, 8'hde});
    q = {<<4{v16}};    ckb("L5", q, '{8'hef, 8'hac});
    fx = {<<8{vec}};   t = {fx[0], fx[1], fx[2], fx[3]}; ckb("L6", t, '{8'hef, 8'hbe, 8'had, 8'hde});
    di = {>>16{vec}};  ti = di; cki("L7", ti, '{32'hdeadbeef});
    di = {<<16{vec}};  ti = di; cki("L8", ti, '{32'hbeefdead});
    dyn = {>>8{vec, v16}}; t = dyn; ckb("L9", t, '{8'hde, 8'had, 8'hbe, 8'hef, 8'hca, 8'hfe});
    q = {>>8{dyn}};    ckb("L10", q, '{8'hde, 8'had, 8'hbe, 8'hef, 8'hca, 8'hfe});
    q = {<<8{dyn}};    ckb("L11", q, '{8'hfe, 8'hca, 8'hef, 8'hbe, 8'had, 8'hde});
    {>>8{dyn}} = vec;  t = dyn; ckb("L12", t, '{8'hde, 8'had, 8'hbe, 8'hef});
    q = q[0:3]; ckb("L13a", q, '{8'hfe, 8'hca, 8'hef, 8'hbe});
    vec = {>>8{q}};
    if (vec !== 32'hfecaefbe) begin $error("L13: %h", vec); errs++; end
    vec = {<<8{q}};
    if (vec !== 32'hbeefcafe) begin $error("L14: %h", vec); errs++; end
    if (errs == 0) $display("PASSED");
  end
endmodule

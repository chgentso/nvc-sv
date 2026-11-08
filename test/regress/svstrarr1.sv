// Expected values: IEEE 1800-2017 11.4.14 (streaming concatenation: >> keeps order, << reverses blocks of
// the slice size (default 1 bit); as a source narrower than the target it is left-justified and zero-filled on
// the right; an unpacked/dynamic/queue target takes the whole stream and is resized), 11.4.14.3 (re-ordering
// of data), 11.4.14.4 (streaming as a target), 7.5/7.10 (dynamic arrays, queues)
module svstrarr1;
  byte b[4];
  int u[2];
  bit [3:0] n[2];
  bit [31:0] x;
  bit [63:0] y;
  int q[$];
  byte qb[$];
  byte dyn[];
  int errs = 0;
  task automatic ck(string w, logic [63:0] got, logic [63:0] exp);
    if (got !== exp) begin $error("%s: got %h expected %h", w, got, exp); errs++; end
  endtask
  initial begin
    b = '{8'h01, 8'h02, 8'h03, 8'h04};
    x = {>>{b}};     ck("L1", x, 32'h01020304);
    x = {<<8{b}};    ck("L2", x, 32'h04030201);
    x = {<<{b}};     ck("L3", x, 32'h20c04080);
    y = {>>{b, b}};  ck("L4", y, 64'h0102030401020304);
    u = '{32'h11223344, 32'h55667788};
    y = {>>{u}};     ck("L5", y, 64'h1122334455667788);
    y = {<<8{u}};    ck("L6", y, 64'h8877665544332211);
    n = '{4'h5, 4'ha};
    x = {>>{n}};     ck("L7", x, 32'h5a000000);   // 8-bit stream left-justified, zero-filled on the right
    {>>{b}} = 32'hdeadbeef;
    ck("L8", {b[0], b[1], b[2], b[3]}, 32'hdeadbeef);
    {<<8{b}} = 32'hdeadbeef;
    ck("L9", {b[0], b[1], b[2], b[3]}, 32'hefbeadde);
    {>>{u}} = 64'h0102030405060708;
    ck("L10", {u[0], u[1]}, 64'h0102030405060708);
    {>>{n}} = 8'h5a;
    ck("L11", {n[0], n[1]}, 8'h5a);
    q = {>>{b}};     // b is ef be ad de
    if (q.size() != 1 || q[0] != 32'hefbeadde) begin $error("L12: q.size()=%0d", q.size()); errs++; end
    qb = {>>{u}};
    if (qb.size() != 8) begin $error("L13: qb.size()=%0d", qb.size()); errs++; end
    else foreach (qb[i]) if (qb[i] != i + 1) begin $error("L13: qb[%0d]=%0d", i, qb[i]); errs++; end
    dyn = {>>{u}};
    if (dyn.size() != 8) begin $error("L14: dyn.size()=%0d", dyn.size()); errs++; end
    else foreach (dyn[i]) if (dyn[i] != i + 1) begin $error("L14: dyn[%0d]=%0d", i, dyn[i]); errs++; end
    {>>{qb}} = 32'h01020304;
    if (qb.size() != 4 || qb[0] != 1 || qb[1] != 2 || qb[2] != 3 || qb[3] != 4) begin
      $error("L15: qb.size()=%0d", qb.size()); errs++;
    end
    qb = {<<8{x}};   // x is 5a000000
    if (qb.size() != 4 || qb[0] != 0 || qb[1] != 0 || qb[2] != 0 || qb[3] != 8'h5a) begin
      $error("L16: qb.size()=%0d", qb.size()); errs++;
    end
    begin byte c[8]; {>>{c}} = {>>{u}}; ck("L17a", c[0], 8'h01); ck("L17b", c[7], 8'h08); end
    begin byte c[4]; c = {>>{x}}; ck("L18a", c[0], 8'h5a); ck("L18b", c[3], 8'h00); end
    begin int w; automatic byte c[4] = '{1,2,3,4}; w = {>>{c}}; ck("L19", w, 32'h01020304); end
    begin int c[2]; {>>{c[1], c[0]}} = 64'h0000000100000002; ck("L20a", c[0], 2); ck("L20b", c[1], 1); end
    if (errs == 0) $display("PASSED");
  end
endmodule

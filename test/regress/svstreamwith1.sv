// Expected values: IEEE 1800-2017 11.4.14.? (streaming with the "with" clause: an array slice [i +: n] or
// [i : j] contributes the selected elements to the stream; used as a target the array is sized to the
// with-range and its width expression sees the value unpacked earlier in the same stream),
// 11.4.14 (<<8 reverses the order of the bytes of the whole stream, >> keeps it), 7.5, 7.10
module svstreamwith1;
  int h, n, crc, oh, on, ocrc;
  byte d[], od[];
  byte q[$];
  byte pkt0[$];
  int di[], odi[];
  int errs = 0;
  task automatic ckb(string w, byte got[$], int exp[$]);
    if (got.size() != exp.size()) begin $error("%s: size %0d expected %0d", w, got.size(), exp.size()); errs++; end
    else foreach (got[i]) if ((got[i] & 8'hff) != exp[i]) begin $error("%s[%0d]: %0d expected %0d", w, i, got[i] & 8'hff, exp[i]); errs++; end
  endtask
  task automatic cki(string w, int got, int exp);
    if (got != exp) begin $error("%s: got %0d expected %0d", w, got, exp); errs++; end
  endtask
  initial begin
    byte pkt[$];
    h = 12; n = 5; crc = 42;
    d = new[5];
    foreach (d[i]) d[i] = i + 1;
    pkt = {<< 8 {h, n, d with [0 +: n], crc}};
    // forward stream: 00 00 00 0c | 00 00 00 05 | 01 02 03 04 05 | 00 00 00 2a ; reversed by bytes
    ckb("L1", pkt, '{42, 0, 0, 0, 5, 4, 3, 2, 1, 5, 0, 0, 0, 12, 0, 0, 0});
    {<< 8 {oh, on, od with [0 +: on], ocrc}} = pkt;
    cki("oh", oh, 12);
    cki("on", on, 5);
    cki("od size", od.size(), 5);
    if (od.size() == 5) foreach (od[i]) cki("od", od[i], i + 1);
    cki("ocrc", ocrc, 42);
    pkt = {>> 8 {d with [1 : 3]}};
    ckb("L3", pkt, '{2, 3, 4});
    di = new[4];
    foreach (di[i]) di[i] = 100 * (i + 1);
    pkt = {>> {h, di with [1 +: 2], crc}};
    ckb("L4", pkt, '{0, 0, 0, 12, 0, 0, 0, 200, 0, 0, 1, 44, 0, 0, 0, 42});
    {>> {oh, odi with [0 +: 2], ocrc}} = pkt;
    cki("oh2", oh, 12);
    cki("odi size", odi.size(), 2);
    if (odi.size() == 2) begin cki("odi0", odi[0], 200); cki("odi1", odi[1], 300); end
    cki("ocrc2", ocrc, 42);
    if (errs == 0) $display("PASSED");
  end
endmodule

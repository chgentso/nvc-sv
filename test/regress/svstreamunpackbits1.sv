// Expected values: IEEE 1800-2017 11.4.14.4 (streaming concatenation as an assignment target: the source
// bits are distributed over the targets, {>>} keeps order with the first element getting the leftmost bits,
// {<<} reverses the order of slices of the given size (default 1 bit); dynamic arrays and queues are
// resized to hold the data), 7.5/7.10
module svstreamunpackbits1;
  int errs = 0;
  task automatic ck(string w, int got, int exp);
    if (got != exp) begin $error("%s: got %0d expected %0d", w, got, exp); errs++; end
  endtask
  initial begin
    bit a[];
    bit q[$];
    bit [3:0] n[];
    logic [9:0] x;
    bit [10:0] y;
    x = 10'b1100000101;
    y = 11'b10000000011;
    a = new[10];
    // {<<{a}} = x: the bit-reversed stream of a equals x, so a[i] == x[i]
    {<<{a}} = x;
    ck("L1 size", a.size(), 10);
    foreach (a[i]) ck("L1", a[i], x[i]);
    // {>>{a}} = x: a[0] gets the leftmost bit, a[i] == x[9-i]
    {>>{a}} = x;
    ck("L2 size", a.size(), 10);
    foreach (a[i]) ck("L2", a[i], x[9 - i]);
    a = new[11];
    {<<{a}} = {1'b1, 9'h0, 1'b1};
    ck("L3 size", a.size(), 11);
    foreach (a[i]) ck("L3", a[i], (i == 0 || i == 10) ? 1 : 0);
    {<<{q}} = y;
    ck("L4 size", q.size(), 11);
    foreach (q[i]) ck("L4", q[i], y[i]);
    n = new[2];
    // reversed stream of {n[0], n[1]} is a3 = 1010_0011, so the stream is 1100_0101
    {<<{n}} = 8'hA3;
    ck("L5 size", n.size(), 2); ck("L5a", n[0], 4'hc); ck("L5b", n[1], 4'h5);
    {>>{n}} = 8'hA3;
    ck("L6a", n[0], 4'ha); ck("L6b", n[1], 4'h3);
    // nibble-reversed: stream is 3 a
    {<<4{n}} = 8'hA3;
    ck("L7a", n[0], 4'h3); ck("L7b", n[1], 4'ha);
    if (errs == 0) $display("PASSED");
  end
endmodule

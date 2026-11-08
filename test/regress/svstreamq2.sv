// Expected values: IEEE 1800-2017 11.4.14 (streaming of a queue source with >>: elements in order, first
// element leftmost; a queue/dynamic array target is cut into elements of its own width), 7.10 (queues)
module svstreamq2;
  int errs = 0;
  task automatic ck(string w, int got, int exp);
    if (got != exp) begin $error("%s: got %0d expected %0d", w, got, exp); errs++; end
  endtask
  initial begin
    automatic bit [7:0] data[$] = {8'ha5, 8'h3c};
    bit bits[$];
    bit [3:0] nib[$];
    bit [1:0] two[];
    automatic bit [15:0] ebits = 16'b1010_0101_0011_1100;
    automatic bit [3:0] enib[4] = '{4'ha, 4'h5, 4'h3, 4'hc};
    automatic int etwo[8] = '{2, 2, 1, 1, 0, 3, 3, 0};
    bits = {>> 1 {data}};
    ck("bits size", bits.size(), 16);
    foreach (bits[i]) ck("bit", bits[i], ebits[15 - i]);
    nib = {>> 4 {data}};
    ck("nib size", nib.size(), 4);
    foreach (nib[i]) ck("nib", nib[i], enib[i]);
    two = {>> 2 {data}};
    ck("two size", two.size(), 8);
    foreach (two[i]) ck("two", two[i], etwo[i]);
    if (errs == 0) $display("PASSED");
  end
endmodule

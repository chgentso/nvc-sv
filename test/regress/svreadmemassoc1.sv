// $readmemh and $readmemb into associative arrays (IEEE 1800-2017 21.4.1):
// the element width comes from the element type, loading an address
// creates the entry and existing entries are kept or overwritten
module svreadmemassoc1_sub;
  bit [7:0] mem [bit [31:0]];
endmodule

module svreadmemassoc1;
  bit [7:0]   mem [bit [31:0]];
  logic [7:0] lmem [int];
  bit [15:0]  wmem [int unsigned];
  byte        smem [string];
  int         f;

  svreadmemassoc1_sub sub ();

  initial begin
    f = $fopen("rma1.hex", "w");
    $fwrite(f, "@00001000\n73 10 2x\n// comment\n@ffffffff 9A\n@00000000\nzz 01\n");
    $fclose(f);
    f = $fopen("rma1.bin", "w");
    $fwrite(f, "0000000000000001\n1111_0000_1111_0000\n");
    $fclose(f);
    f = $fopen("rma2.hex", "w");
    $fwrite(f, "aa bb\n");
    $fclose(f);

    mem[32'h11] = 8'h77;      // Kept: not in the file
    mem[32'h1000] = 8'h55;    // Overwritten by the file
    $readmemh("rma1.hex", mem);
    $display("%0d entries", mem.num());
    foreach (mem[i])
      $display("mem[%h] = %h", i, mem[i]);

    $readmemh("rma1.hex", lmem);   // Signed index: @ffffffff is -1
    $display("%0d entries", lmem.num());
    foreach (lmem[i])
      $display("lmem[%0d] = %h", i, lmem[i]);

    $readmemb("rma1.bin", wmem, 4);
    foreach (wmem[i])
      $display("wmem[%0d] = %h", i, wmem[i]);

    // Hierarchical name, loading downwards
    $readmemh("rma2.hex", sub.mem, 32'h1002, 32'h1001);
    foreach (sub.mem[i])
      $display("sub.mem[%h] = %h", i, sub.mem[i]);

    $readmemh("rma1.hex", smem);   // Not supported
    $display("%0d entries", smem.num());
  end
endmodule

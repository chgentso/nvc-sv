// Self-checking. IEEE 1800-2017 11.4.14.4: unpacking a streaming
// concatenation assigns the bits, in stream order, to a target that is a
// fixed-size unpacked array, here also a local variable of an automatic task.
module svstreamarr1;
  int errs = 0;
  task automatic unpack(bit [31:0] d);
    bit [7:0] b[4];
    {<<byte{b}} = d;
    if (b[0] !== d[7:0] || b[1] !== d[15:8] || b[2] !== d[23:16] || b[3] !== d[31:24]) begin
      errs++;
      $error("got %p for %h", b, d);
    end
  endtask
  initial begin
    bit [7:0] s[4];
    unpack(32'hfb74068d);
    unpack(32'h01020304);
    {>>byte{s}} = 32'h11223344;
    if (s[0] !== 8'h11 || s[3] !== 8'h44) begin errs++; $error("left to right %p", s); end
    if (errs == 0) $display("PASSED");
  end
endmodule

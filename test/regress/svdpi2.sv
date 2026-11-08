// DPI-C imports with 100 bit bit/logic vectors (four 32-bit words, canonical layout).
// Expected values: IEEE 1800-2017 35.5.6 and Annex H.7 (svBitVecVal / svLogicVecVal: word 0
// holds bits 31:0; logic encoding 0=(a0,b0) 1=(a1,b0) z=(a0,b1) x=(a1,b1)), 6.11.
// The C side is test/dpi/dpi_test.c, whose bodies are:
//   lwide: out[i] = in[(i+1)%4], upper word masked to 4 bits, out[0].bval |= 1<<31
//   bwide: returns popcount(in), inout[i] += in[i] per word
//   lw_inout: v[1].aval = ~v[1].aval
`define CHK(c, m) if (!(c)) begin err++; $error(m); end
module svdpi2;
  import "DPI-C" function void dpi_lwide(input logic [99:0] i, output logic [99:0] o);
  import "DPI-C" function int dpi_bwide(input bit [99:0] i, inout bit [99:0] io);
  import "DPI-C" function void dpi_lw_inout(inout logic [99:0] v);
  import "DPI-C" function int dpi_add(input int a, input int b);

  logic [99:0] a, b;
  bit [99:0] c, d;
  int n;
  int err = 0;

  initial begin
    // words of a: [0]=00112233 [1]=89abcdef [2]=01234567 [3]=9
    a = 100'h9_0123_4567_89ab_cdef_0011_2233;
    dpi_lwide(a, b);
    // out = {in[0]&f, in[3], in[2], in[1] with bit 31 made (a=1,b=1) = x}
    `CHK(b === {4'h3, 32'h0000_0009, 32'h0123_4567, 1'bx, 31'h09ab_cdef}, "lwide")
    a = {4'b01xz, 96'h0};
    dpi_lwide(a, b);
    `CHK(b[99:90] === 10'b0, "lwide x: top bits")
    `CHK(b[67:64] === 4'b01xz, "lwide x: word 3 moves to word 2")
    `CHK(b[31] === 1'bz, "lwide x: bit 31 (a=0,b=1) is z")
    c = 100'h3_ffff_ffff_0000_0001_0000_0002;
    d = 100'h1_0000_0000_0000_0000_0000_0001;
    n = dpi_bwide(c, d);
    `CHK(n == 36, "bwide popcount")
    `CHK(d === 100'h4_ffff_ffff_0000_0001_0000_0003, "bwide word-wise add")
    b = 100'h5_5555_5555_aaaa_aaaa_1234_5678;
    dpi_lw_inout(b);
    `CHK(b === 100'h5_5555_5555_5555_5555_1234_5678, "lw_inout inverts word 1")
    `CHK(dpi_add(1, 2) == 3, "after")
    if (err == 0) $display("PASSED");
  end
endmodule

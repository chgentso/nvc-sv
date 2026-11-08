// Bit/part selects of packed struct members that are elements of unpacked arrays, in structs,
// interfaces and interface arrays.
// Expected values: IEEE 1800-2017 7.2.1 (packed struct is a vector, hi is [7:4]), 11.5.1 (bit and
// part selects, indexed part selects +: and -:), 7.4 (unpacked arrays), 25.3 (interfaces),
// 6.3.1 / 11.4.5 (x bits of an uninitialised 4-state variable)
typedef struct packed { logic [3:0] hi; logic [3:0] lo; } byte_t;
typedef struct { logic [1:0] tag; byte_t beat [4]; } wide_t;
interface ifc;
  byte_t arr [0:2];
endinterface
module svpstructsel1;
  wide_t b;
  byte_t arr [0:3];
  ifc i0 ();
  ifc ia [0:1] ();
  int i;
  int errs = 0;
  task automatic chk(string n, logic [7:0] got, logic [7:0] exp);
    if (got !== exp) begin errs++; $error("%s: got %b expected %b", n, got, exp); end
  endtask
  initial begin
    b.tag = 0; for (int k=0;k<4;k++) b.beat[k] = 0;
    b.beat[2] = 8'hA5;
    chk("b.beat[2][7:0]", b.beat[2][7:0], 8'hA5);
    chk("b.beat[2][3:0]", b.beat[2][3:0], 4'b0101);
    b.beat[2][3:0] = 4'h3;
    chk("b.beat[2]", b.beat[2], 8'hA3);
    chk("b.beat[2][7]", b.beat[2][7], 1'b1);
    for (i = 0; i < 4; i++) arr[i] = i * 17;
    i = 2;
    chk("arr[i][7:0]", arr[i][7:0], 8'h22);
    chk("arr[i][3:0]", arr[i][3:0], 4'h2);
    chk("arr[3][4]", arr[3][4], 1'b1);
    arr[1][7:4] = 4'hf;
    chk("arr[1]", arr[1], 8'hF1);
    i0.arr[1] = 8'h5c;
    ia[1].arr[2] = 8'h3e;
    chk("i0.arr[1][7:4]", i0.arr[1][7:4], 4'h5);
    chk("ia[1].arr[2][3:0]", ia[1].arr[2][3:0], 4'hE);
    chk("ia[1].arr[i][2]", ia[1].arr[i][2], 1'b1);
    i0.arr[i][3:0] = 4'h9;     // arr[2] was never written: upper nibble stays x
    chk("i0.arr[2][3:0]", i0.arr[2][3:0], 4'h9);
    chk("i0.arr[2][7:4]", i0.arr[2][7:4], 4'bxxxx);
    b.tag = 2;
    chk("b.beat[1][i+:4]", b.beat[1][i+:4], 4'h0);
    chk("b.beat[1][i -: 2]", b.beat[1][i -: 2], 2'b00);
    chk("b.tag", b.tag, 2'd2);
    if (errs == 0) $display("PASSED");
  end
endmodule

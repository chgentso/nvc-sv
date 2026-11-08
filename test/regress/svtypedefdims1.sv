// Expected values: IEEE 1800-2017 6.18 (typedef), 7.4.1 (packed arrays of packed structs/typedefs),
// 7.2.1 (packed struct layout, first member leftmost), 20.6.2 ($bits)
typedef struct packed { bit [3:0] a; bit b; } s_t;
typedef logic [1:0] two_t;
module svtypedefdims1;
  localparam N = 3;
  s_t [N-1:0] arr;
  two_t [1:0] t2;
  wire [14:0] w;
  int i;
  int errs;
  assign w = arr;
  initial begin
    for (i = 0; i < N; i++) arr[i] = {i[3:0], 1'b1};
    arr[1].a = 4'h7;
    t2 = 4'b1001;
    #1;
    // arr[0]=00001 arr[1]=01111 arr[2]=00101
    if (arr !== 15'h15e1 || arr[1] !== 5'h0f || w !== 15'h15e1) begin $error("arr %h %h %h", arr, arr[1], w); errs++; end
    if (t2 !== 4'b1001 || t2[1] !== 2'b10 || t2[0] !== 2'b01 || $bits(arr) !== 15) begin $error("t2 %b", t2); errs++; end
    if (arr[2].a !== 4'd2 || arr[0].b !== 1'b1) begin $error("members %0d %0d", arr[2].a, arr[0].b); errs++; end
    begin
      s_t [1:0] loc;
      loc = 10'h2a5;
      if (loc !== 10'h2a5 || loc[0].a !== 4'd2 || $bits(loc) !== 10) begin $error("loc %h %h %0d", loc, loc[0].a, $bits(loc)); errs++; end
    end
    if (errs == 0) $display("PASSED");
  end
endmodule

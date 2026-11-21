module inval4;
  typedef enum { A, B, C } e_t;
  e_t val;
  e_t other;
  int i;
  real r = 0.5;

  enum logic [2:0] { G = 4'h2, H = 3'h3 } e1;
  enum bit [1:0] { P = 0, Q = 2'bxx, R = 1 } e2;
  enum integer { S = 0, T = {32{1'bx}}, U } e3;

  struct packed {
    bit [3:0] lo = 4'h5;
    bit [3:0] hi;
  } p1;

  class K;
    rand int x;
    function int rand_mode();
      return 1;
    endfunction
    pure virtual task t();
  endclass

  task fork_test;
    fork
      #20;
      return;
    join_none
  endtask

  initial begin
    val = A;
    val = other;
    val = i > 0 ? A : B;
    val = 1;
    val += 1;
    val = A | B;
    val = i;
    val = e_t'(i);
    @(posedge r);
    @(posedge i);
  end

  e_t init = 2;

  enum logic [1:0] { W0 = 2'd3, W1 } e4;

  int arr[4];
  typedef logic [7:0] byte_t;
  typedef logic byte_arr_t [1:0];
  localparam int W_UNP = $bits(byte_arr_t [3:0]);
  localparam int W_UNS = $bits(byte_t []);
  localparam int W_QUE = $bits(byte_t [$:3]);
  initial begin
    arr = '{i: 1, default: 0};
  end
endmodule

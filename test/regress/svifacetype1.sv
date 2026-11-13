// Self-checking. IEEE 1800-2017 6.18 and 25.10: a type declared in an
// interface can be referenced through an interface instance or port, and its
// size depends on the parameters of that instance.
interface svifacetype1_in #(parameter int N = 1) ();
  typedef logic [$clog2(N)-1:0] id_t;
endinterface

interface svifacetype1_if #(parameter int W = 4, parameter int N = 1) ();
  localparam int V = W * 2;
  typedef logic [W-1:0] data_t;
  typedef struct packed { data_t d; logic [V-1:0] e; } pair_t;
  svifacetype1_in #(N) inner ();
endinterface

interface svifacetype1_wrap #(parameter int N = 1) ();
  svifacetype1_if #(3, N) w ();
  typedef w.data_t data_t;
endinterface

module svifacetype1_sub(svifacetype1_if i, output bit [7:0] bits);
  typedef i.data_t data_t;
  typedef i.pair_t pair_t;
  typedef i.inner.id_t id_t;
  data_t d;
  pair_t p;
  id_t id;
  initial begin
    d = '1;
    p.d = 0;
    p.e = '1;
    id = '1;
    #1;
    if ($bits(d) !== 5 || d !== 5'h1f)
      $error("FAIL d %0d %h", $bits(d), d);
    else if ($bits(p) !== 15 || p !== 15'h3ff)
      $error("FAIL p %0d %h", $bits(p), p);
    else if ($bits(id) !== 3 || id !== 3'h7)
      $error("FAIL id %0d %h", $bits(id), id);
    else
      bits = 8'd1;
  end
endmodule

module svifacetype1_arr(svifacetype1_if ports[1:0], output bit [7:0] bits);
  localparam type data_t = ports[1].data_t;
  data_t d;
  initial begin #1; bits = $bits(d); end
endmodule

module svifacetype1;
  svifacetype1_if #(5, 8) i0();
  typedef i0.data_t data_t;
  localparam type pair_t = i0.pair_t;
  data_t d;
  pair_t p;
  i0.inner.id_t id;
  localparam type packed2_t = i0.data_t[1:0];
  packed2_t pk;
  bit [7:0] bits;
  svifacetype1_if #(2) arr[2] ();
  bit [7:0] abits;
  svifacetype1_arr ua(arr, abits);
  typedef arr[1].data_t adata_t;
  svifacetype1_wrap #(4) wr();
  typedef wr.data_t wdata_t;
  svifacetype1_sub s(i0, bits);
  initial begin
    #2;
    if ($bits(d) !== 5)
      $error("FAIL top d %0d", $bits(d));
    else if ($bits(p) !== 15)
      $error("FAIL top p %0d", $bits(p));
    else if ($bits(id) !== 3)
      $error("FAIL top id %0d", $bits(id));
    else if ($bits(adata_t) !== 2 || $bits(wr.data_t) !== 3
             || $bits(i0.data_t) !== 5 || $bits(wdata_t) !== 3)
      $error("FAIL nested %0d %0d %0d", $bits(adata_t), $bits(wr.data_t),
             $bits(wdata_t));
    else if (abits !== 8'd2)
      $error("FAIL array port %0d", abits);
    else if ($bits(pk) !== 10)
      $error("FAIL packed %0d", $bits(pk));
    else if (bits !== 8'd1)
      $error("FAIL sub");
    else
      $display("PASSED");
  end
endmodule

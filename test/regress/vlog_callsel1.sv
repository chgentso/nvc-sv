// Selects of the result of a function call, a cast and a concatenation
// in always_comb and functions: the base is not part of any variable
module vlog_callsel1;
  typedef struct packed {
    logic [1:0][3:0][2:0] vtab;
  } grid_t;

  function automatic grid_t mk(input grid_t src);
    mk = src;
  endfunction

  function automatic logic [2:0] pick(input grid_t g, input logic [1:0] y,
                                      input logic [1:0] x);
    pick = mk(g).vtab[y[0]][x];
  endfunction

  grid_t      grid;
  logic [1:0] y, x;
  logic [2:0] a, b;

  always_comb a = mk(grid).vtab[y[0]][x];
  always_comb b = pick(grid, y, x);

  initial begin
    for (int i = 0; i < 8; i++)
      grid.vtab[i / 4][i % 4] = 3'(i + 1);
    y = 1; x = 2;
    #1 $display("%0d %0d", a, b);
    y = 0; x = 3;
    #1 $display("%0d %0d", a, b);
    if (a == 4 && b == 4) $display("PASSED");
  end
endmodule

// Assignment patterns typed by the assignment-like context they appear in
// Expected values: IEEE 1800-2017 10.9.1, 10.9.2 (structure patterns), 10.8
typedef struct packed { logic s; logic [2:0] m; } st_t;
typedef enum logic [1:0] {E0, E1, E2} en_t;
typedef struct { st_t f; logic [3:0] v; int arr[3]; } big_t;

module sub #(parameter st_t P = '{s:1, m:3}) (input st_t i, output st_t o);
  assign o = i;
endmodule

function automatic st_t mk(logic z);
  return '{default: z};
endfunction

function automatic logic [3:0] take(st_t s);
  return s;
endfunction

module svpatterncontext1;
  big_t bg;
  st_t w, o1, o2, o3;
  logic [3:0] r;
  logic c;
  st_t arrs [2];
  en_t ens [2];
  sub u1 (.i('{default: 1'b1}), .o(o1));
  initial begin
    c = 1;
    bg.f = '{default: 1'b1};
    bg.v = '{default: 1'b1};
    bg.arr = '{default: 7};
    w = c ? '{default: 1'b1} : '{default: 1'b0};
    o3 = mk(1);
    r = take('{default: 1'b1});
    arrs = '{default: '{default: 1'b1}};
    arrs[1] = '{s: 0, m: 2};
    ens = '{default: E2};
    o2 = st_t'{default: 1'b1};
    #1;
    if (bg.f === 4'b1001 && bg.v === 4'b1111 && bg.arr[2] == 7 && w === 4'b1001
        && o3 === 4'b1001 && r === 4'b1001 && arrs[0] === 4'b1001
        && arrs[1] === 4'b0010 && ens[1] === E2 && o1 === 4'b1001
        && o2 === 4'b1001)
      $display("PASSED");
    else
      $error("pattern context: %p %b %b %b %b %p %p %b", bg, w, o3, r, o2,
             arrs, ens, o1);
  end
endmodule

// Unpacked arrays with a lower bound other than zero declared with a typedef
// Expected values: IEEE 1800-2017 7.4.2 (unpacked arrays, index ranges), 7.4.6 (out of range read of a
// 2-state element is 0, out of range write is ignored), 10.9.1 (positional array assignment patterns:
// the first element goes to the leftmost index), 12.7.3 (foreach visits indices from left to right),
// 20.7 (array query functions), 6.8 / 6.4 (4-state variables start as x)
typedef int t_a [1:3];
typedef int t_d [3:1];
typedef int t_m [1:2][5:6];
typedef int t_z [0:2];
typedef logic [7:0] t_b [2:4];
typedef struct { int a; t_a arr; } st_t;
`define CHK(c, m) if (!(c)) begin errs++; $error(m); end
class C;
  t_a ca;
  t_d cd;
  function void set(int i, int v); ca[i] = v; endfunction
  function int get(int i); return ca[i]; endfunction
endclass
module svlobound1;
  t_a t; t_d d; t_m m; t_b bb; st_t s;
  int plain [1:3];
  int pd [3:1];
  int pm [1:2][5:6];
  int k;
  int errs = 0;
  string trace;
  function automatic int sum(t_a x);
    return x[1] + x[2] * 10 + x[3] * 100;
  endfunction
  function automatic void wr(ref t_a x); x[2] = 99; endfunction
  function automatic t_a ret();
    t_a r; r[1] = 7; r[2] = 8; r[3] = 9; return r;
  endfunction
  initial begin : blk
    C c; t_a u;
    t = '{10, 20, 30};
    `CHK(t[1] == 10 && t[2] == 20 && t[3] == 30, "A")
    d = '{10, 20, 30};
    `CHK(d[3] == 10 && d[2] == 20 && d[1] == 30, "B")
    m = '{'{1,2},'{3,4}};
    `CHK(m[1][5] == 1 && m[1][6] == 2 && m[2][5] == 3 && m[2][6] == 4, "C")
    `CHK(sum(t) == 10 + 200 + 3000, "D")
    wr(t);
    `CHK(t[1] == 10 && t[2] == 99 && t[3] == 30, "F")
    u = ret();
    `CHK(u[1] == 7 && u[2] == 8 && u[3] == 9, "G")
    s.arr[1] = 5; s.arr[3] = 6; s.arr[2] = 4;
    `CHK(s.arr[1] == 5 && s.arr[2] == 4 && s.arr[3] == 6, "H")
    c = new; c.set(1, 11); c.set(3, 33); c.ca[2] = 22;
    `CHK(c.get(1) == 11 && c.get(2) == 22 && c.get(3) == 33 && c.ca[3] == 33, "I")
    trace = "";
    foreach (t[i]) trace = {trace, $sformatf("%0d=%0d;", i, t[i])};
    `CHK(trace == "1=10;2=99;3=30;", "J")
    trace = "";
    foreach (d[i]) trace = {trace, $sformatf("%0d=%0d;", i, d[i])};
    `CHK(trace == "3=10;2=20;1=30;", "K")
    trace = "";
    foreach (m[i,j]) trace = {trace, $sformatf("%0d,%0d=%0d;", i, j, m[i][j])};
    `CHK(trace == "1,5=1;1,6=2;2,5=3;2,6=4;", "L")
    `CHK($low(t) == 1 && $high(t) == 3 && $left(t) == 1 && $right(t) == 3 && $size(t) == 3, "M")
    `CHK($low(d) == 1 && $high(d) == 3 && $left(d) == 3 && $right(d) == 1 && $size(d) == 3, "N")
    `CHK($low(m) == 1 && $high(m) == 2 && $left(m,2) == 5 && $right(m,2) == 6 && $size(m,2) == 2, "O")
    `CHK($increment(t) == -1 && $increment(d) == 1, "P")
    k = 2; t[k] = 77;
    `CHK(t[k] == 77 && t[k+1] == 30, "S")
    t[1] += 5; t[3]++;
    `CHK(t[1] == 15 && t[3] == 31, "V")
    bb[2] = 8'h12; bb[4] = 8'h34; bb[3][3:0] = 4'h5;
    `CHK(bb[2] === 8'h12 && bb[3] === 8'bxxxx0101 && bb[4] === 8'h34 && bb[4][7:4] === 4'h3, "W")
    plain[1] = 1; plain[3] = 3; pd[3] = 3; pd[1] = 1; pm[2][6] = 26; pm[1][5] = 15;
    `CHK(plain[1] == 1 && plain[3] == 3 && pd[3] == 3 && pd[1] == 1 && pm[2][6] == 26 && pm[1][5] == 15, "X")
    `CHK(t[0] == 0 && t[4] == 0, "Y")
    t[0] = 5; t[4] = 6;
    `CHK(t[1] == 15 && t[2] == 77 && t[3] == 31, "Z")
    if (errs == 0) $display("PASSED");
  end
endmodule

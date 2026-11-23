// A fixed size array of dynamic arrays, an associative array and a
// multi-dimensional array as members of a rand structure
class c;
  typedef struct { rand int arr[string]; } as_t;
  typedef struct { rand int arr[2][3]; } md_t;
  typedef struct { rand int mix_arr[3][]; } mx_t;
  rand as_t s4;
  rand md_t s5;
  rand mx_t s6;
  constraint k {
    s4.arr["one"] inside {[10:50]};
    s4.arr["two"] inside {[51:100]};
    foreach (s5.arr[i, j]) s5.arr[i][j] inside {[0:9]};
    foreach (s6.mix_arr[i]) s6.mix_arr[i].size() == i + 2;
    foreach (s6.mix_arr[i, j]) s6.mix_arr[i][j] inside {[50:100]};
  }
  function new();
    s4.arr["one"] = 1000; s4.arr["two"] = 2000;
    s5.arr = '{ '{default:0}, '{default:0} };
    foreach (s6.mix_arr[i]) s6.mix_arr[i] = new[1];
  endfunction
endclass

module svcrvstructnest1;
  c o;
  int bad;
  initial begin
    o = new;
    for (int n = 0; n < 20; n++) begin
      if (!o.randomize()) bad++;
      if (o.s4.arr["one"] < 10 || o.s4.arr["one"] > 50) bad++;
      if (o.s4.arr["two"] < 51 || o.s4.arr["two"] > 100) bad++;
      foreach (o.s5.arr[i, j])
        if (o.s5.arr[i][j] < 0 || o.s5.arr[i][j] > 9) bad++;
      foreach (o.s6.mix_arr[i]) if (o.s6.mix_arr[i].size() != i + 2) bad++;
      foreach (o.s6.mix_arr[i, j])
        if (o.s6.mix_arr[i][j] < 50 || o.s6.mix_arr[i][j] > 100) bad++;
    end
    if (bad == 0) $display("PASSED");
    else $display("FAILED %0d", bad);
  end
endmodule

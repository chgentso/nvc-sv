typedef struct {
  rand int arr[];
  rand int q[$];
  rand int fx[3];
  int fixed[2];
  rand bit [3:0] e;
} st_t;
class c;
  rand st_t s;
  constraint k {
    s.arr.size() == 4;
    foreach (s.arr[i]) s.arr[i] inside {[10:20]};
    s.q.size() inside {[2:5]};
    foreach (s.q[i]) s.q[i] inside {[100:200]};
    foreach (s.fx[i]) s.fx[i] == 7 + i;
  }
endclass
module svcrvstructarr1;
  c o;
  int bad;
  initial begin
    o = new;
    o.s.fixed = '{5, 6};
    for (int n = 0; n < 50; n++) begin
      if (!o.randomize()) bad++;
      if (o.s.arr.size() != 4) bad++;
      foreach (o.s.arr[i]) if (o.s.arr[i] < 10 || o.s.arr[i] > 20) bad++;
      if (o.s.q.size() < 2 || o.s.q.size() > 5) bad++;
      foreach (o.s.q[i]) if (o.s.q[i] < 100 || o.s.q[i] > 200) bad++;
      foreach (o.s.fx[i]) if (o.s.fx[i] != 7 + i) bad++;
      if (o.s.fixed[0] != 5 || o.s.fixed[1] != 6) bad++;
    end
    if (bad == 0) $display("PASSED");
    else $display("FAILED %0d", bad);
  end
endmodule

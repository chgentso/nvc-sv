// A constraint on the elements of a fixed size array of queues: each queue
// has a size of its own which is solved before its elements
class c;
  rand int pages[3][$];
  constraint s {
    foreach (pages[i]) pages[i].size() == i + 2;
    foreach (pages[i, j]) pages[i][j] inside {[i * 100 : i * 100 + 9]};
  }
endclass

module svcrvnest1;
  c o;
  int bad;
  initial begin
    o = new;
    for (int n = 0; n < 50; n++) begin
      if (!o.randomize()) bad++;
      for (int i = 0; i < 3; i++) begin
        if (o.pages[i].size() != i + 2) bad++;
        foreach (o.pages[i][j])
          if (o.pages[i][j] < i * 100 || o.pages[i][j] > i * 100 + 9) bad++;
      end
    end
    if (bad == 0) $display("PASSED");
    else $display("FAILED %0d", bad);
  end
endmodule

// Expected values: IEEE 1800-2017 18.7.1 (local:: scope in randomize() with: the constraint is satisfiable
// only by data_q equal to the seq's queue), 18.5.8 (foreach constraints), 7.10 (queues)
class item;
  rand bit [7:0] data_q[$];
endclass
class seq;
  bit [7:0] data_q[$] = '{1, 2, 3};
  int errs = 0;
  task go();
    item req = new;
    int ok;
    ok = req.randomize() with {
      data_q.size() == local::data_q.size();
      foreach (data_q[i]) {
        data_q[i] == local::data_q[i];
      }
    };
    if (ok !== 1) begin errs++; $error("randomize failed"); end
    if (req.data_q.size() != 3) begin errs++; $error("size %0d", req.data_q.size()); end
    else if (req.data_q[0] != 1 || req.data_q[1] != 2 || req.data_q[2] != 3) begin errs++; $error("contents"); end
  endtask
endclass
module svlocalforeach1;
  initial begin
    automatic seq s = new;
    s.go();
    if (s.errs == 0) $display("PASSED");
  end
endmodule

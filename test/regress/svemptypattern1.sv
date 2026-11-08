// Empty assignment pattern for a queue or dynamic array.
// Expected values: IEEE 1800-2017 10.9.1 / 10.10 ('{} is the empty array), 7.5, 7.10,
// 13.5.3 (default argument values), 8.7 (constructor with default arguments).
`define CHK(c, m) if (!(c)) begin err++; $error(m); end
class frame;
  byte m_payload[$];
  int data[];
  function new(int t = 0, byte payload[$] = '{});
    m_payload = payload;
  endfunction
  task clear(); m_payload = '{}; data = '{}; endtask
  function int count(int a[] = '{}); return a.size(); endfunction
endclass
module svemptypattern1;
  frame f = new;
  int q[$] = '{};
  int d[] = '{};
  int e[$];
  int err = 0;
  initial begin
    `CHK(f.m_payload.size() == 0 && q.size() == 0 && d.size() == 0, "initially empty")
    f.m_payload.push_back(8'h12);
    f.data = new[3];
    q.push_back(1);
    e = '{5, 6};
    `CHK(f.m_payload.size() == 1 && f.data.size() == 3 && q.size() == 1 && e.size() == 2, "filled")
    f.clear();
    q = '{};
    e = '{};
    d = new[2];
    d = '{};
    `CHK(f.m_payload.size() == 0, "m_payload cleared")
    `CHK(f.data.size() == 0, "data cleared")
    `CHK(q.size() == 0 && e.size() == 0 && d.size() == 0, "q, e, d cleared")
    `CHK(f.count() == 0, "count() default argument")
    if (err == 0) $display("PASSED");
  end
endmodule

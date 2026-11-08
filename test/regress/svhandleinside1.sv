// Expected values: IEEE 1800-2017 11.4.13 (inside operator: true if the expression equals any member of the set,
// queues and dynamic arrays in the set are traversed element by element, a null handle matches only null),
// 8.4/8.5 (class handles, handle equality), 7.10 (queues), 7.5 (dynamic arrays).
class k;
  int id;
  function new(int i); id = i; endfunction
endclass
class d extends k;
  function new(int i); super.new(i); endfunction
endclass
module svhandleinside1;
  int err = 0;
  `define CHK(got, exp) if ((got) !== (exp)) begin err++; $error("%s: got %0d expected %0d", `"got`", got, exp); end
  initial begin
    automatic k a = new(1), b = new(2), c = new(3);
    automatic d e = new(4);
    k q[$];
    k da[];
    k n;
    q.push_back(a); q.push_back(b);
    da = new[2];
    da[0] = c; da[1] = e;
    `CHK(a inside {q}, 1)
    `CHK(c inside {q}, 0)
    `CHK(c inside {da}, 1)
    `CHK(a inside {da}, 0)
    `CHK(a inside {a, b}, 1)
    `CHK(c inside {a, b}, 0)
    `CHK(c inside {a, q, c}, 1)
    `CHK(e inside {q, da}, 1)
    `CHK(n inside {q}, 0)
    if (b inside {q}) begin end else begin err++; $error("b not found"); end
    q.delete();
    `CHK(a inside {q}, 0)
    if (err == 0) $display("PASSED");
  end
endmodule

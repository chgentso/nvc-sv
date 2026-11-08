// Self-checking: classes holding containers, linked list of handles, shallow copy of container members.
// Expected values: IEEE 1800-2017 8.4 (handles alias the same object), 8.12 (shallow copy copies property values:
// queue/associative/dynamic/fixed array properties are value-copied, so the copy is independent of the original),
// 7.4-7.10 (array/queue/associative array semantics)
`define CHK(g, e) if ((g) !== (e)) begin errs++; $error("check failed at line %0d", `__LINE__); end
module svlrm_c8h;
  int errs;
  class Node;
    int val; Node next;
    function new(int v); val = v; endfunction
  endclass
  class LinkedList;
    Node head, tail; int n;
    function void add(int v);
      Node nn = new(v);
      if (head == null) head = nn; else tail.next = nn;
      tail = nn; n++;
    endfunction
    function int sum(); int s = 0; Node c = head; while (c != null) begin s += c.val; c = c.next; end return s; endfunction
    function void rev();
      Node prev = null, cur = head, nxt;
      tail = head;
      while (cur != null) begin nxt = cur.next; cur.next = prev; prev = cur; cur = nxt; end
      head = prev;
    endfunction
    function string show(); Node c = head; string s = ""; while (c != null) begin s = {s, $sformatf("%0d ", c.val)}; c = c.next; end return s; endfunction
  endclass
  class Reg;
    int q[$]; int aa[string]; int dyn[]; int fixed[3];
    function void fill(); q = '{1,2,3}; aa["a"] = 1; aa["b"] = 2; dyn = new[2]; dyn[0] = 7; fixed = '{4,5,6}; endfunction
  endclass
  LinkedList ll; Reg r, r2; Node arr[3]; Node tmp;
  initial begin
    ll = new; ll.add(1); ll.add(2); ll.add(3); ll.add(4);
    `CHK(ll.show(), "1 2 3 4 ") `CHK(ll.sum(), 10) `CHK(ll.n, 4)
    ll.rev(); `CHK(ll.show(), "4 3 2 1 ")
    `CHK(ll.tail.val, 1) `CHK(ll.head.val, 4)
    r = new; r.fill();
    `CHK(r.q.size(), 3) `CHK(r.aa.num(), 2) `CHK(r.dyn.size(), 2) `CHK(r.fixed[2], 6)
    r2 = new r; r2.q[0] = 100; r2.aa["a"] = 100; r2.dyn[0] = 100; r2.fixed[0] = 100;
    `CHK(r.q[0], 1) `CHK(r.aa["a"], 1) `CHK(r.dyn[0], 7) `CHK(r.fixed[0], 4)
    `CHK(r2.q[0], 100) `CHK(r2.aa["a"], 100) `CHK(r2.dyn[0], 100) `CHK(r2.fixed[0], 100)
    `CHK(r2.q.size(), 3) `CHK(r2.fixed[2], 6)
    foreach (arr[i]) begin arr[i] = new(i * 10); end
    `CHK(arr[0].val, 0) `CHK(arr[1].val, 10) `CHK(arr[2].val, 20)
    tmp = arr[1]; tmp.val = 99; `CHK(arr[1].val, 99)
    arr[0] = arr[2]; arr[0].val = 5; `CHK(arr[2].val, 5) `CHK(arr[0].val, 5)
    if (errs == 0) $display("PASSED");
  end
endmodule

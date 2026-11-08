// Unpacked arrays with a lower bound other than zero declared with a typedef
// Expected values: IEEE 1800-2017 7.12.1 (find_index, find_first_index, find_last_index, unique_index:
// results are declared index values), 7.12 (item.index), 7.4.2. The order of results for a
// descending array and the representative index chosen for duplicates by unique_index are not checked.
typedef int t_a [1:3];
`define CHK(c, m) if (!(c)) begin errs++; $error(m); end
module svlobound3;
  int p [1:3] = '{3,9,9};
  int pd [3:1] = '{3,9,9};
  t_a t = '{3,9,9};
  int q[$];
  int errs = 0;
  function automatic bit has(int v); foreach (q[i]) if (q[i] == v) return 1; return 0; endfunction
  initial begin
    q = p.find_index(x) with (x == 9);
    `CHK(q.size() == 2 && has(2) && has(3), "p")
    q = p.find_first_index(x) with (x == 9);
    `CHK(q.size() == 1 && q[0] == 2, "pf")
    q = p.find_last_index(x) with (x == 9);
    `CHK(q.size() == 1 && q[0] == 3, "pl")
    q = p.unique_index();
    `CHK(q.size() == 2 && has(1) && (has(2) || has(3)), "pu")
    // pd[3]=3, pd[2]=9, pd[1]=9
    q = pd.find_index(x) with (x == 9);
    `CHK(q.size() == 2 && has(2) && has(1), "d")
    q = t.find_index(x) with (x == 9);
    `CHK(q.size() == 2 && has(2) && has(3), "t")
    q = t.find_first_index(x) with (x == 9);
    `CHK(q.size() == 1 && q[0] == 2, "tf")
    q = t.find_last_index(x) with (x == 9);
    `CHK(q.size() == 1 && q[0] == 3, "tl")
    q = t.unique_index();
    `CHK(q.size() == 2 && has(1) && (has(2) || has(3)), "tu")
    q = t.find with (item.index == 2);
    `CHK(q.size() == 1 && q[0] == 9, "ti2")
    if (errs == 0) $display("PASSED");
  end
endmodule

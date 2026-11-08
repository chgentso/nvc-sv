// Associative-array and unpacked-array elements passed by ref to subroutines taking a queue (self-checking).
// Expected values: IEEE 1800-2017 13.5.2 (ref arguments: the callee operates on the actual, an associative
// element is created by the reference), 7.8 (entry allocation), 7.10.2.3 (push_back), 7.10 (queues).
// %p layout is not checked: contents are examined element by element.
module svassocrefq1;
  typedef int item_q[$];
  item_q stim[int];
  item_q arr[2];
  int dyn_q[$];
  int errs = 0;

  task automatic ck(input bit c, input string m);
    if (!c) begin errs++; $error("%s", m); end
  endtask

  function automatic void gen(ref int q[$], input int n);
    for (int i = 0; i < n; i++) q.push_back(i * 10);
  endfunction

  task automatic t(ref int q[$]);
    q.push_back(99);
  endtask

  initial begin
    gen(stim[3], 3);
    gen(arr[1], 2);
    t(stim[3]);
    t(arr[0]);
    ck(stim.size() == 1, "stim has exactly one entry");
    ck(stim.exists(3), "stim[3] exists");
    ck(stim[3].size() == 4, "stim[3] size");
    ck(stim[3][0] == 0 && stim[3][1] == 10 && stim[3][2] == 20 && stim[3][3] == 99, "stim[3] contents");
    ck(arr[0].size() == 1 && arr[0][0] == 99, "arr[0] contents");
    ck(arr[1].size() == 2 && arr[1][0] == 0 && arr[1][1] == 10, "arr[1] contents");
    if (errs == 0) $display("PASSED");
  end
endmodule

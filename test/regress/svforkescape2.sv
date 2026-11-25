// Test automatic task parameters and locals escaping through nested
// fork..join { fork..join_none } (isolation_fork pattern) and fork..join_any
// after the enclosing task returns and its TLAB space is trimmed/reused.
module svforkescape2;
  class Item;
    string name;
    int    id;
    function new(string n, int i);
      name = n;
      id   = i;
    endfunction
    virtual function string get_full_name();
      return name;
    endfunction
  endclass

  int checked = 0;
  int join_any_done = 0;

  task automatic spawn_isolated(input Item obj,
                                input bit [63:0] mask,
                                input string tag,
                                input bit blocking,
                                output int out_id);
    out_id = obj.id;
    fork
      begin : isolation_fork
        int iso_local = obj.id + 100;
        fork
          begin
            #5;
            if (obj == null)
              $fatal(1, "escaped obj became null");
            if (obj.get_full_name() != tag)
              $fatal(1, "name mismatch: got %s expected %s",
                     obj.get_full_name(), tag);
            if (mask !== 64'hdead_beef_cafe_0000 + obj.id)
              $fatal(1, "mask mismatch: got %h for id=%0d", mask, obj.id);
            if (iso_local !== obj.id + 100)
              $fatal(1, "iso_local mismatch: got %0d for id=%0d",
                     iso_local, obj.id);
            checked++;
          end
        join_none
        if (blocking)
          wait fork;
        else
          #0;
      end : isolation_fork
    join
  endtask

  task automatic spawn_join_any(input Item obj, input int expect_id);
    int local_copy = expect_id * 3;
    fork
      begin
        #1;
      end
      begin
        #6;
        if (obj.id !== expect_id || local_copy !== expect_id * 3)
          $fatal(1, "join_any mismatch id=%0d/%0d loc=%0d",
                 obj.id, expect_id, local_copy);
        join_any_done++;
      end
    join_any
  endtask

  // Calling spawn_isolated from a helper task that returns immediately
  // triggers $TRIM on the calling process's TLAB, exposing any dangling
  // pointers into the returned task's TLAB state struct.
  task automatic launch_and_return(input Item obj,
                                   input bit [63:0] mask,
                                   input string tag,
                                   output int out_id);
    spawn_isolated(obj, mask, tag, 1'b0, out_id);
  endtask

  // Exercise TLAB allocations & trims using wide vectors after tasks return.
  task automatic churn_tlab(int rounds);
    logic [255:0] acc = 256'h1;
    logic [255:0] arr[4];
    for (int r = 0; r < rounds; r++) begin
      logic [255:0] tmp = (acc << 3) ^ {8{32'(r + 1)}};
      acc = tmp + 256'h7;
      arr[r & 3] = acc;
    end
    if (acc == 256'h0 && arr[0] == 256'h0)
      $display("unreachable");
  endtask

  initial begin
    Item items[8];
    for (int i = 0; i < 8; i++)
      items[i] = new($sformatf("reg_%0d", i), i);

    for (int i = 0; i < 8; i++) begin
      int got_id;
      fork
        launch_and_return(items[i], 64'hdead_beef_cafe_0000 + i,
                          $sformatf("reg_%0d", i), got_id);
      join
      if (got_id !== i)
        $fatal(1, "output port mismatch: got %0d expected %0d", got_id, i);
      fork
        churn_tlab(16);
      join
    end

    for (int i = 0; i < 4; i++) begin
      fork
        spawn_join_any(items[i], i);
      join
      fork
        churn_tlab(16);
      join
    end

    #20;
    if (checked !== 8)
      $fatal(1, "expected 8 isolated checks, got %0d", checked);
    if (join_any_done !== 4)
      $fatal(1, "expected 4 join_any completions, got %0d", join_any_done);

    $display("PASSED");
    $finish;
  end
endmodule

module crv2;

  int x;

  class bad;
    rand bit [7:0] arr[];
    int plain;
    constraint k1 { foreach (arr[, j]) arr[j] == 0; }
    constraint k2 { zzz > 0; }
    constraint k3;
    constraint k3 { plain == 2; }
  endclass

  class many;
    rand bit v0, v1, v2, v3, v4, v5, v6, v7, v8, v9, v10, v11, v12, v13, v14,
      v15, v16, v17, v18, v19, v20, v21, v22, v23, v24, v25, v26, v27, v28,
      v29, v30, v31, v32, v33, v34, v35, v36, v37, v38, v39, v40, v41, v42,
      v43, v44, v45, v46, v47, v48, v49, v50, v51, v52, v53, v54, v55, v56,
      v57, v58, v59, v60, v61, v62, v63, v64;
  endclass

  bad b = new;
  int res;

  function void f();
    res = b.randomize() with { local::x == 1; zzz == 2; };
    res = x.randomize();
    res = std::randomize(res) with (res) { res == 1; };
    b.plain.rand_mode(0);
    b.arr.constraint_mode(0);
    res = b.randomize(null);
  endfunction

  initial begin
    res = local::x;
  end

endmodule

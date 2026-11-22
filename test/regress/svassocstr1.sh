set -xe

cat > svassocstr1.sv <<'EOF'
module svassocstr1;
  bit seen[string];

  initial begin
    for (int i = 0; i < 3; i++)
      seen[i] = 1'b1;
    seen[8'h41] = 1'b1;
    if (!seen["A"] || !seen[1])
      $fatal(1, "FAIL: missing entry");
    $display("PASSED");
  end
endmodule
EOF

nvc -a --relaxed svassocstr1.sv -e svassocstr1 -r svassocstr1 2>&1 | tee out
grep -q "PASSED" out
if grep -qi "failure\|fatal" out; then exit 1; fi

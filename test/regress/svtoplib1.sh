set -xe

which nvc

# A top-level unit may be named with the library which contains it
mkdir -p extlib
cat > glbl.sv <<'EOF'
module glbl;
  reg GSR = 1;
  initial #10 GSR = 0;
endmodule
EOF
cat > top.sv <<'EOF'
module top;
  initial begin
    #20 if (glbl.GSR === 1'b0) $display("PASSED"); else $display("FAILED");
  end
endmodule
EOF
nvc --std=19 --work=extlib:extlib -a glbl.sv
nvc --std=19 -L . --work=work -a top.sv
nvc --std=19 -L . --work=work -e top extlib.glbl -r 2>&1 | tee out
grep -q PASSED out
if nvc --std=19 -L . --work=work -e top nolib.glbl 2>err; then exit 1; fi
grep -q "nolib" err

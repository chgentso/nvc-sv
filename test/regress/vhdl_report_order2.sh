set -xe

# A process that reports and then loops for ever in the same delta cycle:
# with the default --report-order=stable the report is held until the end
# of the delta cycle, which never comes, so it is written after about a
# second, and an interrupt writes it before the interrupt message

cat >order2.vhd <<'VHDL_EOF'
entity order2 is
end entity;

architecture test of order2 is
    signal s : bit := '0';
begin
    other: process is
    begin
        wait for 1 ns;
        report "other process";
        wait;
    end process;

    spin: process is
        variable v : bit := '0';
    begin
        wait for 1 ns;
        report "spinning";
        loop
            v := not v;
            s <= v;
        end loop;
    end process;
end architecture;
VHDL_EOF

nvc -a order2.vhd -e order2

run_spin() {
    nvc -r order2 "$@" >out2.txt 2>&1 &
    pid=$!
    # Never leave the simulation running (timeout would not pass on SIGINT,
    # which a background job of a script ignores)
    ( sleep 60; kill -9 $pid ) >/dev/null 2>&1 &
    guard=$!
    trap 'kill -9 $pid $guard 2>/dev/null || true' EXIT

    for i in $(seq 1 100); do
        grep -q spinning out2.txt && break
        sleep 0.1
    done
    cat out2.txt
    if ! grep -q spinning out2.txt; then
        echo "no report before the interrupt"
        exit 1
    fi
    grep -q "other process" out2.txt

    kill -INT $pid
    for i in $(seq 1 300); do
        kill -0 $pid 2>/dev/null || break
        sleep 0.1
    done

    if kill -0 $pid 2>/dev/null; then
        kill -KILL $pid
        echo "not interrupted"
        exit 1
    fi

    set +e
    wait $pid
    status=$?
    set -e

    cat out2.txt
    [ $status -ne 0 ]
    grep -q "interrupted in process :order2:spin" out2.txt
    kill $guard 2>/dev/null || true
}

run_spin
run_spin --report-order=immediate

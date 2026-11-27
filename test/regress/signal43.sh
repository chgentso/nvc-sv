set -xe

# An endless loop of zero-delay assignments that do not change the value
# of the driver in code compiled by LLVM can still be interrupted

cat >loop43.vhd <<'VHDL_EOF'
library ieee;
use ieee.std_logic_1164.all;

entity loop43 is
end entity;

architecture test of loop43 is
    signal v : std_logic_vector(31 downto 0) := (others => '0');
    signal s : std_logic := '1';
begin
    spin: process is
    begin
        wait for 1 ns;
        report "spinning";
        loop
            v <= (others => '0');
            s <= '1';
        end loop;
    end process;
end architecture;
VHDL_EOF

export NVC_JIT_THRESHOLD=1
export NVC_JIT_ASYNC=0

nvc --std=2008 -a loop43.vhd -e loop43
nvc --std=2008 -r loop43 >out43.txt 2>&1 &
pid=$!

for i in $(seq 1 600); do
  grep -q spinning out43.txt && break
  sleep 0.1
done
sleep 0.5

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

cat out43.txt
[ $status -ne 0 ]
grep -q "interrupted in process :loop43:spin" out43.txt

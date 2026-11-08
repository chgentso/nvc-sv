-- IEEE 1850-2010 5.2.3.1 and 5.2.3.3: prev() and stable() take an
-- argument of any type: enumeration, real, record and array values.

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity psl27 is
end entity;

architecture test of psl27 is
    type state_t is (IDLE, RUN, DONE);

    type rec_t is record
        n : integer;
        b : bit;
    end record;

    function mk(i : integer) return rec_t is
    begin
        return (i, bit'val(i mod 2));
    end function;

    function vec(i : integer) return std_logic_vector is
    begin
        return std_logic_vector(to_unsigned(abs(i) * 3, 8));
    end function;

    signal clk  : bit := '0';
    signal cnt  : natural;
    signal st   : state_t;
    signal rl   : real;
    signal rec  : rec_t;
    signal slv  : std_logic_vector(7 downto 0);
    signal str  : string(1 to 3);
    signal hold : natural;                  -- Changes every second cycle
    signal hrec : rec_t;
    signal hslv : std_logic_vector(7 downto 0);

    default clock is rising_edge(clk);
begin

    stim: process is
    begin
        for i in 0 to 9 loop
            cnt  <= i;
            st   <= state_t'val(i mod 3);
            rl   <= real(i) * 0.5;
            rec  <= mk(i);
            slv  <= vec(i);
            str  <= (1 => character'val(65 + i), others => 'z');
            hold <= i / 2;
            hrec <= mk(i / 2);
            hslv <= vec(i / 2);
            wait for 5 ns;
            clk <= '1';
            wait for 3 ns;
            clk <= '0';
            wait for 2 ns;
        end loop;
        wait;
    end process;

    -- All must hold
    a1: assert always cnt >= 1 -> prev(st) = state_t'val((cnt - 1) mod 3);
    a2: assert always cnt >= 1 -> prev(rl) = real(cnt - 1) * 0.5;
    a3: assert always cnt >= 1 -> prev(rec) = mk(cnt - 1);
    a4: assert always cnt >= 2 -> prev(slv, 2) = vec(cnt - 2);
    a5: assert always cnt >= 1 -> prev(str) = character'val(64 + cnt) & "zz";
    a6: assert always cnt >= 3 -> prev(rec, 3) = mk(cnt - 3);

    b1: assert always cnt >= 1 -> (stable(hold) = (cnt mod 2 = 1));
    b2: assert always cnt >= 1 -> (stable(hrec) = (cnt mod 2 = 1));
    b3: assert always cnt >= 1 -> (stable(hslv) = (cnt mod 2 = 1));
    b4: assert always cnt >= 1 -> not stable(st);
    b5: assert always cnt >= 1 -> not stable(rl);

end architecture;

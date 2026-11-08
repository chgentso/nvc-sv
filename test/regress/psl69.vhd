-- IEEE 1850-2010 5.2.3.1: prev() of a record value, one and several
-- cycles back.

entity psl69 is end entity;
architecture a of psl69 is
  type rec_t is record n : integer; b : bit; end record;
  function mk(i : integer) return rec_t is begin return (i, bit'val(i mod 2)); end function;
  signal clk : bit := '0';
  signal cnt : natural;
  signal rec : rec_t;
  default clock is rising_edge(clk);
begin
  stim: process is begin
    for i in 0 to 5 loop
      cnt <= i; rec <= mk(i);
      wait for 5 ns; clk <= '1'; wait for 3 ns; clk <= '0'; wait for 2 ns;
    end loop; wait; end process;
  a1: assert always cnt >= 1 -> prev(rec) = mk(cnt - 1);
  a2: assert always cnt >= 2 -> prev(rec, 2) = mk(cnt - 2);
end architecture;

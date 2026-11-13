library ieee;
use ieee.std_logic_1164.all;

entity partial2 is
end entity;

architecture test of partial2 is
    procedure set (x : out bit_vector(0 to 1); which : out integer) is
    begin
        x := "10";
        which := 1;
    end procedure;

    procedure set (x : out std_ulogic_vector(0 to 1); which : out integer) is
    begin
        x := "01";
        which := 2;
    end procedure;
begin

    p1: process is
        variable b0, b1 : bit;
        variable s0, s1 : std_ulogic;
        variable which : integer;
    begin
        -- The element types of the actuals select the overload
        set (x(0) => b0, x(1) => b1, which => which);
        assert which = 1 and b0 = '1' and b1 = '0';
        set (x(0) => s0, x(1) => s1, which => which);
        assert which = 2 and s0 = '0' and s1 = '1';
        wait;
    end process;

end architecture;

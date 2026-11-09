-- Processes and their variables through VHPI: the process statements of a
-- region, their state (nvc extension) and the values of their variables.
-- The C side is test/vhpi/vhpi31.c.

entity vhpi31 is
end entity;

architecture test of vhpi31 is
    signal s : bit;
begin
    p_count: process is
        variable n : integer := 5;
        variable v : bit_vector(3 downto 0) := "1010";
    begin
        wait for 10 ns;
        n := n + 1;
        v := "0101";
        wait on s;
    end process;

    p_sens: process (s) is
        variable t : integer := 7;
    begin
        t := t + 1;
    end process;

    p_post: postponed process is
    begin
        wait on s;
        assert s = '0' or s = '1';
    end process;
end architecture;

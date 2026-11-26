library ieee;
use ieee.std_logic_1164.all;

package sparse_force_pkg is
    type ram_t is array (0 to 63) of std_ulogic_vector(31 downto 0);
end package;

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use work.sparse_force_pkg.all;

entity sparse_force_dut is
    port (
        clk   : in  std_ulogic;
        we    : in  std_ulogic;
        waddr : in  unsigned(5 downto 0);
        wdata : in  std_ulogic_vector(31 downto 0);
        raddr : in  unsigned(5 downto 0);
        rdata : out std_ulogic_vector(31 downto 0)
    );
end entity;

architecture rtl of sparse_force_dut is
    signal mem : ram_t := (others => (others => '0'));
begin
    p_wr : process (clk) is
    begin
        if rising_edge(clk) then
            if we = '1' then
                mem(to_integer(waddr)) <= wdata;
            end if;
        end if;
    end process;

    rdata <= mem(to_integer(raddr));
end architecture;

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use work.sparse_force_pkg.all;

entity vhdl_island_sparse_force1 is
end entity;

architecture test of vhdl_island_sparse_force1 is
    signal clk      : std_ulogic := '0';
    signal we       : std_ulogic := '0';
    signal waddr    : unsigned(5 downto 0) := (others => '0');
    signal wdata    : std_ulogic_vector(31 downto 0) := (others => '0');
    signal raddr    : unsigned(5 downto 0) := to_unsigned(5, 6);
    signal rdata    : std_ulogic_vector(31 downto 0);
    signal go_force : boolean := false;
    signal done     : boolean := false;
begin
    dut : entity work.sparse_force_dut
        port map (
            clk   => clk,
            we    => we,
            waddr => waddr,
            wdata => wdata,
            raddr => raddr,
            rdata => rdata
        );

    p_force : process is
    begin
        wait until go_force;

        -- Read row 5 written before force attached
        assert << signal dut.mem : ram_t >>(5) = x"11223344"
            report "pre-force external name mismatch";

        -- Force row 5 low byte to 0xEE while leaving upper 3 bytes unforced
        << signal dut.mem : ram_t >>(5)(7 downto 0) <= force x"EE";
        wait for 1 ns;
        assert rdata = x"112233EE" report "forced slice rdata mismatch";

        -- Wait while stimulus writes 0x55667788 to row 5 and 0x99AABBCC to row 6
        wait for 20 ns;
        -- Upper 3 bytes of row 5 updated to 0x556677, forced low byte holds 0xEE
        assert rdata = x"556677EE" report "during-force row 5 mismatch";
        assert << signal dut.mem : ram_t >>(6) = x"99AABBCC"
            report "during-force unforced row 6 mismatch";

        -- Release row 5 low byte: must restore latest driven byte 0x88
        << signal dut.mem : ram_t >>(5)(7 downto 0) <= release;
        wait for 1 ns;
        assert rdata = x"55667788" report "post-release row 5 mismatch";

        done <= true;
        wait;
    end process;

    p_stim : process is
    begin
        -- Cycle 1: write 0x11223344 to row 5
        we    <= '1';
        waddr <= to_unsigned(5, 6);
        wdata <= x"11223344";
        wait for 5 ns; clk <= '1'; wait for 5 ns; clk <= '0';
        assert rdata = x"11223344" report "cycle1 rdata mismatch";

        go_force <= true;
        wait for 5 ns;

        -- Cycle 2: overwrite row 5 while low byte is forced
        waddr <= to_unsigned(5, 6);
        wdata <= x"55667788";
        wait for 5 ns; clk <= '1'; wait for 5 ns; clk <= '0';

        -- Cycle 3: write unforced row 6
        waddr <= to_unsigned(6, 6);
        wdata <= x"99AABBCC";
        wait for 5 ns; clk <= '1'; wait for 5 ns; clk <= '0';

        we <= '0';
        wait until done;

        -- Cycle 4: post-release write to row 5
        we    <= '1';
        waddr <= to_unsigned(5, 6);
        wdata <= x"CAFE0001";
        wait for 5 ns; clk <= '1'; wait for 5 ns; clk <= '0';
        assert rdata = x"CAFE0001" report "post-release write mismatch";
        wait;
    end process;
end architecture;

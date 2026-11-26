library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

-- Matches Microwatt cache_ram pattern:
-- - >1024-bit 2D RAM array (64 x 64 = 4096 bits)
-- - constant-false generic branch containing report/to_hstring
-- - local integer variables lbit/mbit defining constant-width byte slices
-- - assert not is_X(addr) on vectors
entity cache_ram_dut is
    generic (
        ROW_BITS : integer := 6;
        WIDTH    : integer := 64;
        BYTEWID  : integer := 8;
        TRACE    : boolean := false
    );
    port (
        clk     : in  std_logic;
        rd_en   : in  std_logic;
        rd_addr : in  std_logic_vector(ROW_BITS - 1 downto 0);
        rd_data : out std_logic_vector(WIDTH - 1 downto 0);
        wr_sel  : in  std_logic_vector(WIDTH / BYTEWID - 1 downto 0);
        wr_addr : in  std_logic_vector(ROW_BITS - 1 downto 0);
        wr_data : in  std_logic_vector(WIDTH - 1 downto 0)
    );
end entity;

architecture rtl of cache_ram_dut is
    constant SIZE : integer := 2**ROW_BITS;
    type ram_type is array (0 to SIZE - 1) of std_logic_vector(WIDTH - 1 downto 0);
    signal ram      : ram_type := (others => (others => '0'));
    signal rd_data0 : std_logic_vector(WIDTH - 1 downto 0) := (others => '0');
begin
    p_ram : process (clk) is
        variable lbit : integer range 0 to WIDTH - 1;
        variable mbit : integer range 0 to WIDTH - 1;
        variable widx : integer range 0 to SIZE - 1;
        constant sel0 : std_logic_vector(WIDTH / BYTEWID - 1 downto 0) := (others => '0');
    begin
        if rising_edge(clk) then
            if TRACE then
                if wr_sel /= sel0 then
                    report "write a:" & to_hstring(wr_addr);
                end if;
            end if;
            for i in 0 to WIDTH / BYTEWID - 1 loop
                lbit := i * BYTEWID;
                mbit := lbit + BYTEWID - 1;
                if wr_sel(i) = '1' then
                    assert not is_X(wr_addr);
                    widx := to_integer(unsigned(wr_addr));
                    ram(widx)(mbit downto lbit) <= wr_data(mbit downto lbit);
                end if;
            end loop;
            if rd_en = '1' then
                assert not is_X(rd_addr);
                rd_data0 <= ram(to_integer(unsigned(rd_addr)));
                if TRACE then
                    report "read a:" & to_hstring(rd_addr);
                end if;
            end if;
        end if;
    end process;

    rd_data <= rd_data0;
end architecture;

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity vhdl_island_cache_ram1 is
end entity;

architecture test of vhdl_island_cache_ram1 is
    signal clk     : std_logic := '0';
    signal rd_en   : std_logic := '0';
    signal rd_addr : std_logic_vector(5 downto 0) := (others => '0');
    signal rd_data : std_logic_vector(63 downto 0);
    signal wr_sel  : std_logic_vector(7 downto 0) := (others => '0');
    signal wr_addr : std_logic_vector(5 downto 0) := (others => '0');
    signal wr_data : std_logic_vector(63 downto 0) := (others => '0');
begin
    dut : entity work.cache_ram_dut
        port map (
            clk     => clk,
            rd_en   => rd_en,
            rd_addr => rd_addr,
            rd_data => rd_data,
            wr_sel  => wr_sel,
            wr_addr => wr_addr,
            wr_data => wr_data
        );

    p_stim : process is
        procedure tick is
        begin
            wait for 5 ns; clk <= '1'; wait for 5 ns; clk <= '0';
        end procedure;
    begin
        wr_sel  <= x"FF";
        wr_addr <= "001010";
        wr_data <= x"0123456789ABCDEF";
        rd_en   <= '1';
        rd_addr <= "001010";
        tick;
        assert rd_data = x"0000000000000000" report "RBW mismatch";

        -- Byte-enable update of bytes 0, 2, 5, 7
        wr_sel  <= "10100101";
        wr_data <= x"AA00BB0000CC00DD";
        tick;
        assert rd_data = x"0123456789ABCDEF" report "full read mismatch";

        wr_sel <= x"00";
        tick;
        assert rd_data = x"AA23BB6789CCCDDD" report "byte-enable merge mismatch";
        wait;
    end process;
end architecture;

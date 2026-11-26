library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

package extread_pkg is
    function f_log2(n : natural) return natural;

    type t_rec is record
        tag  : std_logic_vector(3 downto 0);
        data : std_logic_vector(7 downto 0);
    end record;

    type t_rec_arr is array (natural range <>) of t_rec;
end package;

package body extread_pkg is
    function f_log2(n : natural) return natural is
        variable v : natural := n - 1;
        variable r : natural := 0;
    begin
        while v > 0 loop
            v := v / 2;
            r := r + 1;
        end loop;
        return r;
    end function;
end package body;

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use work.extread_pkg.all;

-- Child RAM whose address port has user-function bounds (not statically folded
-- by vhdl-island's collect_decl_leaves) and is read only inside an unfused
-- clocked process (no event sensitivity on addr_i).
entity extread_ram is
    generic (
        g_size : natural := 16
    );
    port (
        clk    : in  std_logic;
        we     : in  std_logic;
        addr_i : in  std_logic_vector(f_log2(g_size) - 1 downto 0);
        wdata  : in  std_logic_vector(7 downto 0);
        rdata  : out std_logic_vector(7 downto 0)
    );
end entity;

architecture rtl of extread_ram is
    type t_mem is array (0 to g_size - 1) of std_logic_vector(7 downto 0);
    signal we_d : std_logic;
begin
    -- Fusable comb process so this entity has islands alongside unfused RAM
    we_d <= we;

    p_ram : process (clk) is
        variable mem : t_mem := (others => (others => '0'));
        variable idx : natural;
    begin
        if rising_edge(clk) then
            assert now >= 0 ns;
            idx := to_integer(unsigned(addr_i));
            if we_d = '1' then
                mem(idx) := wdata;
            end if;
            rdata <= mem(idx);
        end if;
    end process;
end architecture;

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use work.extread_pkg.all;

-- Child entity reading unconstrained vector and array-of-records ports inside
-- an unfused clocked process (exercises dynamic uarray and non-homogeneous
-- array-of-records ext_read marking).
entity extread_uncons_sink is
    port (
        clk     : in  std_logic;
        vec_i   : in  std_logic_vector;
        recs_i  : in  t_rec_arr;
        sum_o   : out unsigned(7 downto 0)
    );
end entity;

architecture rtl of extread_uncons_sink is
begin
    p_sink : process (clk) is
        variable acc : unsigned(7 downto 0);
    begin
        if rising_edge(clk) then
            assert now >= 0 ns;
            acc := resize(unsigned(vec_i), 8);
            for i in recs_i'range loop
                acc := acc + unsigned(recs_i(i).data)
                           + resize(unsigned(recs_i(i).tag), 8);
            end loop;
            sum_o <= acc;
        end if;
    end process;
end architecture;

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use work.extread_pkg.all;

-- Simple island producer used inside a generate block with local signals
entity extread_prod is
    port (
        clk   : in  std_logic;
        din   : in  std_logic_vector(7 downto 0);
        dout  : out std_logic_vector(7 downto 0)
    );
end entity;

architecture rtl of extread_prod is
begin
    p_ff : process (clk) is
    begin
        if rising_edge(clk) then
            dout <= std_logic_vector(unsigned(din) + 1);
        end if;
    end process;
end architecture;

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use work.extread_pkg.all;

entity extread_wrap is
    port (
        clk       : in  std_logic;
        sel       : in  std_logic;
        we        : in  std_logic;
        host_addr : in  std_logic_vector(7 downto 0);
        cpu_addr  : in  std_logic_vector(7 downto 0);
        wdata     : in  std_logic_vector(7 downto 0);
        rdata     : out std_logic_vector(7 downto 0);
        sum_o     : out unsigned(7 downto 0);
        gen_o     : out std_logic_vector(7 downto 0)
    );
end entity;

architecture rtl of extread_wrap is
    signal addr_mux : std_logic_vector(7 downto 0);
    signal vec_drv  : std_logic_vector(7 downto 0);
    signal recs_drv : t_rec_arr(0 to 1);
begin
    -- Island combinational logic driving signals that are only consumed by
    -- collapsed child ports read in unfused clocked processes.
    addr_mux <= host_addr when sel = '1' else cpu_addr;
    vec_drv  <= std_logic_vector(unsigned(addr_mux) + 3);

    p_recs : process (addr_mux, wdata) is
    begin
        recs_drv(0).tag  <= addr_mux(3 downto 0);
        recs_drv(0).data <= wdata;
        recs_drv(1).tag  <= addr_mux(7 downto 4);
        recs_drv(1).data <= addr_mux;
    end process;

    u_ram : entity work.extread_ram
        generic map (
            g_size => 16
        )
        port map (
            clk    => clk,
            we     => we,
            addr_i => addr_mux(f_log2(16) + 1 downto 2),
            wdata  => wdata,
            rdata  => rdata
        );

    u_sink : entity work.extread_uncons_sink
        port map (
            clk    => clk,
            vec_i  => vec_drv(5 downto 0),
            recs_i => recs_drv,
            sum_o  => sum_o
        );

    g_local : if true generate
        signal loc_sig : std_logic_vector(7 downto 0);
    begin
        u_prod : entity work.extread_prod
            port map (
                clk  => clk,
                din  => wdata,
                dout => loc_sig
            );

        p_cap : process (clk) is
        begin
            if rising_edge(clk) then
                assert now >= 0 ns;
                gen_o <= loc_sig;
            end if;
        end process;
    end generate;
end architecture;

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;
use work.extread_pkg.all;

entity vhdl_island_fast_extread1 is
end entity;

architecture test of vhdl_island_fast_extread1 is
    signal clk       : std_logic := '0';
    signal sel       : std_logic := '1';
    signal we        : std_logic := '0';
    signal host_addr : std_logic_vector(7 downto 0) := (others => '0');
    signal cpu_addr  : std_logic_vector(7 downto 0) := (others => '0');
    signal wdata     : std_logic_vector(7 downto 0) := (others => '0');
    signal rdata     : std_logic_vector(7 downto 0);
    signal sum_o     : unsigned(7 downto 0);
    signal gen_o     : std_logic_vector(7 downto 0);
begin
    dut : entity work.extread_wrap
        port map (
            clk       => clk,
            sel       => sel,
            we        => we,
            host_addr => host_addr,
            cpu_addr  => cpu_addr,
            wdata     => wdata,
            rdata     => rdata,
            sum_o     => sum_o,
            gen_o     => gen_o
        );

    p_stim : process is
        procedure tick is
        begin
            wait for 5 ns;
            clk <= '1';
            wait for 5 ns;
            clk <= '0';
        end procedure;
    begin
        -- Write 0xA1 to RAM slot 1 (addr 4), 0xB2 to slot 2 (addr 8),
        -- 0xC3 to slot 5 (addr 20) via host_addr
        sel <= '1';
        we  <= '1';

        host_addr <= std_logic_vector(to_unsigned(4, 8));
        wdata     <= x"A1";
        tick;

        host_addr <= std_logic_vector(to_unsigned(8, 8));
        wdata     <= x"B2";
        tick;

        host_addr <= std_logic_vector(to_unsigned(20, 8));
        wdata     <= x"C3";
        tick;

        -- Switch mux to cpu_addr and read back each slot
        sel   <= '0';
        we    <= '0';
        wdata <= x"10";

        cpu_addr <= std_logic_vector(to_unsigned(4, 8));
        tick;
        assert rdata = x"A1"
            report "RAM slot 1 mismatch: got " & to_hstring(rdata);
        -- vec_drv(5 downto 0) = (4+3)=7, recs(0)=(4,0x10), recs(1)=(0,4)
        -- sum = 7 + 16 + 4 + 4 + 0 = 31 (0x1F)
        assert sum_o = to_unsigned(31, 8)
            report "uncons/record sink mismatch at addr 4: got " & to_hstring(sum_o);
        -- gen_o is 1 cycle behind u_prod (previous wdata was 0xC3 -> 0xC4)
        assert gen_o = x"C4"
            report "generate local signal mismatch: got " & to_hstring(gen_o);

        cpu_addr <= std_logic_vector(to_unsigned(8, 8));
        tick;
        assert rdata = x"B2"
            report "RAM slot 2 mismatch: got " & to_hstring(rdata);
        -- vec_drv(5 downto 0) = (8+3)=11, recs(0)=(8,0x10), recs(1)=(0,8)
        -- sum = 11 + 16 + 8 + 8 + 0 = 43 (0x2B)
        assert sum_o = to_unsigned(43, 8)
            report "uncons/record sink mismatch at addr 8: got " & to_hstring(sum_o);
        -- previous wdata was 0x10 -> 0x11
        assert gen_o = x"11"
            report "generate local signal mismatch 2: got " & to_hstring(gen_o);

        cpu_addr <= std_logic_vector(to_unsigned(20, 8));
        tick;
        assert rdata = x"C3"
            report "RAM slot 5 mismatch: got " & to_hstring(rdata);
        -- vec_drv(5 downto 0) = (20+3)=23, recs(0)=(4,0x10), recs(1)=(1,20)
        -- sum = 23 + 16 + 4 + 20 + 1 = 64 (0x40)
        assert sum_o = to_unsigned(64, 8)
            report "uncons/record sink mismatch at addr 20: got " & to_hstring(sum_o);

        wait;
    end process;
end architecture;

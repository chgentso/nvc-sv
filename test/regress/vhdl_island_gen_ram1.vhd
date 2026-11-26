library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

-- Matches NEORV32 neorv32_prim_fifo / neorv32_prim_spram pattern:
-- RAM signal declared inside nested if-generate blocks, writing parent rdata.
entity gen_fifo_dut is
    generic (
        AWIDTH  : natural := 6;   -- 64 entries x 32 bits = 2048 bits (> 1024)
        DWIDTH  : natural := 32;
        ASYNCRD : boolean := false
    );
    port (
        clk_i   : in  std_ulogic;
        rstn_i  : in  std_ulogic;
        we_i    : in  std_ulogic;
        re_i    : in  std_ulogic;
        wdata_i : in  std_ulogic_vector(DWIDTH-1 downto 0);
        rdata_o : out std_ulogic_vector(DWIDTH-1 downto 0);
        avail_o : out std_ulogic
    );
end entity;

architecture rtl of gen_fifo_dut is
    type ram_t is array ((2**AWIDTH)-1 downto 0) of std_ulogic_vector(DWIDTH-1 downto 0);
    signal rdata : std_ulogic_vector(DWIDTH-1 downto 0);
    signal we, re, full, empty, avail : std_ulogic;
    signal w_pnt, r_pnt : unsigned(AWIDTH downto 0);
begin
    p_ptr : process (rstn_i, clk_i) is
    begin
        if rstn_i = '0' then
            w_pnt <= (others => '0');
            r_pnt <= (others => '0');
        elsif rising_edge(clk_i) then
            if we = '1' then
                w_pnt <= w_pnt + 1;
            end if;
            if re = '1' then
                r_pnt <= r_pnt + 1;
            end if;
        end if;
    end process;

    empty <= '1' when w_pnt = r_pnt else '0';
    full  <= '1' when (w_pnt(AWIDTH) /= r_pnt(AWIDTH))
                      and (w_pnt(AWIDTH-1 downto 0) = r_pnt(AWIDTH-1 downto 0)) else '0';
    we    <= we_i and (not full);
    re    <= re_i and (not empty);

    status_sync : if not ASYNCRD generate
        p_avail : process (clk_i) is
        begin
            if rising_edge(clk_i) then
                avail <= not empty;
            end if;
        end process;
    end generate;

    status_async : if ASYNCRD generate
        avail <= not empty;
    end generate;

    avail_o <= avail;

    memory_large : if AWIDTH > 0 generate
        memory_sync : if not ASYNCRD generate
            signal fifo : ram_t := (others => (others => '0'));
        begin
            memory_core : process (clk_i) is
            begin
                if rising_edge(clk_i) then
                    if we = '1' then
                        fifo(to_integer(w_pnt(AWIDTH-1 downto 0))) <= wdata_i;
                    end if;
                    rdata <= fifo(to_integer(r_pnt(AWIDTH-1 downto 0)));
                end if;
            end process;
        end generate;

        memory_async : if ASYNCRD generate
            signal fifo : ram_t := (others => (others => '0'));
        begin
            memory_core : process (clk_i) is
            begin
                if rising_edge(clk_i) then
                    if we = '1' then
                        fifo(to_integer(w_pnt(AWIDTH-1 downto 0))) <= wdata_i;
                    end if;
                end if;
            end process;
            rdata <= fifo(to_integer(r_pnt(AWIDTH-1 downto 0)));
        end generate;
    end generate;

    rdata_o <= rdata when avail = '1' else (others => '0');
end architecture;

-- Sibling for-generate blocks with local signals each writing a distinct slice
-- of a parent signal (tests multi-writer guard on ancestor signals).
library ieee;
use ieee.std_logic_1164.all;

entity gen_sibling_guard_dut is
    port (
        clk : in  std_ulogic;
        din : in  std_ulogic_vector(3 downto 0);
        q   : out std_ulogic_vector(3 downto 0)
    );
end entity;

architecture rtl of gen_sibling_guard_dut is
    signal acc : std_ulogic_vector(3 downto 0) := "0000";
begin
    g_lanes : for i in 0 to 3 generate
        signal tmp : std_ulogic := '0';
    begin
        p_lane : process (clk) is
        begin
            if rising_edge(clk) then
                tmp    <= din(i);
                acc(i) <= tmp;
            end if;
        end process;
    end generate;

    q <= acc;
end architecture;

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

entity vhdl_island_gen_ram1 is
end entity;

architecture test of vhdl_island_gen_ram1 is
    signal clk     : std_ulogic := '0';
    signal rstn    : std_ulogic := '0';
    signal we      : std_ulogic := '0';
    signal re      : std_ulogic := '0';
    signal wdata   : std_ulogic_vector(31 downto 0) := (others => '0');
    signal s_rdata : std_ulogic_vector(31 downto 0);
    signal s_avail : std_ulogic;
    signal a_rdata : std_ulogic_vector(31 downto 0);
    signal a_avail : std_ulogic;
    signal s_din   : std_ulogic_vector(3 downto 0) := "0000";
    signal s_q     : std_ulogic_vector(3 downto 0);
begin
    u_sync : entity work.gen_fifo_dut
        generic map (AWIDTH => 6, DWIDTH => 32, ASYNCRD => false)
        port map (
            clk_i   => clk,
            rstn_i  => rstn,
            we_i    => we,
            re_i    => re,
            wdata_i => wdata,
            rdata_o => s_rdata,
            avail_o => s_avail
        );

    u_async : entity work.gen_fifo_dut
        generic map (AWIDTH => 6, DWIDTH => 32, ASYNCRD => true)
        port map (
            clk_i   => clk,
            rstn_i  => rstn,
            we_i    => we,
            re_i    => re,
            wdata_i => wdata,
            rdata_o => a_rdata,
            avail_o => a_avail
        );

    u_guard : entity work.gen_sibling_guard_dut
        port map (clk => clk, din => s_din, q => s_q);

    p_stim : process is
        procedure tick is
        begin
            wait for 5 ns;
            clk <= '1';
            wait for 5 ns;
            clk <= '0';
        end procedure;
    begin
        rstn <= '0';
        tick;
        rstn <= '1';
        wait for 1 ns;
        assert a_avail = '0' report "async reset avail";

        -- Push 4 words
        we <= '1';
        for i in 1 to 4 loop
            wdata <= std_ulogic_vector(to_unsigned(i * 16#11111111#, 32));
            s_din <= std_ulogic_vector(to_unsigned(i, 4));
            tick;
            if i = 1 then
                -- Async FIFO immediately exposes first word (FWFT)
                assert a_avail = '1' report "async avail after push 1";
                assert a_rdata = x"11111111" report "async FWFT word 1";
            elsif i = 2 then
                -- Sync FIFO exposes first word one cycle later
                assert s_avail = '1' report "sync avail after push 2";
                assert s_rdata = x"11111111" report "sync word 1";
            end if;
        end loop;
        we <= '0';

        -- Verify sibling guard (2-cycle delay: i=3 -> s_q = "0011")
        assert s_q = "0011" report "sibling guard mismatch";

        -- Pop words from async FIFO and sync FIFO
        re <= '1';
        tick;
        assert a_rdata = x"22222222" report "async word 2";
        tick;
        assert a_rdata = x"33333333" report "async word 3";
        assert s_rdata = x"22222222" report "sync word 2";
        tick;
        assert a_rdata = x"44444444" report "async word 4";
        assert s_rdata = x"33333333" report "sync word 3";
        tick;
        assert a_avail = '0' report "async empty";
        assert s_rdata = x"44444444" report "sync word 4";
        re <= '0';
        tick;
        assert s_avail = '0' report "sync empty";
        wait;
    end process;
end architecture;

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

-- Sync-read RAM (> 1024 bits) with byte-enable slice writes + bulk clear
-- (exercises inline sparse log + overflow fallback)
entity sparse_sync_ram is
    port (
        clk      : in  std_ulogic;
        bulk_clr : in  std_ulogic;
        we       : in  std_ulogic;
        ben      : in  std_ulogic_vector(3 downto 0);
        waddr    : in  unsigned(6 downto 0);
        wdata    : in  std_ulogic_vector(31 downto 0);
        raddr    : in  unsigned(6 downto 0);
        rdata    : out std_ulogic_vector(31 downto 0)
    );
end entity;

architecture rtl of sparse_sync_ram is
    type ram_t is array (0 to 127) of std_ulogic_vector(31 downto 0);
    signal mem : ram_t := (others => (others => '0'));
begin
    p_ram : process (clk) is
    begin
        if rising_edge(clk) then
            if bulk_clr = '1' then
                -- 24 writes in one cycle (> RT_ISLAND_SPARSE_INLINE_CAP = 16)
                for i in 0 to 23 loop
                    mem(i) <= std_ulogic_vector(to_unsigned(i * 17 + 5, 32));
                end loop;
            elsif we = '1' then
                for b in 0 to 3 loop
                    if ben(b) = '1' then
                        mem(to_integer(waddr))((b + 1) * 8 - 1 downto b * 8) <=
                            wdata((b + 1) * 8 - 1 downto b * 8);
                    end if;
                end loop;
            end if;
            -- Read-before-write on same address must see previous cycle value
            rdata <= mem(to_integer(raddr));
        end if;
    end process;
end architecture;

library ieee;
use ieee.std_logic_1164.all;
use ieee.numeric_std.all;

-- Async-read FIFO-style RAM (> 1024 bits): clocked write + comb read
entity sparse_async_ram is
    port (
        clk   : in  std_ulogic;
        we    : in  std_ulogic;
        waddr : in  unsigned(5 downto 0);
        wdata : in  std_ulogic_vector(31 downto 0);
        raddr : in  unsigned(5 downto 0);
        rdata : out std_ulogic_vector(31 downto 0)
    );
end entity;

architecture rtl of sparse_async_ram is
    type ram_t is array (63 downto 0) of std_ulogic_vector(31 downto 0);
    signal mem : ram_t := (others => x"00000000");
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

entity vhdl_island_sparse_ram1 is
end entity;

architecture test of vhdl_island_sparse_ram1 is
    signal clk      : std_ulogic := '0';
    signal bulk_clr : std_ulogic := '0';
    signal s_we     : std_ulogic := '0';
    signal s_ben    : std_ulogic_vector(3 downto 0) := "1111";
    signal s_waddr  : unsigned(6 downto 0) := (others => '0');
    signal s_wdata  : std_ulogic_vector(31 downto 0) := (others => '0');
    signal s_raddr  : unsigned(6 downto 0) := (others => '0');
    signal s_rdata  : std_ulogic_vector(31 downto 0);

    signal a_we     : std_ulogic := '0';
    signal a_waddr  : unsigned(5 downto 0) := (others => '0');
    signal a_wdata  : std_ulogic_vector(31 downto 0) := (others => '0');
    signal a_raddr  : unsigned(5 downto 0) := (others => '0');
    signal a_rdata  : std_ulogic_vector(31 downto 0);
begin
    u_sync : entity work.sparse_sync_ram
        port map (
            clk      => clk,
            bulk_clr => bulk_clr,
            we       => s_we,
            ben      => s_ben,
            waddr    => s_waddr,
            wdata    => s_wdata,
            raddr    => s_raddr,
            rdata    => s_rdata
        );

    u_async : entity work.sparse_async_ram
        port map (
            clk   => clk,
            we    => a_we,
            waddr => a_waddr,
            wdata => a_wdata,
            raddr => a_raddr,
            rdata => a_rdata
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
        -- 1. Write full word to sync RAM addr 10 and read addr 10 simultaneously
        s_we    <= '1';
        s_ben   <= "1111";
        s_waddr <= to_unsigned(10, 7);
        s_wdata <= x"12345678";
        s_raddr <= to_unsigned(10, 7);
        tick;
        -- Read-before-write must return old 0x00000000
        assert s_rdata = x"00000000" report "sync RBW mismatch";

        -- 2. Partial byte-enable write to addr 10 (bytes 1 and 3) while reading addr 10
        s_ben   <= "1010";
        s_wdata <= x"AA00BB00";
        tick;
        assert s_rdata = x"12345678" report "sync full read mismatch";

        -- 3. Read back merged byte-enable result
        s_we <= '0';
        tick;
        assert s_rdata = x"AA34BB78" report "sync byte-enable merge mismatch";

        -- 4. Exercise sparse log overflow (> 16 writes in one cycle)
        bulk_clr <= '1';
        s_raddr  <= to_unsigned(10, 7);
        tick;
        assert s_rdata = x"AA34BB78" report "sync RBW during bulk mismatch";
        bulk_clr <= '0';

        for i in 0 to 23 loop
            s_raddr <= to_unsigned(i, 7);
            tick;
            assert s_rdata = std_ulogic_vector(to_unsigned(i * 17 + 5, 32))
                report "bulk overflow entry mismatch at " & integer'image(i);
        end loop;

        -- 5. Async-read RAM: write addr 42 while raddr is already 42 (comb wakeup!)
        a_raddr <= to_unsigned(42, 6);
        wait for 1 ns;
        assert a_rdata = x"00000000" report "async init mismatch";

        a_we    <= '1';
        a_waddr <= to_unsigned(42, 6);
        a_wdata <= x"DEADBEEF";
        tick;
        assert a_rdata = x"DEADBEEF" report "async comb wakeup mismatch";

        -- Write another address 7, switch raddr without clock
        a_waddr <= to_unsigned(7, 6);
        a_wdata <= x"CAFEBABE";
        tick;
        assert a_rdata = x"DEADBEEF" report "async unchanged addr mismatch";
        a_we    <= '0';
        a_raddr <= to_unsigned(7, 6);
        wait for 1 ns;
        assert a_rdata = x"CAFEBABE" report "async switch addr mismatch";

        wait;
    end process;
end architecture;

entity wait32 is
end entity;

architecture test of wait32 is
    signal clk   : bit := '0';
    signal en    : bit := '0';
    signal ready : bit := '0';
    signal stop  : boolean := false;
    signal ticks : integer := 0;

    procedure wait_handshake(signal c, e, r : in bit;
                             expected_t     : in time) is
    begin
        wait until e = '1' and r = '1' and c = '1' and c'event;
        assert now = expected_t
            report "expected " & time'image(expected_t) &
                   " got " & time'image(now)
            severity failure;
    end procedure;
begin
    clk_gen: process is
    begin
        while not stop loop
            wait for 5 ns;
            clk <= not clk;
            if clk = '0' then
                ticks <= ticks + 1;
            end if;
        end loop;
        wait;
    end process;

    stim: process is
    begin
        -- Many non-matching edges before handshake at 55 ns (rising edge 6)
        wait for 22 ns;
        en <= '1';
        wait for 30 ns;
        ready <= '1';
        wait for 10 ns;
        en <= '0';
        ready <= '0';

        -- Wakeup after timeout test (starts at 62 ns, timeout 20 ns -> 82 ns)
        -- Next handshake at 115 ns
        wait for 48 ns;
        en <= '1';
        ready <= '1';
        wait for 20 ns;
        stop <= true;
        wait;
    end process;

    checker: process is
    begin
        -- 1. Repeated non-matching wakeups inside procedure
        wait_handshake(clk, en, ready, 55 ns);

        -- 2. wait until with timeout that expires after several clk wakeups
        wait until en = '1' and ready = '1' and clk = '1' and clk'event for 20 ns;
        assert now = 75 ns
            report "timeout expected 75 ns got " & time'image(now)
            severity failure;

        -- 3. wait until with 0 ns timeout
        wait until en = '1' for 0 ns;
        assert now = 75 ns
            report "zero timeout expected 75 ns got " & time'image(now)
            severity failure;

        -- 4. wait until with timeout that matches early
        wait until en = '1' and ready = '1' and clk = '1' and clk'event for 100 ns;
        assert now = 115 ns
            report "early match expected 115 ns got " & time'image(now)
            severity failure;

        -- 5. Verify triggers were cleared on exit (process must not re-wake)
        wait;
    end process;

    -- 6. FIFO waiter order on a shared signal when a wait-until stays armed
    --    and an earlier waiter exits and re-enters before the next event.
    fifo_order: block is
        type tag_t is protected
            procedure set(v : integer);
            impure function get return integer;
        end protected;

        type tag_t is protected body
            variable val : integer := 0;
            procedure set(v : integer) is
            begin
                val := v;
            end procedure;
            impure function get return integer is
            begin
                return val;
            end function;
        end protected body;

        signal bus_sig : integer := 0;
        shared variable order_tag : tag_t;
    begin
        waiter_a: process is
        begin
            wait on bus_sig until bus_sig = 1;
            -- Exits wait on bus_sig = 1 while waiter_b stays armed on bus_sig
            wait for 1 ns;
            -- Re-enters wait on bus_sig AFTER waiter_b began waiting
            wait on bus_sig until bus_sig = 2;
            assert order_tag.get = 2
                report "expected waiter_b to wake before waiter_a, got " &
                       integer'image(order_tag.get)
                severity failure;
            wait;
        end process;

        waiter_b: process is
        begin
            wait for 0 ns;   -- enter wait after waiter_a initially
            wait on bus_sig until bus_sig = 2;
            order_tag.set(2);
            wait;
        end process;

        driver: process is
        begin
            wait for 10 ns;
            bus_sig <= 1;
            wait for 10 ns;
            bus_sig <= 2;
            wait;
        end process;
    end block;
end architecture;

entity vhpi_rorder1 is
end entity;

architecture test of vhpi_rorder1 is
    signal s : integer := 0;

    procedure show (x : in integer);

    procedure show (x : in integer) is
    begin
        report "foreign binding was not resolved" severity failure;
    end procedure;

    attribute foreign : string;
    attribute foreign of show : procedure is "VHPI vhpi_rorder1 show";
begin
    -- These run in the same delta cycles: their output is written in the
    -- order they are declared, the output of the foreign procedure and of
    -- the VHPI value change callback after what is held before it
    p_a : process (s) is
    begin
        report "p_a s=" & integer'image(s);
    end process;

    p_f : process (s) is
    begin
        report "p_f before s=" & integer'image(s);
        show(s);
        report "p_f after s=" & integer'image(s);
    end process;

    p_b : process (s) is
    begin
        report "p_b s=" & integer'image(s);
    end process;

    stim : process is
    begin
        wait for 1 ns;
        s <= 1;
        wait for 1 ns;
        s <= 2;
        wait;
    end process;
end architecture;

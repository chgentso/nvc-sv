entity conv_prot1_in is
    port ( x : in boolean; y : out boolean );
end entity;

architecture beh of conv_prot1_in is
begin
    y <= x;
end architecture;

entity conv_prot1_out is
    port ( a : in bit; b : out bit );
end entity;

architecture beh of conv_prot1_out is
begin
    b <= a;
end architecture;

entity conv_prot1 is
end entity;

architecture test of conv_prot1 is
    type conv_t is protected
        impure function to_bool(v : bit) return boolean;
    end protected;

    type conv_t is protected body
        impure function to_bool(v : bit) return boolean is
        begin
            return v = '1';
        end function;
    end protected body;

    shared variable cv : conv_t;
    signal s_in  : bit := '0';
    signal s_o1  : boolean := false;
    signal s_o2  : boolean := false;
begin
    u_in: entity work.conv_prot1_in
        port map ( x => cv.to_bool(s_in), y => s_o1 );

    u_out: entity work.conv_prot1_out
        port map ( a => s_in, cv.to_bool(b) => s_o2 );

    stim: process is
    begin
        wait for 1 ns;
        assert s_o1 = false report "expected s_o1=false" severity failure;
        assert s_o2 = false report "expected s_o2=false" severity failure;
        s_in <= '1';
        wait for 1 ns;
        assert s_o1 = true report "expected s_o1=true" severity failure;
        assert s_o2 = true report "expected s_o2=true" severity failure;
        wait;
    end process;
end architecture;

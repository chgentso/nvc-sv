entity sig_idxbounds1 is
    generic (g : integer := 4; n : integer := 0);
end entity;

architecture test of sig_idxbounds1 is
    signal s : string(g downto n) := (others => 'a');
begin
    process is
    begin
        report s;
        wait;
    end process;
end architecture;

entity sig_idxbounds1_alloc is
    generic (n : integer := 0);
end entity;

architecture test of sig_idxbounds1_alloc is
    type str_ptr is access string;
begin
    process is
        variable p : str_ptr;
    begin
        p := new string(4 downto n);
        report integer'image(p'length);
        wait;
    end process;
end architecture;

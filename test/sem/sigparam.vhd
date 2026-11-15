entity sigparam is
end entity;

architecture test of sigparam is
    signal i : integer := 3;
    signal n : natural := 0;
    signal b : bit;

    procedure rd_nat (signal x : in natural) is
    begin
    end procedure;

    procedure rd_int (signal x : in integer) is
    begin
    end procedure;

    procedure wr_int (signal x : out integer) is
    begin
    end procedure;

    procedure wr_nat (signal x : out natural) is
    begin
    end procedure;

    procedure wr_bit (signal x : out bit) is
    begin
    end procedure;
begin
    process is
    begin
        rd_nat(i);                      -- Error
        rd_int(n);                      -- OK
        wr_int(n);                      -- Error
        wr_nat(i);                      -- OK
        wr_bit(b);                      -- OK
        wait;
    end process;
end architecture;

entity attrspec is
    generic (g : integer := 1);
    port (p : in bit := '1');
    attribute pin : integer;
    attribute pin of attrspec : entity is 1;  -- OK
    attribute pin of p : signal is 2;         -- OK
    attribute pin of g : constant is 3;       -- OK
    attribute other : integer;
    attribute delay : time;
    attribute delay of attrspec : entity is 10 ns;  -- OK
end entity;

architecture test of attrspec is
    signal s1, s2, s3 : integer;
    attribute other of attrspec : entity is 3;  -- Error
    attribute other of p : signal is 7;       -- Error
    attribute pin of test : architecture is 4;  -- OK
    attribute pin of s1 : signal is 5;        -- OK
    attribute pin of others : signal is 6;    -- OK
    attribute pin of s2 : signal is 7;        -- Error
    attribute pin of all : variable is 8;     -- OK

    procedure proc1 is
    begin
    end procedure;
    attribute foreign of proc1 : procedure is "VHPIDIRECT proc1";  -- OK
    constant name : string := "proc2";
    procedure proc2 is
    begin
    end procedure;
    attribute foreign of proc2 : procedure is name;  -- OK
    procedure proc3 is
    begin
    end procedure;
    attribute foreign of proc3 : procedure is s1'path_name;  -- Error
begin
    blk: block is
        attribute other of test : architecture is 10;  -- Error
        attribute extra : integer;
        attribute extra of p : signal is 11;  -- Error
    begin
    end block;
end architecture;

entity attrspec2 is
    generic (g : integer := 1);
    attribute pin : integer;
    attribute pin of attrspec2 : entity is g;  -- Error
end entity;

entity protself is
end entity;

architecture test of protself is
    type outer is protected
        procedure bump;
    end protected;

    type outer is protected body
        type outers is array (0 to 1) of outer;
        variable selves : outers;               -- Error
        variable one : outer;                   -- Error
        procedure bump is
        begin
        end procedure;
    end protected body;
begin
end architecture;

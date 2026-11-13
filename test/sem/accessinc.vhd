package accessinc is
    type f1 is file of character;
    type a1 is access f1;                -- Error
    type foo;
    type foo_acc is access foo;          -- Error
    type foo is file of character;
    type node;
    type node_acc is access node;        -- OK
    type node is record
        next_p : node_acc;
    end record;
end package;

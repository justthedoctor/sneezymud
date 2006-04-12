CREATE TABLE shopowned (
    shop_nr integer DEFAULT '0' NOT NULL,
    profit_buy double precision DEFAULT '0.00' NOT NULL,
    profit_sell double precision DEFAULT '0.00' NOT NULL,
    max_num integer,
    password varchar(32),
    corp_id integer,
    dividend double precision,
    reserve_max integer,
    reserve_min integer,
    no_such_item1 varchar(127),
    no_such_item2 varchar(127),
    do_not_buy varchar(127),
    missing_cash1 varchar(127),
    missing_cash2 varchar(127),
    message_buy varchar(127),
    message_sell varchar(127)
);
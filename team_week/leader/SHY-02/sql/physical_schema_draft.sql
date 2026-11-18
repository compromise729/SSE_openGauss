-- SHY-02 executable physical-shape fixture. LMD-01 implements production functions.
CREATE SCHEMA shy02_contract;
CREATE TABLE shy02_contract.demo_data (
    rid text PRIMARY KEY CHECK (rid ~ '^[0-9a-f]{32}$'),
    data_blob text NOT NULL CHECK (octet_length(data_blob) BETWEEN 88 AND 65624)
);
CREATE TABLE shy02_contract.demo_index (
    ut text PRIMARY KEY,
    locator_blob text NOT NULL CHECK (octet_length(locator_blob) = 96)
);
CREATE TABLE shy02_contract.catalog (
    logical_schema text NOT NULL,
    logical_table text NOT NULL,
    protocol_version integer NOT NULL CHECK (protocol_version = 1),
    PRIMARY KEY (logical_schema,logical_table)
);
INSERT INTO shy02_contract.demo_data VALUES (repeat('a',32),repeat('A',88));
INSERT INTO shy02_contract.demo_index VALUES ('public_fixture_ut',repeat('B',96));
SELECT 'SHAPE_OK', count(*) FROM shy02_contract.demo_data;
SELECT 'LOCATOR_SIZE_OK',octet_length(locator_blob) FROM shy02_contract.demo_index;
DO $$ BEGIN
  BEGIN
    INSERT INTO shy02_contract.demo_data VALUES (repeat('a',32),repeat('A',88));
    RAISE EXCEPTION 'DUPLICATE_RID_WAS_ACCEPTED';
  EXCEPTION WHEN unique_violation THEN RAISE NOTICE 'DUPLICATE_RID_REJECTED'; END;
  BEGIN
    INSERT INTO shy02_contract.demo_index VALUES ('bad','short');
    RAISE EXCEPTION 'SHORT_LOCATOR_WAS_ACCEPTED';
  EXCEPTION WHEN check_violation THEN RAISE NOTICE 'SHORT_LOCATOR_REJECTED'; END;
END $$;

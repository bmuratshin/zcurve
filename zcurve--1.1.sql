/* contrib/zcurve/zcurve--1.0.sql */

-- complain if script is sourced in psql, rather than via CREATE EXTENSION
\echo Use "CREATE EXTENSION zcurve" to load this file. \quit


CREATE DOMAIN zcurve AS pg_catalog.oid;


CREATE FUNCTION zcurve_val_from_xy(bigint, bigint)
RETURNS bigint
AS 'MODULE_PATHNAME'
LANGUAGE C IMMUTABLE STRICT;

CREATE FUNCTION zcurve_oids_by_extent(bigint, bigint, bigint, bigint)
RETURNS bigint
AS 'MODULE_PATHNAME'
LANGUAGE C IMMUTABLE STRICT;

CREATE FUNCTION zcurve_oids_by_extent_ii(bigint, bigint, bigint, bigint)
RETURNS bigint
AS 'MODULE_PATHNAME'
LANGUAGE C IMMUTABLE STRICT;

CREATE FUNCTION zcurve_2d_count(text, bigint, bigint, bigint, bigint)
RETURNS bigint
AS 'MODULE_PATHNAME'
LANGUAGE C IMMUTABLE STRICT;

CREATE FUNCTION zcurve_2d_lookup(text, bigint, bigint, bigint, bigint)
RETURNS SETOF record
AS 'MODULE_PATHNAME'
LANGUAGE C IMMUTABLE STRICT;


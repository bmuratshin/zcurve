/* contrib/zcurve/zcurve--1.3.sql */

-- complain if script is sourced in psql, rather than via CREATE EXTENSION
\echo Use "CREATE EXTENSION zcurve" to load this file. \quit


CREATE DOMAIN zcurve AS pg_catalog.oid;


CREATE FUNCTION zcurve_val_from_xy(integer, integer)
RETURNS bigint
AS 'MODULE_PATHNAME'
LANGUAGE C IMMUTABLE STRICT;

CREATE FUNCTION zcurve_num_from_xy(integer, integer)
RETURNS numeric
AS 'MODULE_PATHNAME'
LANGUAGE C IMMUTABLE STRICT;

CREATE FUNCTION zcurve_num_from_xyz(integer, integer, integer)
RETURNS numeric
AS 'MODULE_PATHNAME'
LANGUAGE C IMMUTABLE STRICT;

CREATE TYPE __ret_2d_lookup AS (c_tid TID, x integer, y integer);
CREATE FUNCTION zcurve_2d_lookup(text, integer, integer, integer, integer)
RETURNS SETOF __ret_2d_lookup
AS 'MODULE_PATHNAME'
LANGUAGE C IMMUTABLE STRICT;

CREATE TYPE __ret_3d_lookup AS (c_tid TID, x integer, y integer, z integer);
CREATE FUNCTION zcurve_3d_lookup(text, integer, integer, integer, integer, integer, integer)
RETURNS SETOF __ret_3d_lookup
AS 'MODULE_PATHNAME'
LANGUAGE C IMMUTABLE STRICT;

CREATE FUNCTION zcurve_2d_lookup_tidonly(text, integer, integer, integer, integer)
RETURNS SETOF TID
AS 'MODULE_PATHNAME'
LANGUAGE C IMMUTABLE STRICT;

CREATE FUNCTION zcurve_3d_lookup_tidonly(text, integer, integer, integer, integer, integer, integer)
RETURNS SETOF TID
AS 'MODULE_PATHNAME'
LANGUAGE C IMMUTABLE STRICT;

CREATE FUNCTION hilbert_num_from_xy(integer, integer)
RETURNS numeric
AS 'MODULE_PATHNAME'
LANGUAGE C IMMUTABLE STRICT;

CREATE FUNCTION hilbert_num_from_xyz(integer, integer, integer)
RETURNS numeric
AS 'MODULE_PATHNAME'
LANGUAGE C IMMUTABLE STRICT;

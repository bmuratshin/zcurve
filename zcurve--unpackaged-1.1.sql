/* contrib/zcurve/zcurve--unpackaged--1.0.sql */

-- complain if script is sourced in psql, rather than via CREATE EXTENSION
\echo Use "CREATE EXTENSION zcurve FROM unpackaged" to load this file. \quit

ALTER EXTENSION zcurve ADD domain zcurve;
ALTER EXTENSION zcurve ADD function zcurve_val_from_xy(bigint, bigint);
ALTER EXTENSION zcurve ADD function zcurve_oids_by_extent(bigint, bigint, bigint, bigint);
ALTER EXTENSION zcurve ADD function zcurve_oids_by_extent_ii(bigint, bigint, bigint, bigint);
ALTER EXTENSION zcurve ADD FUNCTION zcurve_2d_lookup(text, bigint, bigint, bigint, bigint);

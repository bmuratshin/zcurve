/* contrib/zcurve/zcurve--unpackaged--1.1.sql */

-- complain if script is sourced in psql, rather than via CREATE EXTENSION
\echo Use "CREATE EXTENSION zcurve FROM unpackaged" to load this file. \quit

ALTER EXTENSION zcurve ADD domain zcurve;
ALTER EXTENSION zcurve ADD function zcurve_val_from_xy(integer, integer);
ALTER EXTENSION zcurve ADD function zcurve_num_from_xy(integer, integer);
ALTER EXTENSION zcurve ADD function zcurve_num_from_xyz(integer, integer, integer);
ALTER EXTENSION zcurve ADD FUNCTION zcurve_2d_lookup(text, integer, integer, integer, integer);

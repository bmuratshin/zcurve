# contrib/zcurve/Makefile

MODULE_big = zcurve

OBJS = zcurve.o sp_tree.o bitkey.o list_sort.o sp_query.o hilbert2.o $(WIN32RES)

EXTENSION = zcurve
DATA = zcurve--1.4.sql zcurve--unpackaged-1.4.sql
PGFILEDESC = "zcurve - bit interleaving stuff"

ifdef USE_PGXS
PG_CONFIG = pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)
else
subdir = contrib/zcurve
top_builddir = ../..
include $(top_builddir)/src/Makefile.global
include $(top_srcdir)/contrib/contrib-global.mk
endif

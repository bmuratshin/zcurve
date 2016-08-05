/*
 *	PostgreSQL definitions for managed Large Objects.
 *
 *	contrib/zcurve/zcurve.c
 *
 */

#include "postgres.h"
#include "catalog/pg_type.h"
#include "fmgr.h"
#include <string.h>
#include "executor/spi.h"
#include "utils/builtins.h"

PG_MODULE_MAGIC;

uint64 zcurve_fromXY (uint32 ix, uint32 iy);
void zcurve_toXY (uint64 al, uint32 *px, uint32 *py);

static uint32 stoBits[8] = {0x0001, 0x0002, 0x0004, 0x0008, 
                  0x0010, 0x0020, 0x0040, 0x0080};
uint64
zcurve_fromXY (uint32 ix, uint32 iy)
{
  uint64 val = 0;
  int curmask = 0xf;
  unsigned char *ptr = (unsigned char *)&val;
  int i;
  for (i = 0; i < 8; i++)
    {
      int xp = (ix & curmask) >> (i<<2);
      int yp = (iy & curmask) >> (i<<2);
      int tmp = (xp & stoBits[0]) | ((yp & stoBits[0])<<1) |
            ((xp & stoBits[1])<<1) | ((yp & stoBits[1])<<2) |
            ((xp & stoBits[2])<<2) | ((yp & stoBits[2])<<3) |
            ((xp & stoBits[3])<<3) | ((yp & stoBits[3])<<4);
      curmask <<= 4;
      ptr[i] = (unsigned char)tmp;
    }
  return val;
}

void 
zcurve_toXY (uint64 al, uint32 *px, uint32 *py)
{
  unsigned char *ptr = (unsigned char *)&al;
  int ix = 0;
  int iy = 0;
  int i;

  if (!px || !py)
    return;

  for (i = 0; i < 8; i++)
    {
      int tmp = ptr[i];
      int tmpx = (tmp & stoBits[0]) + ((tmp & stoBits[2])>>1) + ((tmp & stoBits[4])>>2) + ((tmp & stoBits[6])>>3);
      int tmpy = ((tmp & stoBits[1])>>1) + ((tmp & stoBits[3])>>2) + ((tmp & stoBits[5])>>3) + ((tmp & stoBits[7])>>4);
      ix |= tmpx << (i << 2);
      iy |= tmpy << (i << 2);
    }        
  *px = ix;
  *py = iy;
}

PG_FUNCTION_INFO_V1(zcurve_val_from_xy);

Datum
zcurve_val_from_xy(PG_FUNCTION_ARGS)
{
   uint64 v1  = PG_GETARG_INT64(0);
   uint64 v2  = PG_GETARG_INT64(1);

   PG_RETURN_INT64(zcurve_fromXY(v1, v2));
}


PG_FUNCTION_INFO_V1(zcurve_num_from_xy);

Datum
zcurve_num_from_xy(PG_FUNCTION_ARGS)
{
   uint64 v1  = PG_GETARG_INT64(0);
   uint64 v2  = PG_GETARG_INT64(1);
   uint64 res  = zcurve_fromXY(v1, v2);
   Datum result = DirectFunctionCall1(int8_numeric, res);
   return result;
}


static const int s_maxx = 1000000;
static const int s_maxy = 1000000;

#ifndef MIN
#define MIN(a,b) ((a)<(b)?(a):(b))
#endif


static int compare_uint64( const void *arg1, const void *arg2 )
{
  const uint64 *a = (const uint64 *)arg1;
  const uint64 *b = (const uint64 *)arg2;
  if (*a == *b)
    return 0;
  return *a > *b ? 1: -1;
}

static SPIPlanPtr
prep_interval_request()
{
	SPIPlanPtr	pplan;
	char		sql[8192];
	int 		nkeys = 2;
	Oid		argtypes[2] = {INT8OID, INT8OID};	/* key types to prepare execution plan */
	int ret =0;

   	if ((ret = SPI_connect()) < 0)
   	/* internal error */
     	  elog(ERROR, "check_primary_key: SPI_connect returned %d", ret);

	snprintf(sql, sizeof(sql), "select * from test_points where zcurve_val_from_xy(x, y) between $1 and $2");

	/* Prepare plan for query */
	pplan = SPI_prepare(sql, nkeys, argtypes);
	if (pplan == NULL)
	/* internal error */
		elog(ERROR, "check_primary_key: SPI_prepare returned %d", SPI_result);
	return pplan;
}

static int 
fin_interval_request(SPIPlanPtr pplan)
{
   SPI_finish();
   return 0;
}


static int 
run_interval_request(SPIPlanPtr pplan, uint64 v0, uint64 v1)
{
	Datum		values[2];	/* key types to prepare execution plan */
	Portal 		portal;
	int cnt = 0, i;

	values[0] = Int64GetDatum(v0);
	values[1] = Int64GetDatum(v1);

	portal = SPI_cursor_open(NULL, pplan, values, NULL, true);
	if (NULL == portal)
		/* internal error */
		elog(ERROR, "check_primary_key: SPI_cursor_open");
	for (;;)
	{
		SPI_cursor_fetch(portal, true, 8);
		if (0 == SPI_processed || NULL == SPI_tuptable)
			break;
		{
			/*TupleDesc tupdesc = SPI_tuptable->tupdesc;*/
			for (i = 0; i < SPI_processed; i++)
			{
				/*HeapTuple tuple = SPI_tuptable->vals[i];*/
				/*elog(INFO, "%s, %s", SPI_getvalue(tuple, tupdesc, 1), SPI_getvalue(tuple, tupdesc, 2));*/
				cnt++;
			}
		}
	}
	SPI_cursor_close(portal);

	return cnt;
}

PG_FUNCTION_INFO_V1(zcurve_oids_by_extent);
Datum
zcurve_oids_by_extent(PG_FUNCTION_ARGS)
{
   SPIPlanPtr	pplan;

   uint64 x0  = PG_GETARG_INT64(0);
   uint64 y0  = PG_GETARG_INT64(1);
   uint64 x1  = PG_GETARG_INT64(2);
   uint64 y1  = PG_GETARG_INT64(3);
   uint64 *ids = NULL;
   int cnt = 0;
   int sz = 0, ix, iy;

   x0 = MIN(x0, s_maxx);
   y0 = MIN(y0, s_maxy);
   x1 = MIN(x1, s_maxx);
   y1 = MIN(y1, s_maxy);

   if (x0 > x1)
     elog(ERROR, "xmin > xmax");
   if (y0 > y1)
     elog(ERROR, "ymin > ymax");

   sz = (x1 - x0 + 1) * (y1 - y0 + 1);
   ids = (uint64*)palloc(sz * sizeof(uint64));
   if (NULL == ids)
   /* internal error */
     elog(ERROR, "cant alloc %d bytes in zcurve_oids_by_extent", sz);
   for (ix = x0; ix <= x1; ix++)
    for (iy = y0; iy <= y1; iy++)
    {
     	ids[cnt++] = zcurve_fromXY(ix, iy);
    }
   qsort (ids, sz, sizeof(*ids), compare_uint64);

   cnt = 0;

   pplan = prep_interval_request();
   {
/*	FILE *fl = fopen("/tmp/ttt.sql", "w");*/
	int cur_start = 0; 
	int ix;
	for (ix = cur_start + 1; ix < sz; ix++)
	{
		if (ids[ix] != ids[ix - 1] + 1)
		{
			cnt += run_interval_request(pplan, ids[cur_start], ids[ix - 1]);
/*			fprintf(fl, "EXPLAIN (ANALYZE,BUFFERS) select * from test_points where zcurve_val_from_xy(x, y) between %ld and %ld;\n", ids[cur_start], ids[ix - 1]);
			elog(INFO, "%d -> %d (%ld -> %ld)", cur_start, ix - 1, ids[cur_start], ids[ix - 1]);
			cnt++;*/
			cur_start = ix;
		}
	}
    	if (cur_start != ix)
	{
			cnt += run_interval_request(pplan, ids[cur_start], ids[ix - 1]);
/*			fprintf(fl, "EXPLAIN (ANALYZE,BUFFERS) select * from test_points where zcurve_val_from_xy(x, y) between %ld and %ld;\n", ids[cur_start], ids[ix - 1]);
			elog(INFO, "%d -> %d (%ld -> %ld)", cur_start, ix - 1, ids[cur_start], ids[ix - 1]);*/
	}
/*	fclose(fl);*/
   }
   fin_interval_request(pplan);
   pfree(ids);

   PG_RETURN_INT64(cnt);
}

/*------------------------------------------------------------------------------------------------ */

struct interval_ctx_s {
	SPIPlanPtr	cr_;
	SPIPlanPtr	probe_cr_;
	uint64 		cur_val_;
	uint64 		top_val_;
	FILE * 		fl_;
};
typedef struct interval_ctx_s interval_ctx_t;

int prep_interval_request_ii(interval_ctx_t *ctx);
int run_interval_request_ii(interval_ctx_t *ctx, uint64 v0, uint64 v1);
int probe_interval_request_ii(interval_ctx_t *ctx, uint64 v0);
int fin_interval_request_ii(interval_ctx_t *ctx);


int prep_interval_request_ii(interval_ctx_t *ctx)
{
	char		sql[8192];
	int 		nkeys = 2;
	Oid		argtypes[2] = {INT8OID, INT8OID};	/* key types to prepare execution plan */
	int ret =0;

   	if ((ret = SPI_connect()) < 0)
   	/* internal error */
     	  elog(ERROR, "check_primary_key: SPI_connect returned %d", ret);

	snprintf(sql, sizeof(sql), "select * from test_points where zcurve_val_from_xy(x, y) between $1 and $2");
	ctx->cr_ = SPI_prepare(sql, nkeys, argtypes);
	if (ctx->cr_ == NULL)
	/* internal error */
		elog(ERROR, "check_primary_key: SPI_prepare returned %d", SPI_result);

	snprintf(sql, sizeof(sql), "select * from test_points where zcurve_val_from_xy(x, y) between $1 and %ld order by zcurve_val_from_xy(x::int4, y::int4) limit 1", ctx->top_val_);
	ctx->probe_cr_ = SPI_prepare(sql, 1, argtypes);
	if (ctx->probe_cr_ == NULL)
	/* internal error */
		elog(ERROR, "check_primary_key: SPI_prepare returned %d", SPI_result);

	return 1;
}

int 
probe_interval_request_ii(interval_ctx_t *ctx, uint64 v0)
{
	Datum		values[1];	/* key types to prepare execution plan */
	Portal 		portal;

	values[0] = Int64GetDatum(v0);

	if (ctx->fl_)
		fprintf(ctx->fl_, "EXPLAIN (ANALYZE,BUFFERS) select * from test_points where zcurve_val_from_xy(x, y) between %ld and %ld order by zcurve_val_from_xy(x::int4, y::int4) limit 1;\n", v0, ctx->top_val_);
	portal = SPI_cursor_open(NULL, ctx->probe_cr_, values, NULL, true);
	if (NULL == portal)
		/* internal error */
		elog(ERROR, "check_primary_key: SPI_cursor_open");
	{
		SPI_cursor_fetch(portal, true, 1);
		if (0 != SPI_processed && NULL != SPI_tuptable)
		{
			TupleDesc tupdesc = SPI_tuptable->tupdesc;

			bool isnull;
			HeapTuple tuple = SPI_tuptable->vals[0];
			Datum dx, dy;
			uint64 zv = 0;
			dx = SPI_getbinval(tuple, tupdesc, 1, &isnull);
			dy = SPI_getbinval(tuple, tupdesc, 2, &isnull);
			zv = zcurve_fromXY(DatumGetInt64(dx), DatumGetInt64(dy));
/*			elog(INFO, "%ld %ld -> %ld", DatumGetInt64(dx), DatumGetInt64(dy), zv); */

			ctx->cur_val_ = zv;
			SPI_cursor_close(portal);
			return 1;
		}
		SPI_cursor_close(portal);
	}
	return 0;
}


int 
run_interval_request_ii(interval_ctx_t *ctx, uint64 v0, uint64 v1)
{
	Datum		values[2];	/* key types to prepare execution plan */
	Portal 		portal;
	int cnt = 0, i;

	values[0] = Int64GetDatum(v0);
	values[1] = Int64GetDatum(v1);

	if (ctx->fl_)
		fprintf(ctx->fl_, "EXPLAIN (ANALYZE,BUFFERS) select * from test_points where zcurve_val_from_xy(x, y) between %ld and %ld;\n", v0, v1);
	portal = SPI_cursor_open(NULL, ctx->cr_, values, NULL, true);
	if (NULL == portal)
		/* internal error */
		elog(ERROR, "check_primary_key: SPI_cursor_open");
	for (;;)
	{
		SPI_cursor_fetch(portal, true, 8);
		if (0 == SPI_processed || NULL == SPI_tuptable)
			break;
		{
			/*TupleDesc tupdesc = SPI_tuptable->tupdesc;*/
			for (i = 0; i < SPI_processed; i++)
			{
				/*HeapTuple tuple = SPI_tuptable->vals[i];*/
				/*elog(INFO, "%s, %s", SPI_getvalue(tuple, tupdesc, 1), SPI_getvalue(tuple, tupdesc, 2)); */
				cnt++;
			}
		}
	}
	SPI_cursor_close(portal);

	return cnt;
}


PG_FUNCTION_INFO_V1(zcurve_oids_by_extent_ii);
Datum
zcurve_oids_by_extent_ii(PG_FUNCTION_ARGS)
{
   uint64 x0  = PG_GETARG_INT64(0);
   uint64 y0  = PG_GETARG_INT64(1);
   uint64 x1  = PG_GETARG_INT64(2);
   uint64 y1  = PG_GETARG_INT64(3);
   uint64 *ids = NULL;
   int cnt = 0;
   int sz = 0, ix, iy;
   interval_ctx_t ctx;

   x0 = MIN(x0, s_maxx);
   y0 = MIN(y0, s_maxy);
   x1 = MIN(x1, s_maxx);
   y1 = MIN(y1, s_maxy);

   if (x0 > x1)
     elog(ERROR, "xmin > xmax");
   if (y0 > y1)
     elog(ERROR, "ymin > ymax");

   sz = (x1 - x0 + 1) * (y1 - y0 + 1);
   ids = (uint64*)palloc(sz * sizeof(uint64));
   if (NULL == ids)
   /* internal error */
     elog(ERROR, "can't alloc %d bytes in zcurve_oids_by_extent_ii", sz);
   for (ix = x0; ix <= x1; ix++)
     for (iy = y0; iy <= y1; iy++)
     {
     	ids[cnt++] = zcurve_fromXY(ix, iy);
     }
   qsort (ids, sz, sizeof(*ids), compare_uint64);

   ctx.top_val_ = ids[sz - 1];
   ctx.cur_val_ = 0;
   ctx.cr_ = NULL;
   ctx.probe_cr_ = NULL;
   ctx.fl_ = NULL;/*fopen("/tmp/ttt.sql", "w"); */
   
   cnt = 0;

   prep_interval_request_ii(&ctx);
   {
	int cur_start = 0; 
	int ix;
	for (ix = cur_start + 1; ix < sz; ix++)
	{
		if (0 == probe_interval_request_ii(&ctx, ids[cur_start]))
			break;
		for (; cur_start < sz && ids[cur_start] < ctx.cur_val_; cur_start++);

		ix = cur_start + 1;
                if (ix >= sz)
			break;
		for (; ix < sz && ids[ix] == ids[ix - 1] + 1; ix++);

		cnt += run_interval_request_ii(&ctx, ids[cur_start], ids[ix - 1]);
		cur_start = ix;
	}
   }
   if (ctx.fl_)
   	fclose(ctx.fl_);
   fin_interval_request(NULL);
   pfree(ids);

   PG_RETURN_INT64(cnt);
}

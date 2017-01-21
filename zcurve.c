/*
 * Copyright (c) 2016...2017, Alex Artyushin, Boris Muratshin
 * All rights reserved.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * The names of the authors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
/*
 *	PostgreSQL definitions for managed Large Objects.
 *
 *	contrib/zcurve/zcurve.c
 *
 * Author:	Boris Muratshin, mailto:bmuratshin@gmail.com
 *
 */

#include "postgres.h"
#include "catalog/pg_type.h"
#include "fmgr.h"
#include <string.h>
#include "executor/spi.h"
#include "utils/builtins.h"

#include "funcapi.h"
#include "utils/rel.h"
#include "utils/builtins.h"
#include "utils/fmgroids.h"
#include "utils/builtins.h"
#include "utils/numeric.h"
#include "utils/lsyscache.h"
#include "catalog/namespace.h"
#if PG_VERSION_NUM >= 90600
#include "catalog/pg_am.h"
#endif
#include "access/nbtree.h"
#include "access/htup_details.h"

#include "sp_tree.h"
#include "sp_query.h"
#include "gen_list.h"
#include "list_sort.h"
#include "bitkey.h"

#if PG_VERSION_NUM >= 90600
#define heap_formtuple heap_form_tuple
#endif

PG_MODULE_MAGIC;


PG_FUNCTION_INFO_V1(zcurve_val_from_xy);

Datum
zcurve_val_from_xy(PG_FUNCTION_ARGS)
{
   uint32 coords[ZKEY_MAX_COORDS] = {PG_GETARG_INT64(0), PG_GETARG_INT64(1) };
   bitKey_t key;

   bitKey_CTOR(&key, btZ2D);
   bitKey_fromCoords(&key, coords, 2);
   PG_RETURN_INT64(key.vals_[0]);
}


PG_FUNCTION_INFO_V1(zcurve_num_from_xy);

Datum
zcurve_num_from_xy(PG_FUNCTION_ARGS)
{
   uint32 coords[ZKEY_MAX_COORDS] = {PG_GETARG_INT64(0), PG_GETARG_INT64(1) };
   bitKey_t key;

   bitKey_CTOR(&key, btZ2D);
   bitKey_fromCoords(&key, coords, 2);
   return bitKey_toLong(&key);
}


PG_FUNCTION_INFO_V1(zcurve_num_from_xyz);

Datum
zcurve_num_from_xyz(PG_FUNCTION_ARGS)
{
   uint32 coords[ZKEY_MAX_COORDS] = {PG_GETARG_INT64(0), PG_GETARG_INT64(1),  PG_GETARG_INT64(2)};
   bitKey_t key;
   Datum ret;

   bitKey_CTOR(&key, btZ3D);
   bitKey_fromCoords(&key, coords, 3);
   ret = bitKey_toLong(&key);
#if 0
   bitKey_fromLong(&key, ret);
   coords[0] = coords[1] = coords[2] = 0;
   bitKey_toCoords(&key, coords, 3);
   elog(INFO, "%d %d %d", coords[0], coords[1], coords[2]);
#endif
   return ret;
}



/*
 * Open index relation with AccessShareLock.
 */
static Relation
indexOpen(RangeVar *relvar)
{
#if PG_VERSION_NUM < 90200
	Oid			relOid = RangeVarGetRelid(relvar, false);
#else
	Oid			relOid = RangeVarGetRelid(relvar, NoLock, false);
#endif
	return   index_open(relOid, AccessShareLock);
}

/*
 * Close index relation opened with AccessShareLock.
 */
static void
indexClose(Relation r)
{
	index_close((r), AccessShareLock);
}

/* resulting item temporarily storing for sorting */
typedef struct res_item_s {
	ItemPointerData iptr_;	/* pointer to rable row */
	uint32		coords_[ZKEY_MAX_COORDS]; /* x y z ... */
	gen_list_t	link_;  /* next one */
} res_item_t;

/* in the end we need to sort found index items by t_tid*/
static int  
res_item_compare_proc(const void *a, const void *b, const void *arg)
{
	const res_item_t *l = (const res_item_t *)a;
	const res_item_t *r = (const res_item_t *)b;

	if (l->iptr_.ip_blkid.bi_hi != r->iptr_.ip_blkid.bi_hi)
		return l->iptr_.ip_blkid.bi_hi - r->iptr_.ip_blkid.bi_hi;

	if (l->iptr_.ip_blkid.bi_lo != r->iptr_.ip_blkid.bi_lo)
		return l->iptr_.ip_blkid.bi_lo - r->iptr_.ip_blkid.bi_lo;

	if (l->iptr_.ip_posid != r->iptr_.ip_posid)
		return l->iptr_.ip_posid - r->iptr_.ip_posid;

	return 0;
}

/* SRF-resistent context */
typedef struct p2d_ctx_s {
	Relation    	relation_;	/* index tree */
	spt_query2_t 	qdef_;		/* spatial query definition */
	gen_list_t 	*result_;	/* head og resulting list */
	gen_list_t 	*cur_;		/* current item for out */
	int 		cnt_;		/* resulting list length */

	res_item_t	cur_item_;	/* current item in a non-sorting mode */
	int 		ret_;		/* the result of the last zcurve call */
} p2d_ctx_t;


/* constructor */
static void 
p2d_ctx_t_CTOR(p2d_ctx_t *ptr, const char *relname, const uint32 *min_coords, const uint32 *max_coords, bitkey_type ktype)
{
	List	   *relname_list;
	RangeVar   *relvar;

	Assert(ptr);

	relname_list = stringToQualifiedNameList(relname);
	relvar = makeRangeVarFromNameList(relname_list);
	ptr->relation_ = indexOpen(relvar);
	ptr->cnt_ = 0;
	ptr->result_ = NULL;
	ptr->cur_ = NULL;

	spt_query2_CTOR (&ptr->qdef_, ptr->relation_, min_coords, max_coords, ktype);
}

/* destructor */
static void 
p2d_ctx_t_DTOR(p2d_ctx_t *ptr)
{
	Assert(ptr);
	indexClose(ptr->relation_);
	ptr->relation_ = NULL;
	ptr->result_ = NULL;
	ptr->cur_ = NULL;
	spt_query2_DTOR (&ptr->qdef_);
}

static Datum
zcurve_Xd_lookup_tidonly(FunctionCallInfo fcinfo, char *relname, bitkey_type ktype, uint32 *left_bottom, uint32 *right_upper)
{
	/* SRF stuff */
	FuncCallContext     *funcctx = NULL;
	p2d_ctx_t 	    *pctx = NULL;

	/* params 
  	uint32 x0  = left_bottom[0];
	uint32 y0  = left_bottom[1];
	uint32 z0  = left_bottom[2];
	uint32 x1  = right_upper[0];
	uint32 y1  = right_upper[1];
	uint32 z1  = right_upper[2];*/
	MemoryContext   oldcontext;

	if (SRF_IS_FIRSTCALL())
	{
		uint32 coords[ZKEY_MAX_COORDS] = {0, 0, 0};
		//uint32 coords2[ZKEY_MAX_COORDS] = {x1, y1, z1};
		ItemPointerData iptr;

		funcctx = SRF_FIRSTCALL_INIT();
		funcctx->max_calls = 10000000;

		/* lets start lookup, storing intermediate data in context list */
		oldcontext = MemoryContextSwitchTo(funcctx->multi_call_memory_ctx);

		/* prepare lookup context */
		pctx = (p2d_ctx_t*)palloc(sizeof(p2d_ctx_t));
		p2d_ctx_t_CTOR(pctx, relname, left_bottom, right_upper, ktype);

		funcctx->user_fctx = pctx;
		/* performing spatial cursor forwarding */
		pctx->ret_ = spt_query2_moveFirst(&pctx->qdef_, coords, &iptr);
		if (pctx->ret_)
		{
			pctx->cur_item_.iptr_ = iptr;
			pctx->cnt_++;
		}
	}
	else
	{
		ItemPointerData iptr;
		uint32 coords[ZKEY_MAX_COORDS];

		funcctx = SRF_PERCALL_SETUP();
		oldcontext = MemoryContextSwitchTo(funcctx->multi_call_memory_ctx);

		pctx = (p2d_ctx_t *) funcctx->user_fctx;
		pctx->ret_ = spt_query2_moveNext(&pctx->qdef_, coords, &iptr);
		if (pctx->ret_)
		{
			pctx->cur_item_.iptr_ = iptr;
			pctx->cnt_++;
		}
	}

	PG_FREE_IF_COPY(relname, 0);
	pctx = (p2d_ctx_t *) funcctx->user_fctx;
	{
		/* while the end of result list is not reached */
		if (pctx->ret_)
		{
			MemoryContextSwitchTo(oldcontext);
			SRF_RETURN_NEXT(funcctx, PointerGetDatum(&pctx->cur_item_.iptr_));
		}
		else
		{
			/* no more data, free resources and stop lookup */
			p2d_ctx_t_DTOR(pctx);
		}	
	}
	pfree(pctx);
	MemoryContextSwitchTo(oldcontext);
	SRF_RETURN_DONE(funcctx);
}

PG_FUNCTION_INFO_V1(zcurve_2d_lookup);
Datum
zcurve_2d_lookup(PG_FUNCTION_ARGS)
{
	/* SRF stuff */
	FuncCallContext     *funcctx = NULL;
	TupleDesc            tupdesc;
	AttInMetadata       *attinmeta;
	p2d_ctx_t 	    *pctx = NULL;

	/* params */
	char *relname = text_to_cstring(PG_GETARG_TEXT_PP(0)); 
  	uint64 x0  = PG_GETARG_INT64(1);
	uint64 y0  = PG_GETARG_INT64(2);
	uint64 x1  = PG_GETARG_INT64(3);
	uint64 y1  = PG_GETARG_INT64(4);

	if (SRF_IS_FIRSTCALL())
	{
		MemoryContext   oldcontext;
		funcctx = SRF_FIRSTCALL_INIT();

		/* recordset cosists of 3 columns - t_tid & coordinates */
		tupdesc = CreateTemplateTupleDesc(3, false);
		TupleDescInitEntry(tupdesc, (AttrNumber) 1, "ctid", TIDOID, -1, 0);
		TupleDescInitEntry(tupdesc, (AttrNumber) 2, "x", INT4OID, -1, 0);
		TupleDescInitEntry(tupdesc, (AttrNumber) 3, "y", INT4OID, -1, 0);

		oldcontext = MemoryContextSwitchTo(funcctx->multi_call_memory_ctx);
		funcctx->max_calls = 1000000;

		if (get_call_result_type(fcinfo, NULL, &tupdesc) != TYPEFUNC_COMPOSITE)
			ereport(ERROR,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				errmsg("function returning record called in context "
				"that cannot accept type record")));

		attinmeta = TupleDescGetAttInMetadata(tupdesc);
		funcctx->attinmeta = attinmeta;
		/* lets start lookup, storing intermediate data in context list */
		{
			int 		ret;
			uint32 coords[ZKEY_MAX_COORDS] = {x0, y0};
			uint32 coords2[ZKEY_MAX_COORDS] = {x1, y1};
			ItemPointerData iptr;

			/* prepare lookup context */
			pctx = (p2d_ctx_t*)palloc(sizeof(p2d_ctx_t));
			p2d_ctx_t_CTOR(pctx, relname, coords, coords2, btZ2D);

			funcctx->user_fctx = pctx;

			/* performing spatial cursor forwarding */
			ret = spt_query2_moveFirst(&pctx->qdef_, coords, &iptr);
			while (ret)
			{
				res_item_t *pit = (res_item_t *)palloc(sizeof(res_item_t));
				pit->coords_[0] = coords[0];
				pit->coords_[1] = coords[1];
				pit->iptr_ = iptr;
				pit->link_.data = pit;
				pit->link_.next = pctx->result_;
				pctx->result_ = &pit->link_;

				pctx->cnt_++;
				ret = spt_query2_moveNext(&pctx->qdef_, coords, &iptr);
			}
			/* sort temporary data */
			pctx->result_ = list_sort (pctx->result_,  res_item_compare_proc, NULL);
			pctx->cur_ = pctx->result_;
		}
		MemoryContextSwitchTo(oldcontext);
	}

	PG_FREE_IF_COPY(relname, 0);
	funcctx = SRF_PERCALL_SETUP();
	pctx = (p2d_ctx_t *) funcctx->user_fctx;

	attinmeta = funcctx->attinmeta;

	{
		Datum		datums[3];
		bool		nulls[3];
	        HeapTuple    	htuple;
	        Datum        	result;
		/* while the end of result list is not reached */
		if (pctx->cur_)
		{
			res_item_t *pit = (res_item_t *)(pctx->cur_->data);
			datums[0] = PointerGetDatum(&pit->iptr_);
			datums[1] = Int32GetDatum(pit->coords_[0]);
			datums[2] = Int32GetDatum(pit->coords_[1]);
			pctx->cur_ = pctx->cur_->next;

			nulls[0] = false;
			nulls[1] = false;
			nulls[2] = false;

			htuple = heap_formtuple(attinmeta->tupdesc, datums, nulls);
			result = TupleGetDatum(funcctx, htuple);
			SRF_RETURN_NEXT(funcctx, result);
		}
		else
		{
			/* no more data, free resources and stop lookup */
			p2d_ctx_t_DTOR(pctx);
		}	
	}
	pfree(pctx);
	SRF_RETURN_DONE(funcctx);
}


PG_FUNCTION_INFO_V1(zcurve_3d_lookup);
Datum
zcurve_3d_lookup(PG_FUNCTION_ARGS)
{
	/* SRF stuff */
	FuncCallContext     *funcctx = NULL;
	TupleDesc            tupdesc;
	AttInMetadata       *attinmeta;
	p2d_ctx_t 	    *pctx = NULL;

	/* params */
	char *relname = text_to_cstring(PG_GETARG_TEXT_PP(0)); 
  	uint64 x0  = PG_GETARG_INT64(1);
	uint64 y0  = PG_GETARG_INT64(2);
	uint64 z0  = PG_GETARG_INT64(3);
	uint64 x1  = PG_GETARG_INT64(4);
	uint64 y1  = PG_GETARG_INT64(5);
	uint64 z1  = PG_GETARG_INT64(6);

	if (SRF_IS_FIRSTCALL())
	{
		MemoryContext   oldcontext;
		funcctx = SRF_FIRSTCALL_INIT();

		/* recordset cosists of 3 columns - t_tid & coordinates */
		tupdesc = CreateTemplateTupleDesc(4, false);
		TupleDescInitEntry(tupdesc, (AttrNumber) 1, "ctid", TIDOID, -1, 0);
		TupleDescInitEntry(tupdesc, (AttrNumber) 2, "x", INT4OID, -1, 0);
		TupleDescInitEntry(tupdesc, (AttrNumber) 3, "y", INT4OID, -1, 0);
		TupleDescInitEntry(tupdesc, (AttrNumber) 4, "z", INT4OID, -1, 0);

		oldcontext = MemoryContextSwitchTo(funcctx->multi_call_memory_ctx);
		funcctx->max_calls = 1000000;

		if (get_call_result_type(fcinfo, NULL, &tupdesc) != TYPEFUNC_COMPOSITE)
			ereport(ERROR,
				(errcode(ERRCODE_FEATURE_NOT_SUPPORTED),
				errmsg("function returning record called in context "
				"that cannot accept type record")));

		attinmeta = TupleDescGetAttInMetadata(tupdesc);
		funcctx->attinmeta = attinmeta;
		/* lets start lookup, storing intermediate data in context list */
		{
			int 		ret;
			uint32 coords[ZKEY_MAX_COORDS] = {x0, y0, z0};
			uint32 coords2[ZKEY_MAX_COORDS] = {x1, y1, z1};
			ItemPointerData iptr;

			/* prepare lookup context */
			pctx = (p2d_ctx_t*)palloc(sizeof(p2d_ctx_t));
			p2d_ctx_t_CTOR(pctx, relname, coords, coords2, btZ3D);

			funcctx->user_fctx = pctx;

			/* performing spatial cursor forwarding */
			ret = spt_query2_moveFirst(&pctx->qdef_, coords, &iptr);
			while (ret)
			{
				res_item_t *pit = (res_item_t *)palloc(sizeof(res_item_t));
				pit->coords_[0] = coords[0];
				pit->coords_[1] = coords[1];
				pit->coords_[2] = coords[2];
				pit->iptr_ = iptr;
				pit->link_.data = pit;
				pit->link_.next = pctx->result_;
				pctx->result_ = &pit->link_;

				pctx->cnt_++;
				ret = spt_query2_moveNext(&pctx->qdef_, coords, &iptr);
			}
			/* sort temporary data */
			pctx->result_ = list_sort (pctx->result_,  res_item_compare_proc, NULL);
			pctx->cur_ = pctx->result_;
		}
		MemoryContextSwitchTo(oldcontext);
	}

	PG_FREE_IF_COPY(relname, 0);
	funcctx = SRF_PERCALL_SETUP();
	pctx = (p2d_ctx_t *) funcctx->user_fctx;

	attinmeta = funcctx->attinmeta;

	{
		Datum		datums[4];
		bool		nulls[4];
	        HeapTuple    	htuple;
	        Datum        	result;
		/* while the end of result list is not reached */
		if (pctx->cur_)
		{
			res_item_t *pit = (res_item_t *)(pctx->cur_->data);
			datums[0] = PointerGetDatum(&pit->iptr_);
			datums[1] = Int32GetDatum(pit->coords_[0]);
			datums[2] = Int32GetDatum(pit->coords_[1]);
			datums[3] = Int32GetDatum(pit->coords_[2]);
			pctx->cur_ = pctx->cur_->next;

			nulls[0] = false;
			nulls[1] = false;
			nulls[2] = false;
			nulls[3] = false;

			htuple = heap_formtuple(attinmeta->tupdesc, datums, nulls);
			result = TupleGetDatum(funcctx, htuple);
			SRF_RETURN_NEXT(funcctx, result);
		}
		else
		{
			/* no more data, free resources and stop lookup */
			p2d_ctx_t_DTOR(pctx);
		}	
	}
	pfree(pctx);
	SRF_RETURN_DONE(funcctx);
}

PG_FUNCTION_INFO_V1(zcurve_2d_lookup_tidonly);
Datum
zcurve_2d_lookup_tidonly(PG_FUNCTION_ARGS)
{
	/* params */
	char *relname = text_to_cstring(PG_GETARG_TEXT_PP(0)); 
  	uint64 x0  = PG_GETARG_INT64(1);
	uint64 y0  = PG_GETARG_INT64(2);
	uint64 x1  = PG_GETARG_INT64(3);
	uint64 y1  = PG_GETARG_INT64(4);
	uint32 coords[ZKEY_MAX_COORDS] = {x0, y0};
	uint32 coords2[ZKEY_MAX_COORDS] = {x1, y1};

	return zcurve_Xd_lookup_tidonly(fcinfo, relname, btZ2D, coords, coords2);
}

PG_FUNCTION_INFO_V1(zcurve_3d_lookup_tidonly);
Datum
zcurve_3d_lookup_tidonly(PG_FUNCTION_ARGS)
{
	/* params */
	char *relname = text_to_cstring(PG_GETARG_TEXT_PP(0)); 
  	uint64 x0  = PG_GETARG_INT64(1);
	uint64 y0  = PG_GETARG_INT64(2);
	uint64 z0  = PG_GETARG_INT64(3);
	uint64 x1  = PG_GETARG_INT64(4);
	uint64 y1  = PG_GETARG_INT64(5);
	uint64 z1  = PG_GETARG_INT64(6);
	uint32 coords[ZKEY_MAX_COORDS] = {x0, y0, z0};
	uint32 coords2[ZKEY_MAX_COORDS] = {x1, y1, z1};

	return zcurve_Xd_lookup_tidonly(fcinfo, relname, btZ3D, coords, coords2);
}


PG_FUNCTION_INFO_V1(hilbert_num_from_xy);

Datum
hilbert_num_from_xy(PG_FUNCTION_ARGS)
{
   uint32 coords[ZKEY_MAX_COORDS] = {PG_GETARG_INT64(0), PG_GETARG_INT64(1) };
   bitKey_t key;

   bitKey_CTOR(&key, btHilb2D);
   bitKey_fromCoords(&key, coords, 2);
   return bitKey_toLong(&key);
}


PG_FUNCTION_INFO_V1(hilbert_num_from_xyz);

Datum
hilbert_num_from_xyz(PG_FUNCTION_ARGS)
{
   uint32 coords[ZKEY_MAX_COORDS] = {PG_GETARG_INT64(0), PG_GETARG_INT64(1),  PG_GETARG_INT64(2)};
   bitKey_t key;
   Datum ret;

   bitKey_CTOR(&key, btHilb3D);
   bitKey_fromCoords(&key, coords, 3);
   ret = bitKey_toLong(&key);
#if 0
   bitKey_fromLong(&key, ret);
   coords[0] = coords[1] = coords[2] = 0;
   bitKey_toCoords(&key, coords, 3);
   elog(INFO, "%d %d %d", coords[0], coords[1], coords[2]);
#endif
   return ret;
}


PG_FUNCTION_INFO_V1(hilbert_3d_lookup_tidonly);
Datum
hilbert_3d_lookup_tidonly(PG_FUNCTION_ARGS)
{
	/* params */
	char *relname = text_to_cstring(PG_GETARG_TEXT_PP(0)); 
  	uint64 x0  = PG_GETARG_INT64(1);
	uint64 y0  = PG_GETARG_INT64(2);
	uint64 z0  = PG_GETARG_INT64(3);
	uint64 x1  = PG_GETARG_INT64(4);
	uint64 y1  = PG_GETARG_INT64(5);
	uint64 z1  = PG_GETARG_INT64(6);
	uint32 coords[ZKEY_MAX_COORDS] = {x0, y0, z0};
	uint32 coords2[ZKEY_MAX_COORDS] = {x1, y1, z1};

	return zcurve_Xd_lookup_tidonly(fcinfo, relname, btHilb3D, coords, coords2);
}

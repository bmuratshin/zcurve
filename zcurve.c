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

#include "funcapi.h"
#include "utils/rel.h"
#include "utils/builtins.h"
#include "utils/fmgroids.h"
#include "utils/builtins.h"
#include "utils/numeric.h"
#include "utils/lsyscache.h"
#include "catalog/namespace.h"
#include "access/nbtree.h"
#include "access/htup_details.h"

#include "sp_tree_2d.h"
#include "sp_query_2d.h"
#include "gen_list.h"
#include "list_sort.h"
#include "bitkey.h"


PG_MODULE_MAGIC;


PG_FUNCTION_INFO_V1(zcurve_val_from_xy);

Datum
zcurve_val_from_xy(PG_FUNCTION_ARGS)
{
   uint32 coords[ZKEY_MAX_COORDS] = {PG_GETARG_INT64(0), PG_GETARG_INT64(1) };
   bitKey_t key;

   bitKey_CTOR(&key, 2);
   bitKey_fromCoords(&key, coords, 2);
   PG_RETURN_INT64(key.vals_[0]);
}


PG_FUNCTION_INFO_V1(zcurve_num_from_xy);

Datum
zcurve_num_from_xy(PG_FUNCTION_ARGS)
{
   uint32 coords[ZKEY_MAX_COORDS] = {PG_GETARG_INT64(0), PG_GETARG_INT64(1) };
   bitKey_t key;

   bitKey_CTOR(&key, 2);
   bitKey_fromCoords(&key, coords, 2);
   return bitKey_toLong(&key);
}

/*
 * Check if relation is index and has specified am oid. Trigger error if not
 */
static Relation
checkOpenedRelation(Relation r, Oid PgAmOid)
{
	if (r->rd_am == NULL)
		elog(ERROR, "Relation %s.%s is not an index",
			 get_namespace_name(RelationGetNamespace(r)),
			 RelationGetRelationName(r));
	if (r->rd_rel->relam != PgAmOid)
		elog(ERROR, "Index %s.%s has wrong type",
			 get_namespace_name(RelationGetNamespace(r)),
			 RelationGetRelationName(r));
	return r;
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
	return checkOpenedRelation(
						   index_open(relOid, AccessShareLock), BTREE_AM_OID);
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
	uint32		x_;	/* x coord */
	uint32		y_;	/* y coord */
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
} p2d_ctx_t;


/* constructor */
static void 
p2d_ctx_t_CTOR(p2d_ctx_t *ptr, const char *relname, uint32 x0, uint32 y0, uint32 x1, uint64 y1)
{
	uint32 min_coords[ZKEY_MAX_COORDS] = {x0, y0};
	uint32 max_coords[ZKEY_MAX_COORDS] = {x1, y1};

	List	   *relname_list;
	RangeVar   *relvar;

	Assert(ptr);

	relname_list = stringToQualifiedNameList(relname);
	relvar = makeRangeVarFromNameList(relname_list);
	ptr->relation_ = indexOpen(relvar);
	ptr->cnt_ = 0;
	ptr->result_ = NULL;
	ptr->cur_ = NULL;

	spt_query2_CTOR (&ptr->qdef_, ptr->relation_, min_coords, max_coords, 2);
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
			uint32 coords[ZKEY_MAX_COORDS];
			ItemPointerData iptr;

			/* prepare lookup context */
			pctx = (p2d_ctx_t*)palloc(sizeof(p2d_ctx_t));
			p2d_ctx_t_CTOR(pctx, relname, x0, y0, x1, y1);

			funcctx->user_fctx = pctx;

			/* performing spatial cursor forwarding */
			ret = spt_query2_moveFirst(&pctx->qdef_, coords, &iptr);
			while (ret)
			{
				res_item_t *pit = (res_item_t *)palloc(sizeof(res_item_t));
				pit->x_ = coords[0];
				pit->y_ = coords[1];
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
			datums[1] = Int32GetDatum(pit->x_);
			datums[2] = Int32GetDatum(pit->y_);
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


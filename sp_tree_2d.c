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
#include "utils/rel.h"
#include "utils/builtins.h"
#include "utils/lsyscache.h"
#include "utils/fmgroids.h"
#include "catalog/namespace.h"
#include "access/nbtree.h"

extern uint64 zcurve_fromXY (uint32 ix, uint32 iy);
extern void zcurve_toXY (uint64 al, uint32 *px, uint32 *py);



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

int trace_tree_by_val(Relation relation, uint64 zv);

int 
trace_tree_by_val(Relation rel, uint64 zv)
{
	ScanKeyData skey;
	BTStack	    pstack = NULL, stack = NULL;
	bool nextkey = 0;
	Buffer buf;
	int ilevel;

	ScanKeyInit(
			&skey,
			1, // key part idx
			BTEqualStrategyNumber, 
			F_INT8GE,
			Int64GetDatum(zv));

	//buf = _bt_getroot(rel, AccessShareLock);
	//elog(INFO, "%p(%d)", buf, sizeof(buf));
	//_bt_relbuf(rel, buf);

	pstack = _bt_search(rel, 1, &skey, nextkey,  &buf, BT_READ);
	_bt_relbuf(rel, buf);


	ilevel = 0;
	stack = pstack;
	for (;;)
	{
		Datum		values[INDEX_MAX_KEYS];
		bool		isnull[INDEX_MAX_KEYS];

		if (stack == NULL)
			break;
		elog(INFO, "level(%d) blkno(%d) offset(%d)", ilevel, stack->bts_blkno, stack->bts_offset);


		index_deform_tuple(&stack->bts_btentry, RelationGetDescr(rel),
		   values, isnull);
		elog(INFO, "val(%ld) val(%ld) val(%ld) val(%ld) val(%ld) val(%ld)", 
			DatumGetInt64(values[0]), DatumGetInt64(values[1]),
			DatumGetInt64(values[2]), DatumGetInt64(values[3]),
			DatumGetInt64(values[4]), DatumGetInt64(values[5]));


		stack = stack->bts_parent;
		ilevel++;
	}

	_bt_freestack(pstack);

  	return 0;
}


PG_FUNCTION_INFO_V1(zcurve_2d_lookup);
Datum
zcurve_2d_lookup(PG_FUNCTION_ARGS)
{
	char *relname = text_to_cstring(PG_GETARG_TEXT_PP(0)); 
  	uint64 x0  = PG_GETARG_INT64(1);
	uint64 y0  = PG_GETARG_INT64(2);
	uint64 x1  = PG_GETARG_INT64(3);
	uint64 y1  = PG_GETARG_INT64(4);
	uint64 start_zv = zcurve_fromXY(x0, y0);
	List	   *relname_list;
	RangeVar   *relvar;
	Relation    relation;

	relname_list = stringToQualifiedNameList(relname);
	relvar = makeRangeVarFromNameList(relname_list);
	relation = indexOpen(relvar);

	elog(INFO, "%s(%ld)", relname, start_zv);
	trace_tree_by_val(relation, start_zv);

	indexClose(relation);

	PG_FREE_IF_COPY(relname, 0);
   	PG_RETURN_INT64(11);
}

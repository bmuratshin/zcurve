/*
 *	PostgreSQL definitions for managed Large Objects.
 *
 *	contrib/zcurve/zcurve.c
 *
 */

#include <string.h>
#include "postgres.h"
#include "catalog/pg_type.h"
#include "fmgr.h"
#include "utils/rel.h"
#include "utils/builtins.h"
#include "utils/lsyscache.h"
#include "utils/fmgroids.h"
#include "catalog/namespace.h"
#include "access/nbtree.h"
#include "storage/bufpage.h"

#include "sp_tree_2d.h"
#include "spatialIndex_2d.h"

int32
zcurve_compare(Relation rel,
			int keysz,
			ScanKey scankey,
			Page page,
			OffsetNumber offnum)
{
	TupleDesc	itupdesc = RelationGetDescr(rel);
	BTPageOpaque opaque = (BTPageOpaque) PageGetSpecialPointer(page);
	IndexTuple	itup;
	int		i;
	uint64		cmpval;
	cmpval = DatumGetInt64(scankey->sk_argument);
	/*
	 * Force result ">" if target item is first data item on an internal page
	 * --- see NOTE above.
	 */
	if (!P_ISLEAF(opaque) && offnum == P_FIRSTDATAKEY(opaque))
		return 1;

	itup = (IndexTuple) PageGetItem(page, PageGetItemId(page, offnum));

	/*
	 * The scan key is set up with the attribute number associated with each
	 * term in the key.  It is important that, if the index is multi-key, the
	 * scan contain the first k key attributes, and that they be in order.  If
	 * you think about how multi-key ordering works, you'll understand why
	 * this is.
	 *
	 * We don't test for violation of this condition here, however.  The
	 * initial setup for the index scan had better have gotten it right (see
	 * _bt_first).
	 */

	for (i = 1; i <= keysz; i++)
	{
		Datum		datum;
		bool		isNull;
		uint64		keyval;

		datum = index_getattr(itup, i, itupdesc, &isNull);
		keyval = DatumGetInt64(datum);
		if (keyval != cmpval)
		{
			return keyval > cmpval ? -1 : 1;
		}
	}
	return 0;
}

/*
 *	_bt_binsrch() -- Do a binary search for a key on a particular page.
 *
 * The passed scankey must be an insertion-type scankey (see nbtree/README),
 * but it can omit the rightmost column(s) of the index.
 *
 * When nextkey is false (the usual case), we are looking for the first
 * item >= scankey.  When nextkey is true, we are looking for the first
 * item strictly greater than scankey.
 *
 * On a leaf page, _bt_binsrch() returns the OffsetNumber of the first
 * key >= given scankey, or > scankey if nextkey is true.  (NOTE: in
 * particular, this means it is possible to return a value 1 greater than the
 * number of keys on the page, if the scankey is > all keys on the page.)
 *
 * On an internal (non-leaf) page, _bt_binsrch() returns the OffsetNumber
 * of the last key < given scankey, or last key <= given scankey if nextkey
 * is true.  (Since _bt_compare treats the first data key of such a page as
 * minus infinity, there will be at least one key < scankey, so the result
 * always points at one of the keys on the page.)  This key indicates the
 * right place to descend to be sure we find all leaf keys >= given scankey
 * (or leaf keys > given scankey when nextkey is true).
 *
 * This procedure is not responsible for walking right, it just examines
 * the given page.  _bt_binsrch() has no lock or refcount side effects
 * on the buffer.
 */
OffsetNumber
zcurve_binsrch(Relation rel,
			Buffer buf,
			int keysz,
			ScanKey scankey,
			bool nextkey)
{
	Page		page;
	BTPageOpaque opaque;
	OffsetNumber low,
				high;
	int32		result,
				cmpval;

	page = BufferGetPage(buf);
	opaque = (BTPageOpaque) PageGetSpecialPointer(page);

	low = P_FIRSTDATAKEY(opaque);
	high = PageGetMaxOffsetNumber(page);

	/*
	 * If there are no keys on the page, return the first available slot. Note
	 * this covers two cases: the page is really empty (no keys), or it
	 * contains only a high key.  The latter case is possible after vacuuming.
	 * This can never happen on an internal page, however, since they are
	 * never empty (an internal page must have children).
	 */
	if (high < low)
		return low;

	/*
	 * Binary search to find the first key on the page >= scan key, or first
	 * key > scankey when nextkey is true.
	 *
	 * For nextkey=false (cmpval=1), the loop invariant is: all slots before
	 * 'low' are < scan key, all slots at or after 'high' are >= scan key.
	 *
	 * For nextkey=true (cmpval=0), the loop invariant is: all slots before
	 * 'low' are <= scan key, all slots at or after 'high' are > scan key.
	 *
	 * We can fall out when high == low.
	 */
	high++;						/* establish the loop invariant for high */

	cmpval = nextkey ? 0 : 1;	/* select comparison value */

	while (high > low)
	{
		OffsetNumber mid = low + ((high - low) / 2);

		/* We have low <= mid < high, so mid points at a real slot */

		/* elog(INFO, "(%d .. %d .. %d)", low, mid, high); */
		result = zcurve_compare(rel, keysz, scankey, page, mid);

		if (result >= cmpval)
			low = mid + 1;
		else
			high = mid;
	}
	/*
	 * At this point we have high == low, but be careful: they could point
	 * past the last slot on the page.
	 *
	 * On a leaf page, we always return the first key >= scan key (resp. >
	 * scan key), which could be the last slot + 1.
	 */

	/*elog(INFO, "bsearch %d", low);*/
	if (P_ISLEAF(opaque))
		return low;

	/*
	 * On a non-leaf page, return the last key < scan key (resp. <= scan key).
	 * There must be one if _bt_compare() is playing by the rules.
	 */
	Assert(low > P_FIRSTDATAKEY(opaque));

	return OffsetNumberPrev(low);
}

/*
 *	_bt_search() -- Search the tree for a particular scankey,
 *		or more precisely for the first leaf page it could be on.
 *
 * The passed scankey must be an insertion-type scankey (see nbtree/README),
 * but it can omit the rightmost column(s) of the index.
 *
 * When nextkey is false (the usual case), we are looking for the first
 * item >= scankey.  When nextkey is true, we are looking for the first
 * item strictly greater than scankey.
 *
 * Return value is a stack of parent-page pointers.  *bufP is set to the
 * address of the leaf-page buffer, which is read-locked and pinned.
 * No locks are held on the parent pages, however!
 *
 * NOTE that the returned buffer is read-locked regardless of the access
 * parameter.  However, access = BT_WRITE will allow an empty root page
 * to be created and returned.  When access = BT_READ, an empty index
 * will result in *bufP being set to InvalidBuffer.  Also, in BT_WRITE mode,
 * any incomplete splits encountered during the search will be finished.
 */
BTStack
zcurve_search(Relation rel, int keysz, ScanKey scankey, bool nextkey,
		   Buffer *bufP, int access)
{
	BTStack		stack_in = NULL;
	int ilevel;

	/* Get the root page to start with */
	*bufP = _bt_getroot(rel, access);


	/* If index is empty and access = BT_READ, no root page is created. */
	if (!BufferIsValid(*bufP))
		return (BTStack) NULL;

	/* Loop iterates once per level descended in the tree */
	for (ilevel=0;;ilevel++)
	{
		Page		page;
		BTPageOpaque opaque;
		OffsetNumber offnum;
		ItemId		itemid;
		IndexTuple	itup;
		BlockNumber blkno;
		BlockNumber par_blkno;
		BTStack		new_stack;

		/*
		 * Race -- the page we just grabbed may have split since we read its
		 * pointer in the parent (or metapage).  If it has, we may need to
		 * move right to its new sibling.  Do that.
		 *
		 * In write-mode, allow _bt_moveright to finish any incomplete splits
		 * along the way.  Strictly speaking, we'd only need to finish an
		 * incomplete split on the leaf page we're about to insert to, not on
		 * any of the upper levels (they are taken care of in _bt_getstackbuf,
		 * if the leaf page is split and we insert to the parent page).  But
		 * this is a good opportunity to finish splits of internal pages too.
		 */
		*bufP = _bt_moveright(rel, *bufP, keysz, scankey, nextkey,
							  (access == BT_WRITE), stack_in,
							  BT_READ);
		/* if this is a leaf page, we're done */
		page = BufferGetPage(*bufP);

		opaque = (BTPageOpaque) PageGetSpecialPointer(page);
		if (P_ISLEAF(opaque))
			break;

		/*
		 * Find the appropriate item on the internal page, and get the child
		 * page that it points to.
		 */
		offnum = zcurve_binsrch(rel, *bufP, keysz, scankey, nextkey);
		itemid = PageGetItemId(page, offnum);
		itup = (IndexTuple) PageGetItem(page, itemid);
		blkno = ItemPointerGetBlockNumber(&(itup->t_tid));
		par_blkno = BufferGetBlockNumber(*bufP);

		/*
		 * We need to save the location of the index entry we chose in the
		 * parent page on a stack. In case we split the tree, we'll use the
		 * stack to work back up to the parent page.  We also save the actual
		 * downlink (TID) to uniquely identify the index entry, in case it
		 * moves right while we're working lower in the tree.  See the paper
		 * by Lehman and Yao for how this is detected and handled. (We use the
		 * child link to disambiguate duplicate keys in the index -- Lehman
		 * and Yao disallow duplicate keys.)
		 */
		new_stack = (BTStack) palloc(sizeof(BTStackData));
		new_stack->bts_blkno = par_blkno;
		new_stack->bts_offset = offnum;
		memcpy(&new_stack->bts_btentry, itup, sizeof(IndexTupleData));
		new_stack->bts_parent = stack_in;

		/* drop the read lock on the parent page, acquire one on the child */
		*bufP = _bt_relandgetbuf(rel, *bufP, blkno, BT_READ);

		/* okay, all set to move down a level */
		stack_in = new_stack;
	}

	return stack_in;
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

int 
trace_tree_by_val(Relation rel, uint64 zv)
{
	ScanKeyData skey;
	BTStack	    pstack = NULL, stack = NULL;
	bool nextkey = 0;
	int offnum;

	Buffer buf;
	int ilevel;

	ScanKeyInit(	&skey,
			1, /* key part idx */
			BTGreaterStrategyNumber, 
			F_INT8GE,
			Int64GetDatum(zv));

	pstack = zcurve_search(rel, 1, &skey, nextkey,  &buf, BT_READ);
	offnum = zcurve_binsrch(rel, buf, 1, &skey, nextkey);
	/*elog(INFO, "found(%d)", offnum);*/

	ilevel = 0;
	stack = pstack;
	for (;;)
	{
		/*elog(INFO, "level(%d) blkno(%d) offset(%d)", ilevel++, stack->bts_blkno, stack->bts_offset);*/

		if (stack->bts_parent == NULL)
			break;

		stack = stack->bts_parent;
	}
	
	{
		Page		page;
		page = BufferGetPage(buf);
		trace_page(rel, &skey, page);
	}

	_bt_relbuf(rel, buf);
	_bt_freestack(pstack);
	_bt_freeskey(&skey);
  	return 0;
}

void trace_page(Relation rel, ScanKey skey, Page page)
{
	ItemId		itemid;
	IndexTuple	itup;
	Datum		arg;
	bool		null;
	OffsetNumber	maxoff = 0, off;
	uint32		ix, iy;

	BTPageOpaque opaque;
	OffsetNumber low;

	opaque = (BTPageOpaque) PageGetSpecialPointer(page);
	low = P_FIRSTDATAKEY(opaque);

	elog(INFO, "<<<");
	maxoff = PageGetMaxOffsetNumber(page);
	for (off = low; off <= maxoff; off++)
	{
		itemid = PageGetItemId(page, off);
		itup = (IndexTuple) PageGetItem(page, itemid);

		arg = index_getattr(itup, 1, RelationGetDescr(rel), &null);
		zcurve_toXY (DatumGetInt64(arg), &ix, &iy);
		elog(INFO, "%d) val(%ld), x(%d), y(%d), cmp(%d) dead(%d)", off, DatumGetInt64(nocache_index_getattr(itup, 1, RelationGetDescr(rel))), ix, iy, _bt_compare(rel, 1, skey, page, off), ItemIdIsDead(itemid));

	}
	elog(INFO, ">>>");
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
	uint64 cnt = 0;

	List	   *relname_list;
	RangeVar   *relvar;
	Relation    relation;

	relname_list = stringToQualifiedNameList(relname);
	relvar = makeRangeVarFromNameList(relname_list);
	relation = indexOpen(relvar);

	/*elog(INFO, "%s height(%d) nattrs(%d)", relname, _bt_getrootheight(relation), RelationGetNumberOfAttributes(relation));*/


	{
		/*trace_tree_by_val(relation, start_zv);*/
		spt_query2_def_t qdef;
		int ret = 0;

		spt_query2_def_t_CTOR (&qdef, relation, x0, y0, x1, y1);

		ret = pointSpatial2d_moveFirst(&qdef);
		/*elog(INFO, "moveFirst returns %d", ret);*/
		while (ret)
		{
			cnt++;
			ret = pointSpatial2d_moveNext(&qdef);
		}
		spt_query2_def_t_DTOR (&qdef);
		/*elog(INFO, "DTOR'ed spt_query2_def_t");*/
	}

	indexClose(relation);

	PG_FREE_IF_COPY(relname, 0);
   	PG_RETURN_INT64(cnt);
}

int 
zcurve_scan_move_first(zcurve_scan_ctx_t *ctx, uint64 start_val)
{
	BTStack	pstack = NULL, stack = NULL;
	bool 	nextkey = 0;
	Page 	page;
	int 	ilevel = 0;

	ItemId		itemid;
	IndexTuple	itup;
	Datum		arg;
	bool		null;

	/*elog(INFO, "zcurve_scan_move_first(%lx)", start_val);*/


	if (ctx->buf_)
		_bt_relbuf(ctx->rel_, ctx->buf_);

	ctx->init_zv_ = start_val;
	ctx->skey_.sk_argument = Int64GetDatum(start_val);

	pstack = zcurve_search(ctx->rel_, 1, &ctx->skey_, nextkey,  &ctx->buf_, BT_READ);
	if (NULL == pstack)
		return 0;

	ctx->offset_ = zcurve_binsrch(ctx->rel_, ctx->buf_, 1, &ctx->skey_, nextkey);
	page = BufferGetPage(ctx->buf_);
	ctx->max_offset_ = PageGetMaxOffsetNumber(page);
	if (ctx->offset_ <= ctx->max_offset_)
	{
		itemid = PageGetItemId(page, ctx->offset_);
		itup = (IndexTuple) PageGetItem(page, itemid);
		arg = index_getattr(itup, 1, RelationGetDescr(ctx->rel_), &null);
		ctx->cur_val_ = DatumGetInt64(arg);

		itemid = PageGetItemId(page, ctx->max_offset_);
		itup = (IndexTuple) PageGetItem(page, itemid);
		arg = index_getattr(itup, 1, RelationGetDescr(ctx->rel_), &null);
		ctx->last_page_val_ = DatumGetInt64(arg);

		_bt_freestack(pstack);
		pstack = NULL;

		/*elog(INFO, "zcurve_scan_move_first (%lx -> %lx)[%d %d]", ctx->cur_val_, ctx->last_page_val_, ctx->offset_, ctx->max_offset_);*/
		return 1;
	}
	else
	{
		ilevel = 0;
		stack = pstack;
		for (;;)
		{
			BTPageOpaque opaque;
			OffsetNumber mx = 0;
			ilevel++;

			/*elog(INFO, "level(%d) blkno(%d) offset(%d)", ilevel - 1, stack->bts_blkno, stack->bts_offset);*/

			/* drop the read lock on the parent page, acquire one on the child */
			ctx->buf_ = _bt_relandgetbuf(ctx->rel_, ctx->buf_, stack->bts_blkno, BT_READ);
			page = BufferGetPage(ctx->buf_);
			mx = PageGetMaxOffsetNumber(page);
			/*elog(INFO, "max=(%d)", mx);*/
			if (stack->bts_offset < mx)
			{
				BlockNumber  blkno;
				int i;
				{
					opaque = (BTPageOpaque) PageGetSpecialPointer(page);
					itemid = PageGetItemId(page, stack->bts_offset + 1);
					itup = (IndexTuple) PageGetItem(page, itemid);
					blkno = ItemPointerGetBlockNumber(&(itup->t_tid));
					/*elog (INFO, "BLKNO=%d", blkno);*/
					ctx->buf_ = _bt_relandgetbuf(ctx->rel_, ctx->buf_, blkno, BT_READ);
					page = BufferGetPage(ctx->buf_);
					opaque = (BTPageOpaque) PageGetSpecialPointer(page);
					ctx->offset_ = P_FIRSTDATAKEY(opaque);
				}
				for (i = 0; i < ilevel - 1; i++)
				{
					opaque = (BTPageOpaque) PageGetSpecialPointer(page);
					ctx->offset_ = P_FIRSTDATAKEY(opaque);
					itemid = PageGetItemId(page, ctx->offset_);
					itup = (IndexTuple) PageGetItem(page, itemid);
					blkno = ItemPointerGetBlockNumber(&(itup->t_tid));
					/*elog (INFO, "BLKNO=%d", blkno);*/
					ctx->buf_ = _bt_relandgetbuf(ctx->rel_, ctx->buf_, blkno, BT_READ);
					page = BufferGetPage(ctx->buf_);
				}
				ctx->max_offset_ = PageGetMaxOffsetNumber(page);
				itemid = PageGetItemId(page, ctx->offset_);
				itup = (IndexTuple) PageGetItem(page, itemid);
				arg = index_getattr(itup, 1, RelationGetDescr(ctx->rel_), &null);
				ctx->cur_val_ = DatumGetInt64(arg);

				itemid = PageGetItemId(page, ctx->max_offset_);
				itup = (IndexTuple) PageGetItem(page, itemid);
				arg = index_getattr(itup, 1, RelationGetDescr(ctx->rel_), &null);
				ctx->last_page_val_ = DatumGetInt64(arg);

				_bt_freestack(pstack);
				pstack = NULL;

				/*elog(INFO, "zcurve_scan_move_first (EOF)(%lx -> %lx)[%d %d]", ctx->cur_val_, ctx->last_page_val_, ctx->offset_, ctx->max_offset_);*/
				return 1;
			}

			if (stack->bts_parent == NULL)
				break;

			stack = stack->bts_parent;
		}
	}

	if (pstack)
		_bt_freestack(pstack);
	/*elog(INFO, "zcurve_scan_move_first (EOF)[%d %d]", ctx->offset_, ctx->max_offset_);*/
	return 0;
}

int 
zcurve_scan_move_next(zcurve_scan_ctx_t *ctx)
{
	Assert(ctx);
	/*elog(INFO, "zcurve_scan_move_next");*/
	if (ctx->offset_ < ctx->max_offset_)
	{
		Page 	page;
		ItemId		itemid;
		IndexTuple	itup;
		Datum		arg;
		bool		null;

		ctx->offset_++;
		page = BufferGetPage(ctx->buf_);
		itemid = PageGetItemId(page, ctx->offset_);
		itup = (IndexTuple) PageGetItem(page, itemid);
		arg = index_getattr(itup, 1, RelationGetDescr(ctx->rel_), &null);
		ctx->cur_val_ = DatumGetInt64(arg);
/*		elog(INFO, "zcurve_scan_move_next (%lx -> %lx)[%d %d]", ctx->cur_val_, ctx->last_page_val_, ctx->offset_, ctx->max_offset_);*/
		return 1;
	}
	/*elog(INFO, "zcurve_scan_move_next OOPS");*/
	return 0;
}

int 
zcurve_scan_ctx_CTOR(zcurve_scan_ctx_t *ctx, Relation rel, uint64 start_val)
{
	Assert(ctx);
	ctx->rel_ = rel;
	ctx->init_zv_ = start_val;
	ScanKeyInit(&ctx->skey_, 1/*key part idx*/, BTLessStrategyNumber, F_INT8LE, Int64GetDatum(start_val));
	ctx->offset_ = 0;
	ctx->max_offset_ = 0;
	ctx->cur_val_ = 0;
	ctx->last_page_val_ = 0;
	ctx->buf_ = 0;
	/*elog(INFO, "zcurve_scan_ctx_CTOR(%ld)", sizeof(ctx->buf_));*/
	return 1;
}

int 
zcurve_scan_ctx_DTOR(zcurve_scan_ctx_t *ctx)
{
	Assert(ctx);
	if (ctx->rel_)
	{
		if (ctx->buf_)
			_bt_relbuf(ctx->rel_, ctx->buf_);
		memset(ctx, 0, sizeof(*ctx));
		/*elog(INFO, "zcurve_scan_ctx_DTOR");*/
		return 0;
	}
	return -1;
}

int 
zcurve_scan_ctx_is_opened(zcurve_scan_ctx_t *ctx)
{
	Assert(ctx);
	return 	(ctx->rel_)? 1 : 0;
}

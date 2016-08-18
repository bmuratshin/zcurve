/*
 * contrib/zcurve/sp_tree.c
 *
 *
 * sp_tree.c -- low level operations with numbers and 2D ZCurve index realized as a regular btree
 *		
 *
 * Modified by Boris Muratshin, mailto:bmuratshin@gmail.com
 */

#include <string.h>
#include "postgres.h"
#include "catalog/pg_type.h"
#include "fmgr.h"
#include "funcapi.h"
#include "utils/rel.h"
#include "utils/builtins.h"
#include "utils/lsyscache.h"
#include "utils/fmgroids.h"
#include "utils/builtins.h"
#include "utils/numeric.h"
#include "catalog/namespace.h"
#include "access/nbtree.h"
#include "access/htup_details.h"
#include "storage/bufpage.h"

#include "sp_tree.h"
#include "sp_query.h"
#include "gen_list.h"
#include "list_sort.h"

#if PG_VERSION_NUM >= 90600
#define heap_formtuple heap_form_tuple
#endif

#if 0
/* test only */
static void 
trace_page_2d(Relation rel, ScanKey skey, Page page)
{
	ItemId		itemid;
	IndexTuple	itup;
	Datum		arg;
	bool		null;
	OffsetNumber	maxoff = 0, off;
	uint32		ix, iy;
	uint64		zv;

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
		zv = DatumGetInt64(DirectFunctionCall1(numeric_int8, arg));
		zcurve_toXY (zv, &ix, &iy);
		elog(INFO, "%d) val(%ld), x(%d), y(%d), cmp(%d) dead(%d)", 
			off, zv, ix, iy, 
			_bt_compare(rel, 1, skey, page, off), 
			ItemIdIsDead(itemid));

	}
	elog(INFO, ">>>");
}
#endif


/* 
   analog of _bt_compare from src\backend\access\nbtree\nbtsearch.c 
   with some 2d zcurve specific
*/
int32
zcurve_compare_2d(
	zcurve_scan_ctx_t *pctx,
	Page page,
	OffsetNumber offnum)
{
	Relation rel = pctx->rel_;
	int keysz = 1;

	TupleDesc	itupdesc = RelationGetDescr(rel);
	BTPageOpaque opaque = (BTPageOpaque) PageGetSpecialPointer(page);
	IndexTuple	itup;
	int		i;
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
		int cmp;

		datum = index_getattr(itup, i, itupdesc, &isNull);
		cmp = DatumGetInt32(
			DirectFunctionCall2(
				numeric_cmp,
				pctx->skey_.sk_argument,
				datum));
		if (cmp)
			return cmp;
	}
	return 0;
}

/* 
 * zcurve_binsrch_2d()
 * analog of _bt_binsearch from src\backend\access\nbtree\nbtsearch.c 
 * with some 2d zcurve specific
 */
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
zcurve_binsrch_2d (zcurve_scan_ctx_t *pctx)
{
	bool nextkey = 0;
	Page page;
	BTPageOpaque opaque;
	OffsetNumber low, high;
	int32 result, cmpval;

	page = BufferGetPage(pctx->buf_);
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
		result = zcurve_compare_2d(pctx, page, mid);

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
 * zcurve_search_2d()
 * analog of _bt_search from src\backend\access\nbtree\nbtsearch.c 
 * with some 2d zcurve specific
 * returns 0 if error occured
 */
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
int
zcurve_search_2d(zcurve_scan_ctx_t *pctx)
{
	Relation rel = pctx->rel_;
	int keysz = 1;
	BTStack	stack_in = NULL;
	int ilevel;
	int access = BT_READ;
	bool nextkey = 0;

	/* Get the root page to start with */
	pctx->buf_ = _bt_getroot(rel, access);

	/* If index is empty and access = BT_READ, no root page is created. */
	if (!BufferIsValid(pctx->buf_))
		return 0;

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
		pctx->buf_ = _bt_moveright(rel, pctx->buf_, keysz, &pctx->skey_, nextkey,
						(access == BT_WRITE), stack_in,	  BT_READ);
		/* if this is a leaf page, we're done */
		page = BufferGetPage(pctx->buf_);

		opaque = (BTPageOpaque) PageGetSpecialPointer(page);
		if (P_ISLEAF(opaque))
			break;

		/*
		 * Find the appropriate item on the internal page, and get the child
		 * page that it points to.
		 */
		offnum = zcurve_binsrch_2d (pctx);
		itemid = PageGetItemId(page, offnum);
		itup = (IndexTuple) PageGetItem(page, itemid);
		blkno = ItemPointerGetBlockNumber(&(itup->t_tid));
		par_blkno = BufferGetBlockNumber(pctx->buf_);

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
		pctx->buf_ = _bt_relandgetbuf(rel, pctx->buf_, blkno, BT_READ);

		/* okay, all set to move down a level */
		stack_in = new_stack;
	}
	pctx->pstack_ = stack_in;
	return 1;
}



/* 
   in some circumstances our spatial subquery reached the end of page,
   but last page item is equal the end of query, so we can find some data on the page next
   if preserve_position is true, cursor after return stays untouched
 */
static int 
zcurve_scan_step_forward(zcurve_scan_ctx_t *ctx, bool preserve_position)
{
	Page 		page;
	BTStack		stack = ctx->pstack_;
	ItemId		itemid;
	IndexTuple	itup;
	Datum		arg;
	bool		null;
	int 		ilevel = 0;

	/* save the cursor context */
	BlockNumber	old_block_num = (ctx->buf_) ? BufferGetBlockNumber(ctx->buf_) : 0;
	BlockNumber	new_block_num = 0;

	OffsetNumber	old_offset = ctx->offset_;
	OffsetNumber	old_max_offset = ctx->max_offset_;
	ItemPointerData old_iptr = ctx->iptr_;

	bitKey_t	old_cur_val = ctx->cur_val_;
	bitKey_t	old_last_page_val = ctx->last_page_val_;
	
	for (;;)
	{
		BTPageOpaque opaque;
		OffsetNumber mx = 0;
		ilevel++;

		/* such as we currently at the pasge end, we need to step upper by pages stack and then down to the next page */
		/* drop the read lock on the parent page, acquire one on the child */
		ctx->buf_ = _bt_relandgetbuf(ctx->rel_, ctx->buf_, stack->bts_blkno, BT_READ);
		new_block_num = stack->bts_blkno;
		page = BufferGetPage(ctx->buf_);
		mx = PageGetMaxOffsetNumber(page);

		/* if we are not at the end of the parent's page 
		   otherwise, just keep step upper
		*/
		if (stack->bts_offset < mx)
		{
			BlockNumber  blkno;
			int i;
			/* one step down to (stack->bts_offset + 1) item */
			{
				opaque = (BTPageOpaque) PageGetSpecialPointer(page);
				itemid = PageGetItemId(page, stack->bts_offset + 1);
				itup = (IndexTuple) PageGetItem(page, itemid);
				blkno = ItemPointerGetBlockNumber(&(itup->t_tid));
				/*elog (INFO, "BLKNO=%d", blkno);*/
				ctx->buf_ = _bt_relandgetbuf(ctx->rel_, ctx->buf_, blkno, BT_READ);
				new_block_num = blkno;

				page = BufferGetPage(ctx->buf_);
				opaque = (BTPageOpaque) PageGetSpecialPointer(page);
				ctx->offset_ = P_FIRSTDATAKEY(opaque);
			}
			/* and then going down by the first only items */
			for (i = 0; i < ilevel - 1; i++)
			{
				opaque = (BTPageOpaque) PageGetSpecialPointer(page);
				ctx->offset_ = P_FIRSTDATAKEY(opaque);
				itemid = PageGetItemId(page, ctx->offset_);
				itup = (IndexTuple) PageGetItem(page, itemid);
				blkno = ItemPointerGetBlockNumber(&(itup->t_tid));

				new_block_num = blkno;
				ctx->buf_ = _bt_relandgetbuf(ctx->rel_, ctx->buf_, blkno, BT_READ);
				page = BufferGetPage(ctx->buf_);
			}

			/* filling cursor context */
			ctx->max_offset_ = PageGetMaxOffsetNumber(page);
			itemid = PageGetItemId(page, ctx->offset_);
			itup = (IndexTuple) PageGetItem(page, itemid);
			arg = index_getattr(itup, 1, RelationGetDescr(ctx->rel_), &null);
			bitKey_fromLong(&ctx->cur_val_, arg);
			ctx->next_val_ = ctx->cur_val_;
			ctx->iptr_ = itup->t_tid;

			itemid = PageGetItemId(page, ctx->max_offset_);
			itup = (IndexTuple) PageGetItem(page, itemid);
			arg = index_getattr(itup, 1, RelationGetDescr(ctx->rel_), &null);
			bitKey_fromLong(&ctx->last_page_val_, arg);

			/* and restoring context back if necessary */
			if (preserve_position && old_block_num && old_block_num != new_block_num)
			{
				ctx->buf_ = _bt_relandgetbuf(ctx->rel_, ctx->buf_, old_block_num, BT_READ);

				ctx->offset_ = old_offset;
				ctx->max_offset_ = old_max_offset;
				ctx->cur_val_ = old_cur_val;
				ctx->last_page_val_ = old_last_page_val;
				ctx->iptr_ = old_iptr;
				/* ctx->next_val_ not restored */
			}
			return 1;
		}

		if (stack->bts_parent == NULL)
			break;

		stack = stack->bts_parent;
	}
  return 0;
}

/* constructing a scan context, it may be restarted later with other start_val */
int 
zcurve_scan_ctx_CTOR(zcurve_scan_ctx_t *ctx, Relation rel, int ncoords)
{
	Assert(ctx && rel);
	ctx->rel_ = rel;
	bitKey_CTOR(&ctx->init_zv_, ncoords);
	ScanKeyInit(&ctx->skey_, 1, BTLessStrategyNumber, F_INT8LE, bitKey_toLong(&ctx->init_zv_));
	ctx->offset_ = 0;
	ctx->max_offset_ = 0;
	bitKey_CTOR(&ctx->cur_val_, ncoords);
	bitKey_CTOR(&ctx->next_val_, ncoords);
	bitKey_CTOR(&ctx->last_page_val_, ncoords);
	ctx->buf_ = 0;
	ctx->pstack_ = NULL;
	return 1;
}

/* destructor, unlock page and free pages stack*/
int 
zcurve_scan_ctx_DTOR(zcurve_scan_ctx_t *ctx)
{
	Assert(ctx);
	if (ctx->rel_ && ctx->buf_)
	{
		_bt_relbuf(ctx->rel_, ctx->buf_);
	}

	if (ctx->pstack_)
	{
		_bt_freestack(ctx->pstack_);
	}

	memset(ctx, 0, sizeof(*ctx));
	return 1;
}


/* starting cursor, it may be restorted with new value without calling destructor */
int 
zcurve_scan_move_first(zcurve_scan_ctx_t *ctx, const bitKey_t *start_val)
{
	Page 		page;
	ItemId		itemid;
	IndexTuple	itup;
	Datum		arg;
	bool		null;

	/* first, let's free a page from last subquery if exists */
	if (ctx->buf_)
		_bt_relbuf(ctx->rel_, ctx->buf_);

	/* reinit starting values */
	ctx->init_zv_ = *start_val;
	ctx->skey_.sk_argument = bitKey_toLong(start_val);

	/* let's free a pages stack from last subquery if exists */
	if (ctx->pstack_)
		_bt_freestack(ctx->pstack_);

	/* index tree lookup by the starting value */
	if (0 == zcurve_search_2d(ctx))
		return 0;

	/* 
	   found smth, ok 
	   now trying to find >= item on the list page and store cursore position 
	 */
	ctx->offset_ = zcurve_binsrch_2d (ctx);
	page = BufferGetPage(ctx->buf_);

	ctx->max_offset_ = PageGetMaxOffsetNumber(page);
	if (ctx->offset_ <= ctx->max_offset_)
	{
		itemid = PageGetItemId(page, ctx->offset_);
		itup = (IndexTuple) PageGetItem(page, itemid);
		arg = index_getattr(itup, 1, RelationGetDescr(ctx->rel_), &null);
		bitKey_fromLong(&ctx->cur_val_, arg);
		ctx->iptr_ = itup->t_tid;

		itemid = PageGetItemId(page, ctx->max_offset_);
		itup = (IndexTuple) PageGetItem(page, itemid);
		arg = index_getattr(itup, 1, RelationGetDescr(ctx->rel_), &null);
		bitKey_fromLong(&ctx->last_page_val_, arg);

		return 1;
	}
	else
	{
		/* well, our item between pages, we need to move cursor forward */
		return zcurve_scan_step_forward(ctx, false);
	}
	/* notreached */
	return 0;
}

/* cursor forward moving*/
int 
zcurve_scan_move_next(zcurve_scan_ctx_t *ctx)
{
	Assert(ctx);
	/*elog(INFO, "zcurve_scan_move_next");*/
	if (ctx->offset_ < ctx->max_offset_)
	{
		/* 
		   our cursor currently points into the page, the end is not reached 
		   just increase ctx->offset_ and store position params
		 */
		Page 		page;
		ItemId		itemid;
		IndexTuple	itup;
		Datum		arg;
		bool		null;

		ctx->offset_++;
		page = BufferGetPage(ctx->buf_);
		itemid = PageGetItemId(page, ctx->offset_);
		itup = (IndexTuple) PageGetItem(page, itemid);
		arg = index_getattr(itup, 1, RelationGetDescr(ctx->rel_), &null);
		bitKey_fromLong(&ctx->cur_val_, arg);

		ctx->iptr_ = itup->t_tid;
		return 1;
	}

	/* page ends, just move to next one */
	return zcurve_scan_step_forward(ctx, false);
}

/* test first item on the next page */
int 
zcurve_scan_try_move_next(zcurve_scan_ctx_t *ctx, const bitKey_t *check_val)
{
	/* do nothing if the cursor is not on the end of page */
	if (ctx->offset_ < ctx->max_offset_)
	{
		return 1;
	}
	/* test first item on the next page */
	if (zcurve_scan_step_forward(ctx, true))
	{
		int ret = (bitKey_cmp(& ctx->next_val_, check_val) <= 0) ? 1 : 0;
		/* if it is in subquery range, return true */
		return ret;
	}
	return 0;
}

/* testing for cursor is active */
int 
zcurve_scan_ctx_is_opened(zcurve_scan_ctx_t *ctx)
{
	Assert(ctx);
	return 	(ctx->rel_)? 1 : 0;
}

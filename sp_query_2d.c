/*
 * contrib/zcurve/sp_query_2d.c
 *
 *
 * sp_query_2d.c -- 2d spatial lookup stuff
 *		
 *
 * Modified by Boris Muratshin, mailto:bmuratshin@gmail.com
 */
#include "postgres.h"

#include <math.h>
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
#include "sp_query_2d.h"
#include "bitkey.h"


/* constructor */
void 
spt_query2_CTOR (spt_query2_t *ps, Relation rel, uint32 minx, uint32 miny, uint32 maxx, uint32 maxy)
{
	Assert(NULL != ps);
	ps->queryHead_ = NULL;
	ps->freeHead_ = NULL;
	ps->minX_ = minx;
	ps->minY_ = miny;
	ps->maxX_ = maxx;
	ps->maxY_ = maxy;

	bitKey_CTOR(&ps->currentKey_, 2);
	bitKey_CTOR(&ps->lastKey_, 2);

	/* tree cursor init */
	zcurve_scan_ctx_CTOR(&ps->qctx_, rel);
}

/* destructor */
void 
spt_query2_DTOR (spt_query2_t *ps)
{
	Assert(NULL != ps);
	zcurve_scan_ctx_DTOR(&ps->qctx_);
}


/* if freeHead_ is not empty gets memory there or just palloc some */
spatial2Query_t *
spt_query2_createQuery(spt_query2_t *q)
{
	spatial2Query_t *ret = NULL;
	Assert(q);
	if(q->freeHead_)
	{
		spatial2Query_t *retval = q->freeHead_;
		q->freeHead_ = q->freeHead_->prevQuery_;
		return retval;
	}
	ret = (spatial2Query_t *)palloc(sizeof(spatial2Query_t));
	ret->curBitNum_ = 0;
	ret->prevQuery_ = NULL;
	bitKey_CTOR(&ret->lowKey_, 2);
	bitKey_CTOR(&ret->highKey_, 2);
	return ret;
}

/* push subquery to the reuse list */
void 
spt_query2_freeQuery (spt_query2_t *q, spatial2Query_t *sq)
{
	Assert(q);
	sq->prevQuery_ = q->freeHead_;
	q->freeHead_ = sq;
}

/* just closes index tree cursor */
void 
spt_query2_closeQuery(spt_query2_t *q)
{
	Assert(q);
	if (zcurve_scan_ctx_is_opened(&q->qctx_))
	{
		q->queryHead_ = q->freeHead_ = NULL;
		zcurve_scan_ctx_DTOR(&q->qctx_);
	}
}

/* marks subquery on the top of queue as finished and pops it out */
void 
spt_query2_releaseSubQuery(spt_query2_t *q)
{
	Assert(q);
	if(q->queryHead_)
	{
		spatial2Query_t *prevQuery = q->queryHead_->prevQuery_;
		spt_query2_freeQuery(q, q->queryHead_);
		q->queryHead_ = prevQuery;
		q->subQueryFinished_ = 1;
	}
}

/* PUBLIC, spatial cursor start, returns not 0 in case of cuccess, resulting data in x,y,iptr */
int
spt_query2_moveFirst(spt_query2_t *q, uint32 *x, uint32 * y, ItemPointerData *iptr)
{
	uint32 coords[ZKEY_MAX_COORDS];
	Assert(q && x && y && iptr);

	q->queryHead_ = spt_query2_createQuery (q);
	q->queryHead_->prevQuery_ = NULL;
	q->queryHead_->curBitNum_ = 63;

	coords[0] = q->minX_;
	coords[1] = q->minY_;
	bitKey_fromCoords(&q->queryHead_->lowKey_, coords, ZKEY_MAX_COORDS); //q->minX_, q->minY_);

	coords[0] = q->maxX_;
	coords[1] = q->maxY_;
	bitKey_fromCoords(&q->queryHead_->highKey_, coords, ZKEY_MAX_COORDS); //q->maxX_, q->maxY_);

	return spt_query2_findNextMatch(q, x, y, iptr);
}

/* PUBLIC, main loop iteration, returns not 0 in case of cuccess, resulting data in x,y,iptr */
int
spt_query2_moveNext (spt_query2_t *q, uint32 *x, uint32 * y, ItemPointerData *iptr)
{
	Assert(q && x && y && iptr);
	/*if all finished just go out*/
	if (!zcurve_scan_ctx_is_opened(&q->qctx_))
		return 0;

	/* current subquery is finished, let's get a new one*/
	if (q->subQueryFinished_)
    		return spt_query2_findNextMatch(q, x, y, iptr);

	/* when the upper bound of subquery is equal to the current page last val, we need some additional testing */
	if(!spt_query2_checkNextPage(q))
	{
		/* no, nothin interesting in the begining of the next page, start the next subquery */
		spt_query2_releaseSubQuery(q);
		return spt_query2_findNextMatch(q, x, y, iptr);
	}

	/* are there some interesting data in the index tree? */
	if (!spt_query2_queryNextKey(q))
	{
		/* query diapason is exhausted, no more data, finished */
		spt_query2_closeQuery(q);
		return 0;
	}

	/* up to the lookup diapason end */
	while (bitKey_cmp(&q->currentKey_, &q->queryHead_->highKey_) <= 0)//q->currentKey_.val_ <= q->queryHead_->highKey_.val_)
	{
		/* test if current key in lookup extent */
		if (spt_query2_checkKey(q, x, y))
		{
			/* if yes, returning success */
			*iptr = q->iptr_;
			return 1;
		}
		/* again, when the upper bound of subquery is equal to the current page last val, we need some additional testing */
		if (!spt_query2_checkNextPage(q))
			break;

		/* are there some interesting data there? */
		if (!spt_query2_queryNextKey(q))
		{
			/* no, just returning */
			spt_query2_closeQuery(q);
			return 0;
		}
	}
	/* subquery finished, start the next one from the queue */
	spt_query2_releaseSubQuery(q);
	return spt_query2_findNextMatch(q, x, y, iptr);
}


/* 
  If cursor points not to the end of page just return OK.
  When yes, we need to test the begining of the next page if it is equal to the end of subquery diapason.
  If so, we should continue on the next page.
  Note, subquery MUST end at the last page item's value.
*/
int
spt_query2_checkNextPage(spt_query2_t *q)
{
	if (q->qctx_.offset_ == q->qctx_.max_offset_)
	{
		if (!zcurve_scan_try_move_next(&q->qctx_, &q->queryHead_->highKey_))
			return 0;
		if (bitKey_cmp(&q->qctx_.next_val_, &q->queryHead_->highKey_) > 0) //q->qctx_.next_val_ > q->queryHead_->highKey_.val_)
			return 0;
		return 1;
	}
	return 1;
}


/* 
   gets an subquery from queue, split it if necessary 
   till the full satisfaction and then test for an appropriate data
 */
int
spt_query2_findNextMatch(spt_query2_t *q, uint32 *x, uint32 * y, ItemPointerData *iptr)
{
	Assert(q);
	/* are there some subqueries in the queue? */
	while(q->queryHead_)
	{
		q->subQueryFinished_ = 0;
		/* (re)initialize cursor */
		if(!spt_query2_queryFind(q, &q->queryHead_->lowKey_))
		{
			/* end of tree */
			spt_query2_closeQuery (q);
			return 0;
		}
		/* while there is something to split (last value on the current page less then the upper bound of subquery diapason) */
		while (bitKey_cmp(&q->lastKey_, &q->queryHead_->highKey_) < 0)//q->lastKey_.val_ < q->queryHead_->highKey_.val_)
		{
			/* let's split query */
			spatial2Query_t *subQuery = NULL;

			/* decrease curBitNum till corresponding bits are equal in both diapason numbers */
			while ( bitKey_getBit(&q->queryHead_->lowKey_, q->queryHead_->curBitNum_) == 
				bitKey_getBit(&q->queryHead_->highKey_, q->queryHead_->curBitNum_))
			{
				q->queryHead_->curBitNum_--;
			}

			/* create neq subquery */
			subQuery = spt_query2_createQuery (q);
			/* push it to the queue */
			subQuery->prevQuery_ = q->queryHead_;
			/* init diapason */
			subQuery->lowKey_ = q->queryHead_->lowKey_;
			subQuery->highKey_ = q->queryHead_->highKey_;
			/* cut diapason by curBitNum for new subquery */
			bitKey_setLowBits(&subQuery->highKey_, q->queryHead_->curBitNum_);
			/* cut diapason by curBitNum for old subquery */
			bitKey_clearLowBits(&q->queryHead_->lowKey_, q->queryHead_->curBitNum_);
			/* decrease bits pointers */
			subQuery->curBitNum_ = --q->queryHead_->curBitNum_;

			/*{
				uint32 x0,y0;
				uint32 x1,y1;

				elog(INFO, "[%lx %lx]", q->queryHead->lowKey.val_, q->queryHead->highKey.val_);
				bit2Key_toXY(&q->queryHead->lowKey, &x0, &y0);
				bit2Key_toXY(&q->queryHead->highKey, &x1, &y1);
				elog(INFO, "Q[%d %d %d %d]",x0, y0, x1, y1);

				elog(INFO, "[%lx %lx]", subQuery->lowKey.val_, subQuery->highKey.val_);
				bit2Key_toXY(&subQuery->lowKey, &x0, &y0);
				bit2Key_toXY(&subQuery->highKey, &x1, &y1);
				elog(INFO, "Q[%d %d %d %d]",x0, y0, x1, y1);
			}*/

			q->queryHead_ = subQuery;
			q->subQueryFinished_ = 0;
		}

		/* up to the lookup diapason end */
        	while (bitKey_cmp(&q->currentKey_, &q->queryHead_->highKey_) <= 0)//q->currentKey_.val_ <= q->queryHead_->highKey_.val_)
		{
			/* test if current key in lookup extent */
			if (spt_query2_checkKey(q, x, y))
			{
				/* if yes, returning success */
				*iptr = q->iptr_;
				return 1;
			}
			/* again, when the upper bound of subquery is equal to the current page last val, we need some additional testing */
			if (!spt_query2_checkNextPage(q))
				break;
			/* are there some interesting data there? */
			if (!spt_query2_queryNextKey(q))
			{
				/* no, just returning */
				spt_query2_closeQuery(q);
                		return 0;
			}
		}
		/* subquery finished, let's try next one */
		spt_query2_releaseSubQuery(q);
	}
	/* all done */
	spt_query2_closeQuery(q);
	return 0;
};


/* split current cursor value back to x & y and check it complies to query extent */
int
spt_query2_checkKey (spt_query2_t *q, uint32 *x, uint32 * y)
{
	const bitKey_t *lKey = &q->queryHead_->lowKey_;
	const bitKey_t *hKey = &q->queryHead_->highKey_;
	uint32 coords[ZKEY_MAX_COORDS];

	if (0 == bitKey_between(&q->currentKey_, lKey, hKey))
		return 0;

	/* OK, return data */
	Assert(x);
	Assert(y);
	bitKey_toCoords (&q->currentKey_, coords, ZKEY_MAX_COORDS);
	*x = coords[0];
	*y = coords[1];
	return 1;
}

/* performs index cursor lookup for start_val */
int
spt_query2_queryFind (spt_query2_t *q, const bitKey_t *start_val)
{
	int ret = zcurve_scan_move_first(&q->qctx_, start_val);
	Assert(q);
	q->currentKey_ = q->qctx_.cur_val_;
	q->lastKey_ = q->qctx_.last_page_val_;
	q->iptr_ = q->qctx_.iptr_;
	return ret;
}

/* moves cursor forward */
int
spt_query2_queryNextKey (spt_query2_t *q)
{
	int ret = zcurve_scan_move_next(&q->qctx_);
	Assert(q);
	q->currentKey_ = q->qctx_.cur_val_;
	q->lastKey_ = q->qctx_.last_page_val_;
	q->iptr_ = q->qctx_.iptr_;
	return ret;
}


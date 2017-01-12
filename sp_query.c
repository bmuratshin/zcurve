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
 * contrib/zcurve/sp_query.c
 *
 *
 * sp_query.c -- spatial lookup stuff
 *		
 *
 * Author:	Boris Muratshin, mailto:bmuratshin@gmail.com
 *
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

#include "sp_tree.h"
#include "sp_query.h"
#include "bitkey.h"


/* constructor */
void 
spt_query2_CTOR (spt_query2_t *ps, Relation rel, const uint32 *min_coords, const uint32 *max_coords, bitkey_type ktype)
{
	int i;
	unsigned ncoords = bitKey_getNCoords(ktype);
	Assert(NULL != ps && ncoords < ZKEY_MAX_COORDS);

	ps->ncoords_ = ncoords;
	ps->keytype_ = ktype;
	ps->queryHead_ = NULL;
	ps->freeHead_ = NULL;

	for (i = 0; i < ncoords; i++)
	{
		ps->min_point_[i] = min_coords[i];
		ps->max_point_[i] = max_coords[i];
	}

	bitKey_CTOR(&ps->currentKey_, ktype);
	bitKey_CTOR(&ps->lastKey_, ktype);

	/* tree cursor init */
	zcurve_scan_ctx_CTOR(&ps->qctx_, rel, ktype);
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
	ret->solid_ = 0;
	ret->ncoords_ = q->ncoords_;
	ret->keytype_ = q->keytype_;
	ret->prevQuery_ = NULL;
	bitKey_CTOR(&ret->lowKey_, q->keytype_);
	bitKey_CTOR(&ret->highKey_, q->keytype_);
	return ret;
}

/* push subquery to the reuse list */
void 
spt_query2_freeQuery (spt_query2_t *q, spatial2Query_t *sq)
{
	Assert(q && sq);
	sq->prevQuery_ = q->freeHead_;
	q->freeHead_ = sq;
}


static int 
Log2(int n)
{
	return !!(n & 0xFFFF0000) << 4
		| !!(n & 0xFF00FF00) << 3
		| !!(n & 0xF0F0F0F0) << 2
		| !!(n & 0xCCCCCCCC) << 1
		| !!(n & 0xAAAAAAAA);
}

/* testing for query is solid - no additional splitting etc, just out data */
void
spt_query2_testSolidity (spatial2Query_t *q)
{
	uint32_t lcoords[ZKEY_MAX_COORDS];
	uint32_t hcoords[ZKEY_MAX_COORDS];
	int dcoords[ZKEY_MAX_COORDS], i, ok = 1, diff = 0, odiff = 0;
	uint64_t vol = 1;

	bitKey_toCoords (&q->lowKey_, lcoords, ZKEY_MAX_COORDS);
	bitKey_toCoords (&q->highKey_, hcoords, ZKEY_MAX_COORDS);

	for (i=0; i < q->ncoords_; i++)
	{
		dcoords[i] = hcoords[i] - lcoords[i];
		vol *= (unsigned)(dcoords[i]);
		if (dcoords[i]++)
		{
			diff = 1 << Log2(dcoords[i]);
			if (diff != dcoords[i])
			{
				ok = 0;
				break;
			}
			if (odiff && odiff != diff)
			{
				ok = 0;
				break;
			}
			odiff = diff;
		}
	}
	if (0 == vol)
	{
		ok = 0;
	}
	q->solid_ = ok;
	if (ok)
	{
		q->dhighKey_ = bitKey_toLong(&q->highKey_);
	}

#if 0
	/* gnuplot line compatible output*/
	elog(INFO, "%d %d",lcoords[0], lcoords[2]);
	elog(INFO, "%d %d",lcoords[0], hcoords[2]);
	elog(INFO, "%d %d",hcoords[0], hcoords[2]);
	elog(INFO, "%d %d",hcoords[0], lcoords[2]);
	elog(INFO, "%d %d %d %d %d %llu %c",lcoords[0], lcoords[2], dcoords[0], dcoords[1], dcoords[2], vol, ok?'*':' ');
	elog(INFO, "");
#endif
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
spt_query2_moveFirst(spt_query2_t *q, uint32 *coords, ItemPointerData *iptr)
{
	Assert(q && coords && iptr);

	q->queryHead_ = spt_query2_createQuery (q);
	q->queryHead_->prevQuery_ = NULL;
	q->queryHead_->curBitNum_ = ((32 * q->ncoords_) - 1);

	bitKey_fromCoords(&q->queryHead_->lowKey_, q->min_point_, ZKEY_MAX_COORDS); 
	bitKey_fromCoords(&q->queryHead_->highKey_, q->max_point_, ZKEY_MAX_COORDS);

	spt_query2_testSolidity(q->queryHead_);

	return spt_query2_findNextMatch(q, coords, iptr);
}

/* PUBLIC, main loop iteration, returns not 0 in case of cuccess, resulting data in x,y,...,iptr */
int
spt_query2_moveNext (spt_query2_t *q, uint32 *coords, ItemPointerData *iptr)
{
	Assert(q && coords && iptr);
	/*if all finished just go out*/
	if (!zcurve_scan_ctx_is_opened(&q->qctx_))
	{
		return 0;
	}
	/* current subquery is finished, let's get a new one*/
	if (q->subQueryFinished_)
	{
    		return spt_query2_findNextMatch(q, coords, iptr);
	}

	for (;;)
	{
		if (q->queryHead_->solid_)
		{
			/* are there some interesting data in the index tree? */
			if (!spt_query2_queryNextKey(q))
			{
				/* query diapason is exhausted, no more data, finished */
				spt_query2_closeQuery(q);
				return 0;
			}

			if (!spt_query2_testRawKey(q))
				break;

			*iptr = q->iptr_;
			return 1;
		}
		else
		{
			/* when the upper bound of subquery is equal to the current page last val, we need some additional testing */
			if (!spt_query2_checkNextPage(q))
				break;

			/* are there some interesting data in the index tree? */
			if (!spt_query2_queryNextKey(q))
			{
				/* query diapason is exhausted, no more data, finished */
				spt_query2_closeQuery(q);
				return 0;
			}

			/* up to the lookup diapason end */
			if (bitKey_cmp(&q->currentKey_, &q->queryHead_->highKey_) > 0)
				break;
	
			/* test if current key in lookup extent */
			if (spt_query2_checkKey(q, coords))
			{
				/* if yes, returning success */
				*iptr = q->iptr_;
				return 1;
			}
		}
	}

	/* subquery finished, start the next one from the queue */
	spt_query2_releaseSubQuery(q);
	return spt_query2_findNextMatch(q, coords, iptr);
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
		if (bitKey_cmp(&q->qctx_.next_val_, &q->queryHead_->highKey_) > 0) 
			return 0;
		return 1;
	}
	return 1;
}

/* reads next key and comares it with hikey datum, for solid queries only, optimisation */
int
spt_query2_testRawKey(spt_query2_t *q)
{
	int cmp = DatumGetInt32(
		DirectFunctionCall2(
			numeric_cmp,
			q->qctx_.raw_val_,
			q->queryHead_->dhighKey_));
	return cmp <= 0 ? 1 : 0;
}


/* 
   gets an subquery from queue, split it if necessary 
   till the full satisfaction and then test for an appropriate data
 */
int
spt_query2_findNextMatch(spt_query2_t *q, uint32 *coords, ItemPointerData *iptr)
{
	Assert(q && coords && iptr);
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
		while (0 == q->queryHead_->solid_ && 
			bitKey_cmp(&q->lastKey_, &q->queryHead_->highKey_) < 0)
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

			spt_query2_testSolidity(subQuery);
			spt_query2_testSolidity(q->queryHead_);

			q->queryHead_ = subQuery;
			q->subQueryFinished_ = 0;
		}

		for (;;)
		{
			if (q->queryHead_->solid_)
			{
				if (!spt_query2_testRawKey(q))
					break;

				*iptr = q->iptr_;
				return 1;
			}
			else
			{
				if (bitKey_cmp(&q->currentKey_, &q->queryHead_->highKey_) > 0)
					break;

				if (spt_query2_checkKey(q, coords))
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
spt_query2_checkKey (spt_query2_t *q, uint32 *coords)
{
	const bitKey_t *lKey = &q->queryHead_->lowKey_;
	const bitKey_t *hKey = &q->queryHead_->highKey_;

	/* let's try our key is positioned in necessary diapason */
	if (0 == q->queryHead_->solid_ && 
	    0 == bitKey_between(&q->currentKey_, lKey, hKey))
		return 0;

	/* OK, return data */
	Assert(coords);
	bitKey_toCoords (&q->currentKey_, coords, ZKEY_MAX_COORDS);
	return 1;
}

/* performs index cursor lookup for start_val */
int
spt_query2_queryFind (spt_query2_t *q, const bitKey_t *start_val)
{
	int ret = zcurve_scan_move_first(&q->qctx_, start_val, q->queryHead_->solid_);
	Assert(q && start_val);
	q->currentKey_ = q->qctx_.cur_val_;
	q->lastKey_ = q->qctx_.last_page_val_;
	q->iptr_ = q->qctx_.iptr_;
	return ret;
}

/* moves cursor forward */
int
spt_query2_queryNextKey (spt_query2_t *q)
{
	int ret = zcurve_scan_move_next(&q->qctx_, q->queryHead_->solid_);
	Assert(q);
	q->currentKey_ = q->qctx_.cur_val_;
	q->lastKey_ = q->qctx_.last_page_val_;
	q->iptr_ = q->qctx_.iptr_;
	return ret;
}


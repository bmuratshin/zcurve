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
 * contrib/zcurve/sp_tree.h
 *
 *
 * sp_tree.h -- low level operations with numbers and 2D ZCurve index realized as a regular btree
 *		
 *
 * Author:	Boris Muratshin, mailto:bmuratshin@gmail.com
 *
 */
#ifndef __ZCURVE_SP_TREE_H
#define __ZCURVE_SP_TREE_H

#include "bitkey.h"

/* the definition struct for zcurve subqery cursor */
typedef struct zcurve_scan_ctx_s {
	Relation 	rel_;		/* index tree */
	bitKey_t	init_zv_;       /* start value for lookup */
	Buffer		buf_;		/* currentle holded buffer (don't forget to call zcurve_scan_ctx_DTOR) */
	OffsetNumber	offset_;	/* cursor position in the holded page */
	OffsetNumber	max_offset_;	/* page size in items */
	ScanKeyData 	skey_;		/* initial key */

	bitKey_t 	cur_val_;	/* current value of cursor */
	bitKey_t	next_val_;	/* forward value of cursor for some special cases */
	bitKey_t	last_page_val_;	/* last value on the current page */
	ItemPointerData iptr_;		/* table row pointer from the current cursor position */

	Datum		raw_val_;

	BTStack		pstack_;	/* intermediate pages stack to the current page, need for possible interpages step */
} zcurve_scan_ctx_t;


/* 
   analog of _bt_search from src\backend\access\nbtree\nbtsearch.c 
   with some 2d zcurve specific
   returns 0 if error occured
*/
extern int zcurve_search_2d(zcurve_scan_ctx_t *pctx);

/* 
   analog of _bt_binsearch from src\backend\access\nbtree\nbtsearch.c 
   with some 2d zcurve specific
*/
extern OffsetNumber zcurve_binsrch_2d(zcurve_scan_ctx_t *pctx);

/* 
   analog of _bt_compare from src\backend\access\nbtree\nbtsearch.c 
   with some 2d zcurve specific
*/
extern int32 zcurve_compare_2d(zcurve_scan_ctx_t *pctx, Page page, OffsetNumber offnum);

/* context constructor */
extern int zcurve_scan_ctx_CTOR(zcurve_scan_ctx_t *ctx, Relation rel, bitkey_type ktype);

/* context destructor */
extern int zcurve_scan_ctx_DTOR(zcurve_scan_ctx_t *ctx);

/* starting cursor, it may be resterted with new value without calling destructor */
extern int zcurve_scan_move_first(zcurve_scan_ctx_t *ctx, const bitKey_t *start_val, bool raw);

/* cursor forward moving*/
extern int zcurve_scan_move_next(zcurve_scan_ctx_t *ctx, bool raw);

/* testing next value on the folowing page, cursor preserves its position */
extern int zcurve_scan_try_move_next(zcurve_scan_ctx_t *ctx, const bitKey_t *check_val);

/* testing for cursor is active */
extern int zcurve_scan_ctx_is_opened(zcurve_scan_ctx_t *ctx);

#endif  /*__ZCURVE_SP_TREE_H*/

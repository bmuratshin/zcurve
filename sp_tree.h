/*
 * contrib/zcurve/sp_tree.h
 *
 *
 * sp_tree.h -- low level operations with numbers and 2D ZCurve index realized as a regular btree
 *		
 *
 * Modified by Boris Muratshin, mailto:bmuratshin@gmail.com
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
extern int zcurve_scan_ctx_CTOR(zcurve_scan_ctx_t *ctx, Relation rel, int ncoords);

/* context destructor */
extern int zcurve_scan_ctx_DTOR(zcurve_scan_ctx_t *ctx);

/* starting cursor, it may be resterted with new value without calling destructor */
extern int zcurve_scan_move_first(zcurve_scan_ctx_t *ctx, const bitKey_t *start_val);

/* cursor forward moving*/
extern int zcurve_scan_move_next(zcurve_scan_ctx_t *ctx);

/* testing next value on the folowing page, cursor preserves its position */
extern int zcurve_scan_try_move_next(zcurve_scan_ctx_t *ctx, const bitKey_t *check_val);

/* testing for cursor is active */
extern int zcurve_scan_ctx_is_opened(zcurve_scan_ctx_t *ctx);

#endif  /*__ZCURVE_SP_TREE_H*/

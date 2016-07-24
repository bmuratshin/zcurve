/*
 * contrib/zcurve/list_sort.h
 *
 *
 * list_sort.h -- a kind of mergesort for a generic list 
 *		
 *
 * Modified by Boris Muratshin, mailto:bmuratshin@gmail.com
 */

#ifndef __ZCURVE_LIST_SORT_H
#define __ZCURVE_LIST_SORT_H

#include "gen_list.h"

extern gen_list_t *
list_sort (
  gen_list_t * list,
  int (* compare_proc)(const void *, const void *, const void *), 
  const void *arg  );

#endif /*__ZCURVE_LIST_SORT_H*/
#ifndef __LIST_SORT_H
#define __LIST_SORT_H

#include "gen_list.h"

extern gen_list_t *
list_sort (
  gen_list_t * list,
  int (* compare_proc)(const void *, const void *, const void *), 
  const void *arg  );

#endif
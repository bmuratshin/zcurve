#include "postgres.h"
#include "list_sort.h"

static gen_list_t *
IntersectSorted(
  gen_list_t *pList1, 
  gen_list_t *pList2,
  int (* compare_proc)(const void *, const void *, const void *), 
  const void *arg)
{
  gen_list_t *pCurItem, *res;
  gen_list_t *p1, *p2;
  int cmp = 0;
  
  p1 = pList1;
  p2 = pList2;

  cmp = compare_proc (p1->data, p2->data, arg);
  if (cmp < 0)
    {
      pCurItem = p1;
      p1 = p1->next;
    }
  else 
    {
      pCurItem = p2;
      p2 = p2->next;
    }
  res = pCurItem;
  while (NULL != p1 && NULL != p2) 
    {
      cmp = compare_proc (p1->data, p2->data, arg);
      if (cmp < 0)
        {
          pCurItem->next = p1;
          pCurItem = p1;
          p1 = p1->next;
        }
      else 
        {
          pCurItem->next = p2;
          pCurItem = p2;
          p2 = p2->next;
        }
    }
  pCurItem->next = (p1)? p1 : p2;
  return res;
}

struct   SortStackItem {
    int		Level;
    gen_list_t *Item;
};


#define MAX_SORT_STACK 32
gen_list_t *
list_sort (
  gen_list_t *List,
  int (* compare_proc)(const void *, const void *, const void *), 
  const void *arg  )
{
  struct SortStackItem Stack[MAX_SORT_STACK];
  int StackPos = 0;
  gen_list_t *p = List;
  
  while (NULL != p)
  {
    Stack[StackPos].Level = 1;
    Stack[StackPos].Item = p;
    p = p->next;
    Stack[StackPos].Item->next = NULL;
    StackPos++;
    Assert (StackPos<MAX_SORT_STACK);
    while (StackPos > 1 && Stack[StackPos - 1].Level == Stack[StackPos - 2].Level) 
    {
      Stack[StackPos - 2].Item = IntersectSorted(Stack[StackPos - 2].Item, Stack[StackPos - 1].Item, compare_proc, arg);
      Stack[StackPos - 2].Level++;
      StackPos--;
      Assert (StackPos>=0);
    }
  }
  while (StackPos > 1)
  {
    Stack[StackPos - 2].Item = IntersectSorted(Stack[StackPos - 2].Item, Stack[StackPos - 1].Item, compare_proc, arg);
    Stack[StackPos - 2].Level++;
    StackPos--;
    Assert (StackPos>=0);
  }
  if (StackPos > 0)
    List = Stack[0].Item;
  return List;
}

/*
 * contrib/zcurve/list_sort.c
 *
 *
 * list_sort.c -- a kind of mergesort for a generic list 
 *		
 *
 * Modified by Boris Muratshin, mailto:bmuratshin@gmail.com
 */
#include "postgres.h"
#include "list_sort.h"

static gen_list_t *
intersect_sorted(
	gen_list_t * plist1,
	gen_list_t * plist2,
	int (*compare_proc) (const void *, const void *, const void *), 
	const void *arg)
{
	gen_list_t *pcur_item, *res;
	gen_list_t *p1, *p2;
	int cmp = 0;

	p1 = plist1;
	p2 = plist2;

	cmp = compare_proc (p1->data, p2->data, arg);
	if (cmp < 0)
	{
		pcur_item = p1;
		p1 = p1->next;
	}
	else
	{
		pcur_item = p2;
		p2 = p2->next;
	}
	res = pcur_item;
	while (NULL != p1 && NULL != p2)
	{
		cmp = compare_proc (p1->data, p2->data, arg);
		if (cmp < 0)
		{
			pcur_item->next = p1;
			pcur_item = p1;
			p1 = p1->next;
		}
		else
		{
			pcur_item->next = p2;
			pcur_item = p2;
			p2 = p2->next;
		}
	}
	pcur_item->next = (p1) ? p1 : p2;
	return res;
}

typedef struct sort_stack_item_s
{
	int level_;
	gen_list_t *item_;
} sort_stack_item_t;


#define MAX_SORT_STACK 32
gen_list_t *
list_sort (
	gen_list_t * list,
	int (*compare_proc) (const void *, const void *, const void *),
	const void *arg)
{
	sort_stack_item_t stack[MAX_SORT_STACK];
	int stack_pos = 0;
	gen_list_t *p = list;

	while (NULL != p)
	{
		stack[stack_pos].level_ = 1;
		stack[stack_pos].item_ = p;
		p = p->next;
		stack[stack_pos].item_->next = NULL;
		stack_pos++;
		Assert (stack_pos < MAX_SORT_STACK);
		while (stack_pos > 1
			&& stack[stack_pos - 1].level_ == stack[stack_pos - 2].level_)
		{
			stack[stack_pos - 2].item_ =
				intersect_sorted(
					stack[stack_pos - 2].item_,
					stack[stack_pos - 1].item_, 
					compare_proc, 
					arg);
			stack[stack_pos - 2].level_++;
			stack_pos--;
			Assert (stack_pos >= 0);
		}
	}
	while (stack_pos > 1)
	{
		stack[stack_pos - 2].item_ =
			intersect_sorted(
				stack[stack_pos - 2].item_, 
				stack[stack_pos - 1].item_,
				compare_proc, 
				arg);
		stack[stack_pos - 2].level_++;
		stack_pos--;
		Assert(stack_pos >= 0);
	}
	if (stack_pos > 0)
		list = stack[0].item_;
	return list;
}

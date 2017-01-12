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
 * contrib/zcurve/list_sort.c
 *
 *
 * list_sort.c -- a kind of mergesort for a generic list 
 *		
 *
 * Author:	Boris Muratshin, mailto:bmuratshin@gmail.com
 *
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

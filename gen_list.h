/*
 * contrib/zcurve/gen_list.h
 *
 *
 * gen_list.h -- generic list declaration
 *		
 *
 * Modified by Boris Muratshin, mailto:bmuratshin@gmail.com
 */

#ifndef __ZCURVE_GEN_LIST_H
#define __ZCURVE_GEN_LIST_H

typedef struct gen_list_s {
	struct gen_list_s *next;
	void		  *data;
} gen_list_t;

#endif /*__ZCURVE_GEN_LIST_H*/
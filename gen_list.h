#ifndef __GEN_LIST_H
#define __GEN_LIST_H


struct gen_list_s {
	struct gen_list_s *next;
	void		  *data;
};
typedef struct gen_list_s gen_list_t;


#endif
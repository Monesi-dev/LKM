#ifndef __EX_THREAD_H__
#define __EX_THREAD_H__
int thread_create(void);
void thread_destroy(void);
int get_data(char *p, int size);
int write_data(const char * buf, int size);
extern struct completion available_data;
#endif	/* __EX_THREAD_H__ */

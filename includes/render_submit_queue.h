#ifndef RENDERSUBQUEUE_H
#define RENDERSUBQUEUE_H
#ifdef __cplusplus
extern "C" {
#endif


int work_complete;

void enqueue(const char *xmlname, int x, int y, int z);
void spawn_workers(int num, const char *socketpath, int maxLoad);
void wait_for_empty_queue(void);
void finish_workers(void);
void print_statistics(void);

#ifdef __cplusplus
}
#endif

#endif

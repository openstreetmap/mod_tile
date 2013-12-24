#include <stdio.h>
#include <unistd.h>
#include <stddef.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <pthread.h>
#include <stdlib.h>
#include <sys/time.h>
#include <errno.h>

#include "render_submit_queue.h"
#include "sys_utils.h"
#include "protocol.h"
#include "protocol_helper.h"
#include "render_config.h"

#define QMAX 32

pthread_mutex_t qLock;
pthread_mutex_t qStatsLock;
static pthread_cond_t qCondNotEmpty;
static pthread_cond_t qCondNotFull;

static int maxLoad = 0;

static unsigned int qLen;
struct qItem {
    char *mapname;
    int x,y,z;
    struct qItem *next;
};

struct speed_stat {
    time_t time_min;
    time_t time_max;
    time_t time_total;
    int    noRendered;
};

struct speed_stats {
    struct speed_stat stat[MAX_ZOOM + 1];
};

struct speed_stats performance_stats;

static struct qItem *qHead, *qTail;

static int no_workers;
static pthread_t *workers;

static void check_load(void)
{
    double avg = get_load_avg();

    while (avg >= maxLoad) {
        /* printf("Load average %d, sleeping\n", avg); */
        sleep(5);
        avg = get_load_avg();
    }
}

static int process(struct protocol * cmd, int fd)
{
    struct timeval tim;
    time_t t1;
    time_t t2;
    int ret = 0;
    struct protocol rsp;

    gettimeofday(&tim, NULL);
    t1 = tim.tv_sec*1000+(tim.tv_usec/1000);

    //printf("Sending request\n");
    if (send_cmd(cmd, fd) < 1) {
        perror("send error");
    };

    //printf("Waiting for response\n");
    bzero(&rsp, sizeof(rsp));
    ret = recv_cmd(&rsp, fd,1);
    if (ret < 1) return 0;
    //printf("Got response %i\n", rsp.cmd);

    if (rsp.cmd != cmdDone)
    {
        printf("rendering failed with command %i, pausing.\n", rsp.cmd);
        sleep(10);
    } else {
        gettimeofday(&tim, NULL);
        t2 = tim.tv_sec*1000+(tim.tv_usec/1000);
        pthread_mutex_lock(&qStatsLock);
        t1 = t2 - t1;
        performance_stats.stat[cmd->z].noRendered++;
        performance_stats.stat[cmd->z].time_total += t1;
        if ((performance_stats.stat[cmd->z].time_min > t1) || (performance_stats.stat[cmd->z].time_min == 0))
            performance_stats.stat[cmd->z].time_min = t1;
        if (performance_stats.stat[cmd->z].time_max < t1)
            performance_stats.stat[cmd->z].time_max = t1;
        pthread_mutex_unlock(&qStatsLock);
    }

    if (!ret)
        perror("Socket send error");
    return ret;
}

static struct protocol * fetch(void) {
    struct protocol * cmd;
    pthread_mutex_lock(&qLock);

    while (qLen == 0) {
        if (work_complete) {
            pthread_mutex_unlock(&qLock);
            return NULL;
        }
        pthread_cond_wait(&qCondNotEmpty, &qLock);
    }

    // Fetch item from queue
    if (!qHead) {
        fprintf(stderr, "Queue failure, null qHead with %d items in list\n", qLen);
        exit(1);
    }
    cmd = malloc(sizeof(struct protocol));
    memset(cmd, 0, sizeof(struct protocol));

    cmd->ver = 2;
    cmd->cmd = cmdRenderBulk;
    cmd->z = qHead->z;
    cmd->x = qHead->x;
    cmd->y = qHead->y;
    strncpy(cmd->xmlname, qHead->mapname, XMLCONFIG_MAX - 1);

    if (qHead == qTail) {
        free(qHead->mapname);
        free(qHead);
        qHead = NULL;
        qTail = NULL;
        qLen = 0;
    } else {
        struct qItem *e = qHead;
        qHead = qHead->next;
        free(e->mapname);
        free(e);
        qLen--;
    }
    pthread_cond_signal(&qCondNotFull);
    pthread_mutex_unlock(&qLock);
    return cmd;
}

void enqueue(const char *xmlname, int x, int y, int z)
{
    // Add this path in the local render queue
    struct qItem *e = malloc(sizeof(struct qItem));

    e->mapname = strdup(xmlname);
    e->x = x;
    e->y = y;
    e->z = z;
    e->next = NULL;

    if (!e->mapname) {
        fprintf(stderr, "Malloc failure\n");
        exit(1);
    }

    pthread_mutex_lock(&qLock);

    while (qLen == QMAX)
    {
        int ret = pthread_cond_wait(&qCondNotFull, &qLock);
        if( ret != 0 ) {
            fprintf(stderr,"pthread_cond_wait(qCondNotFull): %s\n", strerror(ret));
        }
    }

    // Append item to end of queue
    if (qTail)
        qTail->next = e;
    else
        qHead = e;
    qTail = e;
    qLen++;
    pthread_cond_signal(&qCondNotEmpty);

    pthread_mutex_unlock(&qLock);
}


void *thread_main(void *arg)
{
    const char *spath = (const char *)arg;
    int fd;
    struct sockaddr_un addr;
    struct protocol * cmd;

    fd = socket(PF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) {
        fprintf(stderr, "failed to create unix socket\n");
        exit(2);
    }

    bzero(&addr, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, spath, sizeof(addr.sun_path) - 1);

    if (connect(fd, (struct sockaddr *) &addr, sizeof(addr)) < 0) {
        fprintf(stderr, "socket connect failed for: %s\n", spath);
        close(fd);
        return NULL;
    }

    while(1) {
        check_load();
        if (!(cmd = fetch())) break;
        process(cmd, fd);
        free(cmd);
    }

    close(fd);

    return NULL;
}

void spawn_workers(int num, const char *spath, int max_load)
{
    int i;
    no_workers = num;
    maxLoad = max_load;

    // Setup request queue
    pthread_mutex_init(&qLock, NULL);
    pthread_mutex_init(&qStatsLock, NULL);
    pthread_cond_init(&qCondNotEmpty, NULL);
    pthread_cond_init(&qCondNotFull, NULL);

    printf("Starting %d rendering threads\n", no_workers);
    workers = calloc(sizeof(pthread_t), no_workers);
    if (!workers) {
        perror("Error allocating worker memory");
        exit(1);
    }
    for(i=0; i<no_workers; i++) {
        if (pthread_create(&workers[i], NULL, thread_main, (void *)spath)) {
            perror("Thread creation failed");
            exit(1);
        }
    }
}

void print_statistics(void) {
    int i;
    printf("*****************************************************\n");
    for (i = 0; i <= MAX_ZOOM; i++) {
        if (performance_stats.stat[i].noRendered == 0) continue;
        printf("Zoom %02i: min: %4.1f    avg: %4.1f     max: %4.1f     over a total of %8.1fs in %i requests\n",
                i, performance_stats.stat[i].time_min / 1000.0, (performance_stats.stat[i].time_total /  performance_stats.stat[i].noRendered) / 1000.0,
                performance_stats.stat[i].time_max / 1000.0, performance_stats.stat[i].time_total / 1000.0, performance_stats.stat[i].noRendered);
    }
    printf("*****************************************************\n");
    printf("*****************************************************\n");
}

void wait_for_empty_queue() {

    pthread_mutex_lock(&qLock);
    while (qLen > 0) {
        pthread_cond_wait(&qCondNotFull, &qLock);
    }
    pthread_mutex_unlock(&qLock);
}

void finish_workers(void)
{
    int i;

    printf("Waiting for rendering threads to finish\n");
    pthread_mutex_lock(&qLock);
    work_complete = 1;
    pthread_mutex_unlock(&qLock);
    pthread_cond_broadcast(&qCondNotEmpty);

    for(i=0; i<no_workers; i++)
        pthread_join(workers[i], NULL);
    free(workers);
    workers = NULL;
}


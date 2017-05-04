#ifndef _VR_WORKER_H_
#define _VR_WORKER_H_
//worker 线程结构体
typedef struct vr_worker {
    //worker id
    int id;
    //
    vr_eventloop vel;
    //用来实现与master线程通信
    int socketpairs[2];         /*0: belong to master thread, 1: belong to myself*/
    //客户端的链接池
    dlist *csul;    /* Connect swap unit list */
    pthread_mutex_t csullock;   /* swap unit list locker */

    /* Some global state in order to continue the work incrementally 
       * across calls for activeExpireCycle() to expire some keys. */
    //当前的数据库
    unsigned int current_db;    /* Last DB tested. */
    //线程的运行时间
    int timelimit_exit;         /* Time limit hit in previous call? */
    //轮流的时间
    long long last_fast_cycle;  /* When last fast cycle ran. */

    /* We use global counters so if we stop the computation at a given
       * DB we'll be able to start from the successive in the next
       * cron loop iteration for databasesCron() to resize and reshash db. */
   
    unsigned int resize_db;
    unsigned int rehash_db;
}vr_worker;

//链接交换单元
struct connswapunit {
    //客户端的socket id
    int num;
    void *data;
    struct connswapunit *next;
};

//压缩数组的线程池
extern struct darray workers;

//worker线程api
//初始化
int workers_init(uint32_t worker_count);
//运行
int workers_run(void);
//等待
int workers_wait(void);
//析构
void workers_deinit(void);

//创建一个链接单元
struct connswapunit *csui_new(void);
//释放一个链接单元
void csui_free(struct connswapunit *item);
//压入一个链接
void csul_push(vr_worker *worker, struct connswapunit *su);
//弹出一个链接
struct connswapunit *csul_pop(vr_worker *worker);

//下一个线程
int worker_get_next_idx(int curidx);
//分配一个链接
void dispatch_conn_new(vr_listen *vlisten, int sd);
//worke线程进入epoll等待前的执行函数
void worker_before_sleep(struct aeEventLoop *eventLoop, void *private_data);
//worker线程的周期执行函数
int worker_cron(struct aeEventLoop *eventLoop, long long id, void *clientData);

#endif

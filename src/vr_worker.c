#include <vr_core.h>

/* Which thread we assigned a connection to most recently. */
static int last_worker_thread = -1;
static int num_worker_threads;

struct darray workers;

static void *worker_thread_run(void *args);

#define SU_PER_ALLOC 64

//空闲链接池
/* Free list of swapunit structs */
static struct connswapunit *csui_freelist;
static pthread_mutex_t csui_freelist_lock;

/*
 * 创建一个空闲链接池循环链表并且返回第一个
 * Returns a fresh connection connswapunit queue item.
 */
struct connswapunit *
csui_new(void) {
    struct connswapunit *item = NULL;
    //进入临界区
    pthread_mutex_lock(&csui_freelist_lock);
    if (csui_freelist) {
        item = csui_freelist;
        csui_freelist = item->next;
    }
    pthread_mutex_unlock(&csui_freelist_lock);

    if (NULL == item) {
        int i;

        /* Allocate a bunch of items at once to reduce fragmentation */
        item = dalloc(sizeof(struct connswapunit) * SU_PER_ALLOC);
        if (NULL == item) {
            return NULL;
        }

        /*
         * Link together all the new items except the first one
         * (which we'll return to the caller) for placement on
         * the freelist.
         */
        for (i = 2; i < SU_PER_ALLOC; i++)
            item[i - 1].next = &item[i];

        pthread_mutex_lock(&csui_freelist_lock);
        item[SU_PER_ALLOC - 1].next = csui_freelist;
        csui_freelist = &item[1];
        pthread_mutex_unlock(&csui_freelist_lock);
    }

    return item;
}

/*
 * 释放一个链接单元，其实就是将它插入空闲链接池进行复用
 * Frees a connection connswapunit queue item (adds it to the freelist.)
 */
void 
csui_free(struct connswapunit *item) {
    pthread_mutex_lock(&csui_freelist_lock);
    item->next = csui_freelist;
    csui_freelist = item;
    pthread_mutex_unlock(&csui_freelist_lock);
}

//向链接池压入新链接
void
csul_push(vr_worker *worker, struct connswapunit *su)
{
    pthread_mutex_lock(&worker->csullock);
    dlistPush(worker->csul, su);
    pthread_mutex_unlock(&worker->csullock);
}

//从链接池弹出一个链接
struct connswapunit *
csul_pop(vr_worker *worker)
{
    struct connswapunit *su = NULL;

    pthread_mutex_lock(&worker->csullock);
    su = dlistPop(worker->csul);
    pthread_mutex_unlock(&worker->csullock);
    
    return su;
}

//初始化worker线程
int
vr_worker_init(vr_worker *worker)
{
    rstatus_t status;
    int maxclients, threads_num;
    int filelimit;
    
    if (worker == NULL) {
        return VR_ERROR;
    }
    //初始化线程id，管道描述符
    worker->id = 0;
    worker->socketpairs[0] = -1;
    worker->socketpairs[1] = -1;
    //链接池
    worker->csul = NULL;
    
    pthread_mutex_init(&worker->csullock, NULL);
    worker->current_db = 0;
    worker->timelimit_exit = 0;
    worker->last_fast_cycle = 0;
    worker->resize_db = 0;
    worker->rehash_db = 0;
    //读取配置树得到最大的客户端个数
    conf_server_get(CONFIG_SOPN_MAXCLIENTS,&maxclients);
    //调整进程的最大打开的文件数目
    filelimit = adjustOpenFilesLimit(maxclients);
    if (filelimit <= 0) {
        return VR_ERROR;
    }
    //线程事件循环初始化
    vr_eventloop_init(&worker->vel, filelimit);
    //线程的执行函数
    worker->vel.thread.fun_run = worker_thread_run;
    //线程函数的参数
    worker->vel.thread.data = worker;
    //命令状态表
    worker->vel.cstable = commandStatsTableCreate();
    //初始化双向管道
    status = socketpair(AF_LOCAL, SOCK_STREAM, 0, worker->socketpairs);
    if (status < 0) {
        log_error("create socketpairs failed: %s", strerror(errno));
        return VR_ERROR;
    }
    //set the pipe no block
    status = vr_set_nonblocking(worker->socketpairs[0]);
    if (status < 0) {
        log_error("set socketpairs[0] %d nonblocking failed: %s", 
            worker->socketpairs[0], strerror(errno));
        close(worker->socketpairs[0]);
        worker->socketpairs[0] = -1;
        close(worker->socketpairs[1]);
        worker->socketpairs[1] = -1;
        return VR_ERROR;
    }
    status = vr_set_nonblocking(worker->socketpairs[1]);
    if (status < 0) {
        log_error("set socketpairs[1] %d nonblocking failed: %s", 
            worker->socketpairs[1], strerror(errno));
        close(worker->socketpairs[0]);
        worker->socketpairs[0] = -1;
        close(worker->socketpairs[1]);
        worker->socketpairs[1] = -1;
        return VR_ERROR;
    }
    //创建链接池
    worker->csul = dlistCreate();
    if (worker->csul == NULL) {
        log_error("create list failed: out of memory");
        return VR_ENOMEM;
    }
    
    return VR_OK;
}

//析构一个线程
void
vr_worker_deinit(vr_worker *worker)
{
    if (worker == NULL) {
        return;
    }

    vr_eventloop_deinit(&worker->vel);
//关闭管道
    if (worker->socketpairs[0] > 0){
        close(worker->socketpairs[0]);
        worker->socketpairs[0] = -1;
    }
    if (worker->socketpairs[1] > 0){
        close(worker->socketpairs[1]);
        worker->socketpairs[1] = -1;
    }
    //释放连接池
    if (worker->csul != NULL) {
        dlistRelease(worker->csul);
        worker->csul = NULL;
    }
}

//得到下一个运行的线程id
int
worker_get_next_idx(int curidx)
{
    int idx = curidx + 1;
    return idx>=num_worker_threads?0:idx;
}

//分发链接到线程
void
dispatch_conn_new(vr_listen *vlisten, int sd)
{
    //创建一个新的交换单元
    struct connswapunit *su = csui_new();
    char buf[1];
    vr_worker *worker;

    if (su == NULL) {
        close(sd);
        /* given that malloc failed this may also fail, but let's try */
        log_error("Failed to allocate memory for connection swap object\n");
        return ;
    }
    //轮训得到对应的线程
    //得到线程
    int tid = (last_worker_thread + 1) % num_worker_threads;
    worker = darray_get(&workers, (uint32_t)tid);

    //记录上次的线程id
    last_worker_thread = tid;

    //初始化链接单元、
    /*
    //网络库
    //主机名端口号文件描述符
    //typedef struct vr_listen {
    //    sds name;               /* hostname:port */
    //    int port;               /* port */
    //    mode_t perm;            /* socket permissions */
    //    struct sockinfo info;   /* listen socket info */
    //    int sd;                 /* socket descriptor */
    //}vr_listen;

    */
    su->num = sd;
    su->data = vlisten;

    //压入链接池
    csul_push(worker, su);

    //向管道中写入c通知worker有链接压入链接池
    buf[0] = 'c';
    if (vr_write(worker->socketpairs[0], buf, 1) != 1) {
        log_error("Notice the worker failed.");
    }
    //增加全局配置
    update_curr_clients_add(1);
}

//线程事件循环
static void
thread_event_process(aeEventLoop *el, int fd, void *privdata, int mask) {

    rstatus_t status;
    //得到对应的线程
    vr_worker *worker = privdata;
    char buf[1];
    int sd;
    //tcp结构
    vr_listen *vlisten;
    struct conn *conn;
    struct connswapunit *csu;
    client *c;

    ASSERT(el == worker->vel.el);
    ASSERT(fd == worker->socketpairs[1]);
    //从管道中读取一个字符
    if (vr_read(fd, buf, 1) != 1) {
        log_warn("Can't read for worker(id:%d) socketpairs[1](%d)", 
            worker->vel.thread.id, fd);
        buf[0] = 'c';
    }
    //根据对应的master的通知执行对应的状态处理机
    switch (buf[0]) {
        //c代表有链接压入链接池
    case 'c':
        //弹出对应的链接单元
        csu = csul_pop(worker);
        if (csu == NULL) {
            return;
        }
        //得到客户端的socket fd
        sd = csu->num;
        //得到底层传输结构
        vlisten = csu->data;
        //释放对应的链接单元 
        csui_free(csu);
        conn = conn_get(worker->vel.cb);
        if (conn == NULL) {
            log_error("get conn for c %d failed: %s", 
                sd, strerror(errno));
            status = close(sd);
            if (status < 0) {
                log_error("close c %d failed, ignored: %s", sd, strerror(errno));
            }
            return;
        }
        conn->sd = sd;
    
        status = vr_set_nonblocking(conn->sd);
        if (status < 0) {
            log_error("set nonblock on c %d failed: %s", 
                conn->sd, strerror(errno));
            conn_put(conn);
            return;
        }
    //设置tcp的传输算法
        if (vlisten->info.family == AF_INET || vlisten->info.family == AF_INET6) {
            status = vr_set_tcpnodelay(conn->sd);
            if (status < 0) {
                log_warn("set tcpnodelay on c %d failed, ignored: %s",
                    conn->sd, strerror(errno));
            }
        }
    //创建客户端
        c = createClient(&worker->vel, conn);
        if (c == NULL) {
            log_error("Create client failed");
            conn_put(conn);
            return;
        }
        //得到当前的线程id
        c->curidx = worker->id;
        //创建对应的客户端读写的事件
        status = aeCreateFileEvent(worker->vel.el, conn->sd, AE_READABLE, 
            readQueryFromClient, c);
        if (status == AE_ERR) {
            log_error("Unrecoverable error creating worker ipfd file event.");
            return;
        }

        update_stats_add(c->vel->stats, numconnections, 1);
        
        break;
//线程之前跳转
    case 'j':
        //pop出一个链接
        csu = csul_pop(worker);
        if (csu == NULL) {
            return;
        }
        //此时data存放客户端
        c = csu->data;
        //释放对应的链接单元
        csui_free(csu);
        c->vel = &worker->vel;
        c->curidx = worker->id;
        c->steps ++;
        c->cmd->proc(c);
        
        if (c->flags&CLIENT_JUMP) {
            dispatch_conn_exist(c,c->taridx);
        } else {
            resetClient(c);
            linkClientToEventloop(c,c->vel);
        }
        break;
    default:
        log_error("read error char '%c' for worker(id:%d) socketpairs[1](%d)", 
            buf[0], worker->vel.thread.id, worker->socketpairs[1]);
        break;
    }
}
//安装worker线程
static int
setup_worker(vr_worker *worker)
{
    rstatus_t status;
    //创建对应的管道读事件
    status = aeCreateFileEvent(worker->vel.el, worker->socketpairs[1], AE_READABLE, 
        thread_event_process, worker);
    if (status == AE_ERR) {
        log_error("Unrecoverable error creating worker ipfd file event.");
        return VR_ERROR;
    }

    aeSetBeforeSleepProc(worker->vel.el, worker_before_sleep, worker);

    /* Create the serverCron() time event, that's our main way to process
     * background operations. */
    //创建循环事件
    if(aeCreateTimeEvent(worker->vel.el, 1, worker_cron, worker, NULL) == AE_ERR) {
        serverPanic("Can't create the serverCron time event.");
        return VR_ERROR;
    }
    
    return VR_OK;
}
//线程的运行函数
static void *
worker_thread_run(void *args)
{
    vr_worker *worker = args;
    
    /* vire worker run */
    aeMain(worker->vel.el);

    return NULL;
}
//线程初始化 先初始化 再安装
int
workers_init(uint32_t worker_count)
{
    rstatus_t status;
    uint32_t idx;
    vr_worker *worker;
    
    csui_freelist = NULL;
    pthread_mutex_init(&csui_freelist_lock, NULL);

    darray_init(&workers, worker_count, sizeof(vr_worker));

    for (idx = 0; idx < worker_count; idx ++) {
        worker = darray_push(&workers);
        vr_worker_init(worker);
        worker->id = idx;
        status = setup_worker(worker);
        if (status != VR_OK) {
            exit(1);
        }
    }
    
    num_worker_threads = (int)darray_n(&workers);

    return VR_OK;
}
//线程开始运行
int
workers_run(void)
{
    uint32_t i, thread_count;
    vr_worker *worker;

    thread_count = (uint32_t)num_worker_threads;

    for (i = 0; i < thread_count; i ++) {
        worker = darray_get(&workers, i);
        vr_thread_start(&worker->vel.thread);
    }

    return VR_OK;
}
//线程等待退出
int
workers_wait(void)
{
    uint32_t i, thread_count;
    vr_worker *worker;

    thread_count = (uint32_t)num_worker_threads;

    for (i = 0; i < thread_count; i ++) {
        worker = darray_get(&workers, i);
        pthread_join(worker->vel.thread.thread_id, NULL);
    }

    return VR_OK;
}
//线程析构
void
workers_deinit(void)
{
    vr_worker *worker;

    while(darray_n(&workers)) {
        worker = darray_pop(&workers);
		vr_worker_deinit(worker);
    }
}

/* This function gets called every time Redis is entering the
 * main loop of the event driven library, that is, before to sleep
 * for ready file descriptors. */
//进入epoll之前的函数
void
worker_before_sleep(struct aeEventLoop *eventLoop, void *private_data) {
    vr_worker *worker = private_data;

    UNUSED(eventLoop);
    UNUSED(private_data);

    ASSERT(eventLoop == worker->vel.el);

    /* Handle writes with pending output buffers. */
    handleClientsWithPendingWrites(&worker->vel);

    //activeExpireCycle(worker, ACTIVE_EXPIRE_CYCLE_FAST);
}
//周期函数
int
worker_cron(struct aeEventLoop *eventLoop, long long id, void *clientData) {
    vr_worker *worker = clientData;
    vr_eventloop *vel = &worker->vel;
    size_t stat_used_memory, stats_peak_memory;

    UNUSED(eventLoop);
    UNUSED(id);
    UNUSED(clientData);

    ASSERT(eventLoop == vel->el);

    vel->unixtime = time(NULL);
    vel->mstime = vr_msec_now();

    run_with_period(100, vel->cronloops) {
        long long stats_value;
        update_stats_get(vel->stats,numcommands,&stats_value);
        trackInstantaneousMetric(vel->stats,STATS_METRIC_COMMAND,stats_value);
        update_stats_get(vel->stats,net_input_bytes,&stats_value);
        trackInstantaneousMetric(vel->stats,STATS_METRIC_NET_INPUT,stats_value);
        update_stats_get(vel->stats,net_output_bytes,&stats_value);
        trackInstantaneousMetric(vel->stats,STATS_METRIC_NET_OUTPUT,stats_value);
    }

    /* Sample the RSS here since this is a relatively slow call. */
    run_with_period(1000, vel->cronloops) {
        vel->resident_set_size = dalloc_get_rss();
    }

    /* Record the max memory used since the server was started. */
    /*stat_used_memory = dalloc_used_memory();
    update_stats_get(vel->stats, peak_memory, &stats_peak_memory);
    if (stat_used_memory > stats_peak_memory) {
        update_stats_set(vel->stats, peak_memory, stat_used_memory);
    }*/

    /* Close clients that need to be closed asynchronous */
    freeClientsInAsyncFreeQueue(vel);

    //databasesCron(worker);

    /* Update the config cache */
    run_with_period(1000, vel->cronloops) {
        conf_cache_update(&vel->cc);
    }
    
    vel->cronloops ++;
    return 1000/vel->hz;
}

#include <vr_core.h>

vr_master master;

static int setup_master(void);
static void *master_thread_run(void *args);

int
master_init(vr_conf *conf)
{
    rstatus_t status;
    uint32_t j;
    sds *host, listen_str;
    //创建listen
    vr_listen **vlisten;
    //线程数目
    int threads_num;
    //文件数目限制
    int filelimit;

    //主线程的链接交换单元
    master.cbsul = NULL;
    pthread_mutex_init(&master.cbsullock, NULL);
    //得到配置
    conf_server_get(CONFIG_SOPN_THREADS,&threads_num);
    //文件限制
    filelimit = threads_num*2+CONFIG_MIN_RESERVED_FDS;
    //初始化事件
    vr_eventloop_init(&master.vel,filelimit);
    //设置master线程的运行函数
    master.vel.thread.fun_run = master_thread_run;
    //初始化n个监听链接的结构
    darray_init(&master.listens,darray_n(&cserver->binds),sizeof(vr_listen*));

    //得到每一个监听结构的内容
    for (j = 0; j < darray_n(&cserver->binds); j ++) {
        //得到绑定的主机名
        host = darray_get(&cserver->binds,j);
        //得到接收链接的地址
        listen_str = sdsdup(*host);
        //将端口号附加进主机名
        listen_str = sdscatfmt(listen_str, ":%i", cserver->port);
        //初始化监听链接的链接结构体
        vlisten = darray_push(&master.listens);
        *vlisten = vr_listen_create(listen_str);
        
        if (*vlisten == NULL) {
            darray_pop(&master.listens);
            log_error("Create listen %s failed", listen_str);
            sdsfree(listen_str);
            return VR_ERROR;
        }
        sdsfree(listen_str);
    }
    //开始监听
    for (j = 0; j < darray_n(&master.listens); j ++) {
        vlisten = darray_get(&master.listens, j);
        //到这里时才将监听的fd赋值给vlisten
        status = vr_listen_begin(*vlisten);
        if (status != VR_OK) {
            log_error("Begin listen to %s failed", (*vlisten)->name);
            return VR_ERROR;
        }
    }
    //创建链接交换队列
    master.cbsul = dlistCreate();
    if (master.cbsul == NULL) {
        log_error("Create list failed: out of memory");
        return VR_ENOMEM;
    }

    setup_master();

    return VR_OK;
}

void
master_deinit(void)
{
    vr_listen **vlisten;
    
    vr_eventloop_deinit(&master.vel);

    while (darray_n(&master.listens) > 0) {
        vlisten = darray_pop(&master.listens);
        vr_listen_destroy(*vlisten);
    }
    darray_deinit(&master.listens);
    
}

//接收链接：分发到worker
static void
client_accept(aeEventLoop *el, int fd, void *privdata, int mask) {
    int sd;
    vr_listen *vlisten = privdata;
    //循环从系统的三次握手成功的队列中拿到链接分发到worker线程，每次给不同的线程，然后向其管道写入c
    while((sd = vr_listen_accept(vlisten)) > 0) {
        dispatch_conn_new(vlisten, sd);
    }
}

//向master的队列加入链接单元
static void
cbsul_push(struct connswapunit *su)
{
    pthread_mutex_lock(&master.cbsullock);
    dlistPush(master.cbsul, su);
    pthread_mutex_unlock(&master.cbsullock);
}
//从对应的链接交换单元队列中弹出一个链接
static struct connswapunit *
cbsul_pop(void)
{
    struct connswapunit *su = NULL;

    pthread_mutex_lock(&master.cbsullock);
    su = dlistPop(master.cbsul);
    pthread_mutex_unlock(&master.cbsullock);
    
    return su;
}
//切换线程
void
dispatch_conn_exist(client *c, int tid)
{
    struct connswapunit *su = csui_new();
    char buf[1];
    vr_worker *worker;

    if (su == NULL) {
        freeClient(c);
        /* given that malloc failed this may also fail, but let's try */
        log_error("Failed to allocate memory for connection swap object\n");
        return ;
    }

    su->num = tid;
    su->data = c;
   //移除客户端 
    unlinkClientFromEventloop(c);
//压入master线程的交换队列
    cbsul_push(su);

    //得到一个worker线程
    worker = darray_get(&workers, (uint32_t)c->curidx);

    /* Back to master */
    //向对应的管道写入b
    buf[0] = 'b';
    if (vr_write(worker->socketpairs[1], buf, 1) != 1) {
        log_error("Notice the worker failed.");
    }
}
//master的业务逻辑：只有一个逻辑会用到这个事件就是当worker线程要跳转时会向master写入b，master选择一个新的worker传递对应的客户端过去
//同时向该worker线程写入一个 j
//这里就要说下交换队列的设计思路
///链接交换单元：num是客户端的连接，而data用来在worker和master之间进行交换数据，
//这里没有锁操作，原因是什么呢，这里借鉴了go的设计思路之一：用通信解决共享而不是用共享解决通信
//struct connswapunit {
    //客户端的socket id
//    int num;
//    void *data;
//    struct connswapunit *next;
//};
static void
thread_event_process(aeEventLoop *el, int fd, void *privdata, int mask) {

    rstatus_t status;
    vr_worker *worker = privdata;
    char buf[1];
    int idx;
    client *c;
    struct connswapunit *su;

    ASSERT(el == master.vel.el);
    ASSERT(fd == worker->socketpairs[0]);
//读取管道
    if (vr_read(fd, buf, 1) != 1) {
        log_warn("Can't read for worker(id:%d) socketpairs[1](%d)", 
            worker->vel.thread.id, fd);
        buf[0] = 'b';
    }
    
    switch (buf[0]) {
    //切换线程
    case 'b':
        //从交换队列里面弹出一个链接
        su = cbsul_pop();
        if (su == NULL) {
            log_warn("Pop from connection back swap list is null");
            return;
        }
        
        idx = su->num;
        su->num = worker->id;
        //得到交换单元里面的id对应的线程
        worker = darray_get(&workers, (uint32_t)idx);
        //将被交换的空闲线程压入交换单元队列
        csul_push(worker, su);

        /* Jump to the target worker. */
        //向目标线程写入j
        buf[0] = 'j';
        if (vr_write(worker->socketpairs[0], buf, 1) != 1) {
            log_error("Notice the worker failed.");
        }
        break;
    default:
        log_error("read error char '%c' for worker(id:%d) socketpairs[0](%d)", 
            buf[0], worker->vel.thread.id, worker->socketpairs[1]);
        break;
    }
}

//安装master的事件
static int
setup_master(void)
{
    rstatus_t status;
    uint32_t j;
    vr_listen **vlisten;
    vr_worker *worker;

    //监听worker的管道读事件
    for (j = 0; j < darray_n(&workers); j ++) {
        worker = darray_get(&workers, j);
        //worker线程监听管道的读事件
        status = aeCreateFileEvent(master.vel.el, worker->socketpairs[0], 
            AE_READABLE, thread_event_process, worker);
        if (status == AE_ERR) {
            log_error("Unrecoverable error creating master ipfd file event.");
            return VR_ERROR;
        }
    }

    //master线程监听链接:来一个连接就压入到worker的交换队列
    for (j = 0; j < darray_n(&master.listens); j ++) {
        vlisten = darray_get(&master.listens,j);
        status = aeCreateFileEvent(master.vel.el, (*vlisten)->sd, AE_READABLE, 
            client_accept, *vlisten);
        if (status == AE_ERR) {
            log_error("Unrecoverable error creating master ipfd file event.");
            return VR_ERROR;
        }
    }
    
    return VR_OK;
}

//master线程的运行函数
static void *
master_thread_run(void *args)
{    
    /* vire master run */
    aeMain(master.vel.el);

    return NULL;
}

int
master_run(void)
{
    vr_thread_start(&master.vel.thread);
    return VR_OK;
}

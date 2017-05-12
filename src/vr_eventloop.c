#include <vr_core.h>

//初始化线程事件：打开的文件描符的限制
int
vr_eventloop_init(vr_eventloop *vel, int filelimit)
{    
    rstatus_t status;
    int maxclients, threads_num;

    if (vel == NULL || filelimit <= 0) {
        return VR_ERROR;
    }
    //初始化线程
    vr_thread_init(&vel->thread);
    
    vel->el = NULL;                     //事件基
    vel->hz = 10;                       //周期函数执行的频率 
    vel->cronloops = 0;                 //周期函数的执行次数
    vel->unixtime = time(NULL);         //时间缓存
    vel->mstime = vr_msec_now();        //时间缓存：毫秒
    vel->lruclock = getLRUClock();      // lru的时钟
    vel->cb = NULL;                     //链接
    vel->next_client_id = 1;            /* Client IDs, start from 1 .*/
    vel->current_client = NULL;         //当前的客户端为NULL
    vel->clients = NULL;                //客户端链表
    vel->clients_pending_write = NULL;  //追加的写操作函数
    vel->clients_to_close = NULL;       //异步关闭的客户端链表
    vel->clients_paused = 0;            //客户端的中断标志
    vel->clients_pause_end_time = 0;    //中断客户端的时间
    vel->stats = NULL;                  //线程状态
    vel->resident_set_size = 0;       
    vel->dirty = 0;                     //是否已经写了数据库
    vel->bpop_blocked_clients = 0;      //阻塞弹出操作的客户端队列
    vel->unblocked_clients = NULL;      //不再阻塞的客户端链表
    vel->clients_waiting_acks = NULL;   //等得ack的客户端链表
    vel->pubsub_channels = NULL;        //订阅的频道信息
    vel->pubsub_patterns = NULL;        //
    vel->notify_keyspace_events = 0;    //键空间通知
    vel->cstable = NULL;                //命令状态表

    vel->el = aeCreateEventLoop(filelimit);//创建事件基
    if (vel->el == NULL) {
        log_error("create eventloop failed.");
        return VR_ERROR;
    }

    vel->cb = dalloc(sizeof(conn_base));//创建链接库
    if (vel->cb == NULL) {
        log_error("create conn_base failed: out of memory");
        return VR_ENOMEM;
    }
    status = conn_init(vel->cb);        //初始化链接库
    if (status != VR_OK) {
        log_error("init conn_base failed");
        return VR_ERROR;
    }

    vel->clients = dlistCreate();       //创建客户端链表
    if (vel->clients == NULL) {
        log_error("create list failed: out of memory");
        return VR_ENOMEM;
    }

    vel->clients_pending_write = dlistCreate();    //创建追加写操作链表
    if (vel->clients_pending_write == NULL) {
        log_error("create list failed: out of memory");
        return VR_ENOMEM;
    }
    //创建异步关闭的客户端
    vel->clients_to_close = dlistCreate();
    if (vel->clients_to_close == NULL) {
        log_error("create list failed: out of memory");
        return VR_ENOMEM;
    }
    //不再阻塞的客户端链表
    vel->unblocked_clients = dlistCreate();
    if (vel->unblocked_clients == NULL) {
        log_error("create list failed: out of memory");
        return VR_ENOMEM;
    }
    //线程的状态
    vel->stats = dalloc(sizeof(vr_stats));
    if (vel->stats == NULL) {
        log_error("out of memory");
        return VR_ENOMEM;
    }

    vr_stats_init(vel->stats);      //初始化线程状态

    conf_cache_init(&vel->cc);      //初始化配置缓存

    return VR_OK;
}
//析构
void
vr_eventloop_deinit(vr_eventloop *vel)
{
    if (vel == NULL) {
        return;
    }

    vr_thread_deinit(&vel->thread);

    if (vel->el != NULL) {
        aeDeleteEventLoop(vel->el);
        vel->el = NULL;
    }

    if (vel->clients != NULL) {
        client *c;
        while (c = dlistPop(vel->clients)) {
            freeClient(c);
        }
        dlistRelease(vel->clients);
        vel->clients = NULL;
    }

    if (vel->clients_pending_write != NULL) {
        client *c;
        while (c = dlistPop(vel->clients_pending_write)) {}
        dlistRelease(vel->clients_pending_write);
        vel->clients_pending_write = NULL;
    }

    if (vel->clients_to_close != NULL) {
        client *c;
        while (c = dlistPop(vel->clients_to_close)) {
            freeClient(c);
        }
        dlistRelease(vel->clients_to_close);
        vel->clients_to_close = NULL;
    }

    if (vel->unblocked_clients != NULL) {
        client *c;
        while (c = dlistPop(vel->unblocked_clients)) {}
        dlistRelease(vel->unblocked_clients);
        vel->unblocked_clients = NULL;
    }

    if (vel->cb != NULL) {
        conn_deinit(vel->cb);
        dfree(vel->cb);
        vel->cb = NULL;
    }

    if (vel->stats != NULL) {
        vr_stats_deinit(vel->stats);
        dfree(vel->stats);
        vel->stats = NULL;
    }

    if (vel->cstable != NULL) {
        commandStatsTableDestroy(vel->cstable);
        vel->cstable = NULL;
    }

    conf_cache_deinit(&vel->cc);
}


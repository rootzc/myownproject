#ifndef _VR_EVENTLOOP_H_
#define _VR_EVENTLOOP_H_

typedef struct vr_eventloop {
    //执行循环的线程
    vr_thread thread;

    //epoll监听事件基
    aeEventLoop *el;
    //周期函数的至此间隔
    int hz;                     /* cron() calls frequency in hertz */
    //周期函数的执行次数
    int cronloops;              /* Number of times the cron function run */
    
    /* time cache */
    //时间的缓存
    //unix时间戳记录
    time_t unixtime;            /* Unix time sampled every cron cycle. */
    //毫秒
    long long mstime;           /* Like 'unixtime' but with milliseconds resolution. */

    //lru过期时间
    unsigned lruclock:LRU_BITS; /* Clock for LRU eviction */

    //链接
    conn_base *cb;

    //下一歌客户端的id
    uint64_t next_client_id;    /* Next client unique ID. Incremental. */
    //当前的客户端
    struct client *current_client;     /* Current client, only used on crash report */
    //客户端链表
    dlist *clients;              /* List of active clients */
    
    //装载handler使用？
    dlist *clients_pending_write;/* There is to write or install handler. */
    //异步关闭的客户端链表
    dlist *clients_to_close;     /* Clients to close asynchronously */
    
    //客户端中断标志
    int clients_paused;         /* True if clients are currently paused */
    //中断客户端的时间
    long long clients_pause_end_time; /* Time when we undo clients_paused */

    //线程状态
    vr_stats *stats;            /* stats for this thread */
    //
    size_t resident_set_size;   /* RSS sampled in workerCron(). */

    //对数据库有了写操作
    long long dirty;            /* Changes to DB from the last save */

    //阻塞的客户端的个数
    unsigned int bpop_blocked_clients; /* Number of clients blocked by lists */
    //非阻塞的客户端数量
    dlist *unblocked_clients;        /* list of clients to unblock before next loop */

    /* Synchronous replication. */
    //等待ack响应的等待队列
    dlist *clients_waiting_acks;     /* Clients waiting in WAIT command. */

    /* Pubsub */
    //订阅频道
    dict *pubsub_channels;  /* Map channels to list of subscribed clients */
    //
    dlist *pubsub_patterns;  /* A list of pubsub_patterns */
    //
    int notify_keyspace_events; /* Events to propagate via Pub/Sub. This is an
                                   xor of NOTIFY_... flags. */

    //配置缓存选项
    conf_cache cc; /* Cache the hot config option to improve vire speed. */

    //命令状态表
    struct darray *cstable; /* type: commandStats */
}vr_eventloop;

int vr_eventloop_init(vr_eventloop *vel, int filelimit);
void vr_eventloop_deinit(vr_eventloop *vel);

#endif

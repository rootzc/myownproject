#include <vr_core.h>

/* Which thread we assigned a connection to most recently. */
static int num_backend_threads;

struct darray backends;

static void *backend_thread_run(void *args);
//初始化后台线程
int
vr_backend_init(vr_backend *backend)
{
    rstatus_t status;
    int threads_num;
    
    if (backend == NULL) {
        return VR_ERROR;
    }

    backend->id = 0;
    backend->current_db = 0;
    backend->timelimit_exit = 0;
    backend->last_fast_cycle = 0;
    backend->resize_db = 0;
    backend->rehash_db = 0;

    //初始化事件处理机
    vr_eventloop_init(&backend->vel, 10);
    backend->vel.thread.fun_run = backend_thread_run;
    backend->vel.thread.data = backend;
    
    return VR_OK;
}
//析构后台线程
void
vr_backend_deinit(vr_backend *backend)
{
    if (backend == NULL) {
        return;
    }

    vr_eventloop_deinit(&backend->vel);
}
//周期函数
static int
backend_cron(struct aeEventLoop *eventLoop, long long id, void *clientData) {
    //得到后台线程
    vr_worker *backend = clientData;
    //事件处理机
    vr_eventloop *vel = &backend->vel;
    size_t stat_used_memory, stats_peak_memory;

    UNUSED(eventLoop);
    UNUSED(id);
    UNUSED(clientData);

    ASSERT(eventLoop == vel->el);

    vel->unixtime = time(NULL);
    vel->mstime = vr_msec_now();

    /* Record the max memory used since the server was started. */
    stat_used_memory = dalloc_used_memory();
    update_stats_get(vel->stats, peak_memory, &stats_peak_memory);
    if (stat_used_memory > stats_peak_memory) {
        update_stats_set(vel->stats, peak_memory, stat_used_memory);
    }
    //数据库的周期处理函数
    databasesCron(backend);

    /* Update the config cache */
    run_with_period(1000, vel->cronloops) {
        conf_cache_update(&vel->cc);
    }
    
    vel->cronloops ++;
    return 1000/vel->hz;
}
//安装后台线程的事件处理函数
static int
setup_backend(vr_backend *backend)
{
    /* Create the serverCron() time event, that's our main way to process
     * background operations. */
    if(aeCreateTimeEvent(backend->vel.el, 1, backend_cron, backend, NULL) == AE_ERR) {
        serverPanic("Can't create the serverCron time event.");
        return VR_ERROR;
    }
    
    return VR_OK;
}
//后台线程的运行函数
static void *
backend_thread_run(void *args)
{
    vr_worker *backend = args;
    
    /* vire worker run */
    aeMain(backend->vel.el);

    return NULL;
}
//初始化后台线程组
int
backends_init(uint32_t backend_count)
{
    rstatus_t status;
    uint32_t idx;
    vr_backend *backend;

    darray_init(&backends, backend_count, sizeof(vr_backend));

    for (idx = 0; idx < backend_count; idx ++) {
        backend = darray_push(&backends);
        vr_backend_init(backend);
        backend->id = idx;
        status = setup_backend(backend);
        if (status != VR_OK) {
            exit(1);
        }
    }
    
    num_backend_threads = (int)darray_n(&backends);

    return VR_OK;
}
//运行后台线程组
int
backends_run(void)
{
    uint32_t i, thread_count;
    vr_backend *backend;

    thread_count = (uint32_t)num_backend_threads;

    for (i = 0; i < thread_count; i ++) {
        backend = darray_get(&backends, i);
        vr_thread_start(&backend->vel.thread);
    }

    return VR_OK;
}
//等待后台线程退出
int
backends_wait(void)
{
    uint32_t i, thread_count;
    vr_backend *backend;

    thread_count = (uint32_t)num_backend_threads;

    for (i = 0; i < thread_count; i ++) {
        backend = darray_get(&backends, i);
        pthread_join(backend->vel.thread.thread_id, NULL);
    }

    return VR_OK;
}
//析构后台线程组
void
backends_deinit(void)
{
    vr_backend *backend;

    while(darray_n(&backends)) {
        backend = darray_pop(&backends);
		vr_backend_deinit(backend);
    }
}

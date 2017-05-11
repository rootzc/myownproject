#ifndef _VR_MASTER_H_
#define _VR_MASTER_H_
//master结构
typedef struct vr_master {

    //事件循环
    vr_eventloop vel;
    //监听链接的结构
    struct darray listens;   /* type: vr_listen */
    //链接交换单元
    dlist *cbsul;    /* Connect back swap unit list */
    //链接交换单元的互斥锁
    pthread_mutex_t cbsullock;   /* swap unit list locker */
}vr_master;

extern vr_master master;

int master_init(vr_conf *conf);
void master_deinit(void);

void dispatch_conn_exist(struct client *c, int tid);

int master_run(void);

#endif

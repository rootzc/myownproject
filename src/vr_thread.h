#ifndef _VR_THREAD_H_
#define _VR_THREAD_H_

typedef void *(*vr_thread_func_t)(void *data);
//线程抽象
typedef struct vr_thread {
    //线程的局部
    int id;
    //线程id
    pthread_t thread_id;
    //线程运行的函数
    vr_thread_func_t fun_run;
    //线程函数参数
    void *data;
}vr_thread;

int vr_thread_init(vr_thread *thread);
void vr_thread_deinit(vr_thread *thread);
int vr_thread_start(vr_thread *thread);

#endif

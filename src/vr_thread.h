#ifndef _VR_THREAD_H_
#define _VR_THREAD_H_
//抽象出的线程结构体
//
//线程执行函数
typedef void *(*vr_thread_func_t)(void *data);

typedef struct vr_thread {
    //这是局部的线程id用来标记同种类线程，用于任务分配
    int id;
    //全局的线程id
    pthread_t thread_id;
    //线程的执行函数
    vr_thread_func_t fun_run;
    //参数
    void *data;
}vr_thread;

//线程很简单就是初始化，析构以及开始运行
int vr_thread_init(vr_thread *thread);
void vr_thread_deinit(vr_thread *thread);
int vr_thread_start(vr_thread *thread);

#endif

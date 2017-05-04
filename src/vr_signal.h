#ifndef _VR_SIGNAL_H_
#define _VR_SIGNAL_H_
//信号
struct signal {
    int  signo;                     //信号值
    char *signame;                  //信号名字
    int  flags;                     //标志
    void (*handler)(int signo);     //信号处理函数
};

//信号初始化
rstatus_t signal_init(void);
//信号析构
void signal_deinit(void);
//信号处理函数
void signal_handler(int signo);

#endif

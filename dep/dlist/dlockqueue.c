#include <pthread.h>
#include <stdlib.h>
#include <stdio.h>

#include <dmalloc.h>

#include <dlist.h>
#include <dmtqueue.h>
#include <dlockqueue.h>
//创建有锁队列
dlockqueue *dlockqueue_create(void)
{
    dlockqueue *lqueue;

    lqueue = dalloc(sizeof(*lqueue));
    if (lqueue == NULL) {
        return NULL;
    }

    lqueue->maxlen = -1;
    //0
    lqueue->maxlen_policy = MAX_LENGTH_POLICY_REJECT;
    //初始化互斥量
    pthread_mutex_init(&lqueue->lmutex,NULL);
    
    lqueue->l = dlistCreate();
    if (lqueue->l == NULL) {
        dlockqueue_destroy(lqueue);
        return NULL;
    }

    return lqueue;
}
//压数据
long long dlockqueue_push(void *q, void *value)
{
    dlockqueue *lqueue = q;
    dlist *list;
    long long length;
    //加锁
    pthread_mutex_lock(&lqueue->lmutex);
    length = (long long)dlistLength(lqueue->l);
    //长度超过限制
    if (lqueue->maxlen >0 && length >= lqueue->maxlen) {
        //策略为拒绝继续插入，
        if (lqueue->maxlen_policy == MAX_LENGTH_POLICY_REJECT) {
            length = -1;

            //策略为删头，尾插新
        } else if (lqueue->maxlen_policy == MAX_LENGTH_POLICY_EVICT_HEAD) {
            while (length >= lqueue->maxlen) {
                dlistNode *ln = dlistFirst(lqueue->l);
                dlistDelNode(lqueue->l,ln);
                length = (long long)dlistLength(lqueue->l);
            }
            list = dlistAddNodeTail(lqueue->l, value);
            length ++;
            //删除尾，尾插新
        } else if (lqueue->maxlen_policy == MAX_LENGTH_POLICY_EVICT_END) {
            while (length >= lqueue->maxlen) {
                dlistNode *ln = dlistLast(lqueue->l);
                dlistDelNode(lqueue->l,ln);
                length = (long long)dlistLength(lqueue->l);
            }
            list = dlistAddNodeTail(lqueue->l, value);
            length ++;
        }
    } else {
        //尾插
        list = dlistAddNodeTail(lqueue->l, value);
        length ++;
    }
    //解锁
    pthread_mutex_unlock(&lqueue->lmutex);

    if (list == NULL) {
        return -1;
    }

    return length;
}

void *dlockqueue_pop(void *q)
{
    dlockqueue *lqueue = q;
    dlistNode *node;
    void *value;
        
    if (lqueue == NULL || lqueue->l == NULL) {
        return NULL;
    }
    //加锁
    pthread_mutex_lock(&lqueue->lmutex);
    //找到头
    node = dlistFirst(lqueue->l);
    if (node == NULL) {
        //出错
        pthread_mutex_unlock(&lqueue->lmutex);
        return NULL;
    }
    
    value = dlistNodeValue(node);

    dlistDelNode(lqueue->l, node);
    
    pthread_mutex_unlock(&lqueue->lmutex);

    return value;
}

//销毁
void dlockqueue_destroy(void *q)
{
    dlockqueue *lqueue = q;
    if (lqueue == NULL) {
        return;
    }

    if (lqueue->l != NULL) {
        dlistRelease(lqueue->l);
    }
    //销毁锁
    pthread_mutex_destroy(&lqueue->lmutex);

    dfree(lqueue);
}
//得到长度
long long dlockqueue_length(void *q)
{
    dlockqueue *lqueue = q;
    long long length;
    
    if (lqueue == NULL || lqueue->l == NULL) {
        return -1;
    }

    pthread_mutex_lock(&lqueue->lmutex);
    length = dlistLength(lqueue->l);
    pthread_mutex_unlock(&lqueue->lmutex);
    
    return length;
}

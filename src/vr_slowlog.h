#ifndef _VR_SLOWLOG_H_
#define _VR_SLOWLOG_H_

#define SLOWLOG_ENTRY_MAX_ARGC 32
#define SLOWLOG_ENTRY_MAX_STRING 128

/* This structure defines an entry inside the slow log list */
//慢查询日志链表的节点
typedef struct slowlogEntry {
    //
    robj **argv;//命令，参数
    int argc;//参数数量
    long long id;       /* Unique entry identifier. */
    long long duration; /* Time spent by the query, in nanoseconds. */
    time_t time;        /* Unix time at which the query was executed. */
} slowlogEntry;

/* Exported API */
void slowlogInit(void);
void slowlogPushEntryIfNeeded(vr_eventloop *vel, robj **argv, int argc, long long duration);

/* Exported commands */
void slowlogCommand(client *c);

#endif

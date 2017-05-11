#ifndef _VR_COMMAND_H_
#define _VR_COMMAND_H_

/* Command flags. Please check the command table defined in the redis.c file
 * for more information about the meaning of every flag. */
//命令的标志

#define CMD_WRITE 1                   /* "w" flag */
#define CMD_READONLY 2                /* "r" flag */
#define CMD_DENYOOM 4                 /* "m" flag */
#define CMD_NOT_USED_1 8              /* no longer used flag */
#define CMD_ADMIN 16                  /* "a" flag */
#define CMD_PUBSUB 32                 /* "p" flag */
#define CMD_NOSCRIPT  64              /* "s" flag */
#define CMD_RANDOM 128                /* "R" flag */
#define CMD_SORT_FOR_SCRIPT 256       /* "S" flag */
#define CMD_LOADING 512               /* "l" flag */
#define CMD_STALE 1024                /* "t" flag */
#define CMD_SKIP_MONITOR 2048         /* "M" flag */
#define CMD_ASKING 4096               /* "k" flag */
#define CMD_FAST 8192                 /* "F" flag */

/* Command call flags, see call() function */
//命令调用标志， 
#define CMD_CALL_NONE 0
#define CMD_CALL_SLOWLOG (1<<0)
#define CMD_CALL_STATS (1<<1)
#define CMD_CALL_PROPAGATE_AOF (1<<2)
#define CMD_CALL_PROPAGATE_REPL (1<<3)
#define CMD_CALL_PROPAGATE (CMD_CALL_PROPAGATE_AOF|CMD_CALL_PROPAGATE_REPL)
#define CMD_CALL_FULL (CMD_CALL_SLOWLOG | CMD_CALL_STATS | CMD_CALL_PROPAGATE)

/* Command propagation flags, see propagate() function */
//
#define PROPAGATE_NONE 0
#define PROPAGATE_AOF 1
#define PROPAGATE_REPL 2

//关闭标志
/* SHUTDOWN flags */
#define SHUTDOWN_NOFLAGS 0      /* No flags. */
#define SHUTDOWN_SAVE 1         /* Force SAVE on SHUTDOWN even if no save
                                     points are configured. */
#define SHUTDOWN_NOSAVE 2       /* Don't SAVE on SHUTDOWN. */

//命令执行函数类型
typedef void redisCommandProc(struct client *c);
typedef int *redisGetKeysProc(struct redisCommand *cmd, robj **argv, int argc, int *numkeys);
//redis命令结构体
struct redisCommand {
    //命令名
    char *name;     
    //执行的函数
    redisCommandProc *proc;
    //参数数量
    int arity;
    //字符串标志
    char *sflags; /* Flags as string representation, one char per flag. */
    //数字标志
    int flags;    /* The actual flags, obtained from the 'sflags' field. */
    /* Use a function to determine keys arguments in a command line.
     * Used for Redis Cluster redirect. */
    //
    redisGetKeysProc *getkeys_proc;
    /* What keys should be loaded in background when calling this command? */
    //第一个键
    int firstkey; /* The first argument that's a key (0 = no keys) */
    //最后一个键
    int lastkey;  /* The last argument that's a key */
    //第一个键与最后一个键之间的步数
    int keystep;  /* The step between first and last key */
    int idx;
    //管理员命令
    int needadmin;
};

//命令状态
typedef struct commandStats {
    //命令名
    char *name;
    //执行的时间
    long long microseconds;
    //调用的次数
    long long calls;
}commandStats;

/* The redisOp structure defines a Redis Operation, that is an instance of
 * a command with an argument vector, database ID, propagation target
 * (PROPAGATE_*), and command pointer.
 *
 * Currently only used to additionally propagate more commands to AOF/Replication
 * after the propagation of the executed command. */
//redis类似额操作
typedef struct redisOp {
    //所有参数
    robj **argv;
    //参数个数，数据库，目标
    int argc, dbid, target;
    //对应的命令
    struct redisCommand *cmd;
} redisOp;

/* Defines an array of Redis operations. There is an API to add to this
 * structure in a easy way.
 *
 * redisOpArrayInit();
 * redisOpArrayAppend();
 * redisOpArrayFree();
 */
//命令组
typedef struct redisOpArray {
    redisOp *ops;
    //命令个数
    int numops;
} redisOpArray;

//命令hash表的操作
extern dictType commandTableDictType;

//创建命令表
void populateCommandTable(void);
//标记命令表中的管理员命令
int populateCommandsNeedAdminpass(void);

//查找命令
struct redisCommand *lookupCommand(sds name);
//
struct redisCommand *lookupCommandOrOriginal(sds name);
//通过c字符串查找命令
struct redisCommand *lookupCommandByCString(char *s);

//真正的调用出处：多态的命令，这里的client的结构体颇有c++面向对象设计方法的风格，一个client类似于cpp中隐式传入的对象
void call(struct client *c, int flags);
//处理命令
int processCommand(struct client *c);

//多条命令组合
void redisOpArrayInit(redisOpArray *oa);
int redisOpArrayAppend(redisOpArray *oa, struct redisCommand *cmd, int dbid, robj **argv, int argc, int target);
void redisOpArrayFree(redisOpArray *oa);

void propagate(struct redisCommand *cmd, int dbid, robj **argv, int argc, int flags);
void alsoPropagate(struct redisCommand *cmd, int dbid, robj **argv, int argc, int target);
void forceCommandPropagation(struct client *c, int flags);
void preventCommandPropagation(struct client *c);
void preventCommandAOF(struct client *c);
void preventCommandReplication(struct client *c);

void commandCommand(struct client *c);

struct darray *commandStatsTableCreate(void);
void commandStatsTableDestroy(struct darray *cstatstable);

#endif

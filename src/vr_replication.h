#ifndef _VR_REPLICATION_H_
#define _VR_REPLICATION_H_
//用于主从复制
//
//
//
/* Slave replication state. Used in server.repl_state for slaves to remember
 * what to do next. */
//复制状态
#define REPL_STATE_NONE 0 /* No active replication */
#define REPL_STATE_CONNECT 1 /* Must connect to master */
#define REPL_STATE_CONNECTING 2 /* Connecting to master */

//握手状态
/* --- Handshake states, must be ordered --- */
//等待回应pong
#define REPL_STATE_RECEIVE_PONG 3 /* Wait for PING reply */


//发送验证密码的命令
#define REPL_STATE_SEND_AUTH 4 /* Send AUTH to master */
//等待验证
#define REPL_STATE_RECEIVE_AUTH 5 /* Wait for AUTH reply */


//发送配置的接收复制内容的监听端口
#define REPL_STATE_SEND_PORT 6 /* Send REPLCONF listening-port */
//等待发送端口后的响应
#define REPL_STATE_RECEIVE_PORT 7 /* Wait for REPLCONF reply */

//发送了数据
#define REPL_STATE_SEND_CAPA 8 /* Send REPLCONF capa */
//等待响应
#define REPL_STATE_RECEIVE_CAPA 9 /* Wait for REPLCONF reply */

//发送PSYNC
#define REPL_STATE_SEND_PSYNC 10 /* Send PSYNC */
//等待响应
#define REPL_STATE_RECEIVE_PSYNC 11 /* Wait for PSYNC reply */


/* --- End of handshake states --- */
//结束握手
//受到了rdb文件
#define REPL_STATE_TRANSFER 12 /* Receiving .rdb from master */
//链接到master
#define REPL_STATE_CONNECTED 13 /* Connected to master */


/* State of slaves from the POV of the master. Used in client->replstate.
 * In SEND_BULK and ONLINE state the slave receives new updates
 * in its output queue. In the WAIT_BGSAVE states instead the server is waiting
 * to start the next background saving in order to send updates to it. */
//master服务器的标志
//产生rdb文件
#define SLAVE_STATE_WAIT_BGSAVE_START 6 /* We need to produce a new RDB file. */
//等待创建rdb结束
#define SLAVE_STATE_WAIT_BGSAVE_END 7 /* Waiting RDB file creation to finish. */
//发送rdb文件到slave
#define SLAVE_STATE_SEND_BULK 8 /* Sending RDB file to slave. */
//
#define SLAVE_STATE_ONLINE 9 /* RDB file transmitted, sending just updates. */


/* Slave capabilities. */
#define SLAVE_CAPA_NONE 0
#define SLAVE_CAPA_EOF (1<<0)   /* Can parse the RDB EOF streaming format. */

/* Synchronous read timeout - slave side */
#define CONFIG_REPL_SYNCIO_TIMEOUT 5

#define REPLICATION_ROLE_MASTER 0
#define REPLICATION_ROLE_SLAVE  1

//
#define CONFIG_DEFAULT_REPL_BACKLOG_SIZE (1024*1024)    /* 1mb */
#define CONFIG_DEFAULT_REPL_BACKLOG_TIME_LIMIT (60*60)  /* 1 hour */
#define CONFIG_REPL_BACKLOG_MIN_SIZE (1024*16)          /* 16k */


struct vr_replication {
    vr_eventloop vel;
    //角色
    int role;               /* Master/slave? */

    /* Replication (master) */
    //slaver链表
    dlist *slaves;           /* List of slaves */
    //从服务器选择的数据库
    int slaveseldb;                 /* Last SELECTed DB in replication output */
    //全局的复制的偏移地址
    long long master_repl_offset;   /* Global replication offset */
    //master服务器发送ping给slave的频率
    int repl_ping_slave_period;     /* Master pings the slave every N seconds */
    //发送缓冲区
    char *repl_backlog;             /* Replication backlog for partial syncs */
    //发送循环缓冲区的大小
    long long repl_backlog_size;    /* Backlog circular buffer size */
    //发送缓冲的的有效数据长度
    long long repl_backlog_histlen; /* Backlog actual data length */
    //发送缓冲当前位移
    long long repl_backlog_idx;     /* Backlog circular buffer current offset */
    //循环缓冲区当前的起始位置
    long long repl_backlog_off;     /* Replication offset of first byte in the
                                       backlog buffer. */
    //没有slave时多久释放缓冲区
    time_t repl_backlog_time_limit; /* Time without slaves after the backlog
                                       gets released. */
    //当slvae链表为空时的时间
    time_t repl_no_slaves_since;    /* We have no slaves since that time.
                                       Only valid if server.slaves len is 0. */
    //
    //最小进行复制的slave数量
    int repl_min_slaves_to_write;   /* Min number of slaves to write. */
    int repl_min_slaves_max_lag;    /* Max lag of <count> slaves to write. */
    //可以进行复制的数量
    int repl_good_slaves_count;     /* Number of slaves with lag <= max_lag. */
    //直接发送RDB到网络
    int repl_diskless_sync;         /* Send RDB to slaves sockets directly. */
    //延迟发送rdb
    int repl_diskless_sync_delay;   /* Delay to start a diskless repl BGSAVE. */

    /* Replication (slave) */
    //master验证密码
    char *masterauth;               /* AUTH with this password with master */
    //master hostname
    char *masterhost;               /* Hostname of master */
    
    int masterport;                 /* Port of master */
    
    int repl_timeout;               /* Timeout after N seconds of master idle */
    //master作为slave的client
    client *master;     /* Client that is master for this slave */
    //缓存master
    client *cached_master; /* Cached master to be reused for PSYNC. */
    //
    int repl_syncio_timeout; /* Timeout for synchronous I/O calls */
    //复制的状态
    int repl_state;          /* Replication status if the instance is a slave */
    //每次读取rdb文件的大小
    off_t repl_transfer_size; /* Size of RDB to read from master during sync. */
    //每次读取rdb的文件数量
    off_t repl_transfer_read; /* Amount of RDB read from master during sync. */
    //上次传输的位置
    off_t repl_transfer_last_fsync_off; /* Offset when we fsync-ed last time. */
    //网络socketfd
    int repl_transfer_s;     /* Slave -> Master SYNC socket */
    //rdb文件fd
    int repl_transfer_fd;    /* Slave -> Master SYNC temp file descriptor */
    
    //
    char *repl_transfer_tmpfile; /* Slave-> master SYNC temp file name */
    time_t repl_transfer_lastio; /* Unix time of the latest read, for timeout */
    int repl_serve_stale_data; /* Serve stale data when link is down? */
    int repl_slave_ro;          /* Slave is read only? */
    time_t repl_down_since; /* Unix time at which link with master went down */
    //关闭tcp的传输算法
    int repl_disable_tcp_nodelay;   /* Disable TCP_NODELAY after SYNC? */
    //高可用模式中的主服务器
    int slave_priority;             /* Reported in INFO and used by Sentinel. */
    //runid
    char repl_master_runid[CONFIG_RUN_ID_SIZE+1];  /* Master run id for PSYNC. */
    long long repl_master_initial_offset;         /* Master PSYNC offset. */
};

extern struct vr_replication repl;

int vr_replication_init(void);
void vr_replication_deinit(void);

void unblockClientWaitingReplicas(client *c);
void refreshGoodSlavesCount(void);
void replicationHandleMasterDisconnection(void);
void replicationCacheMaster(client *c);
char *replicationGetSlaveName(client *c);
void replconfCommand(client *c);
void putSlaveOnline(client *slave);
void replicationSendAck(void);
void replicationFeedMonitors(client *c, dlist *monitors, int dictid, robj **argv, int argc);
void replicationFeedSlaves(dlist *slaves, int dictid, robj **argv, int argc);
void feedReplicationBacklogWithObject(robj *o);
void feedReplicationBacklog(void *ptr, size_t len);

#endif

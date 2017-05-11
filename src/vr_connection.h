#ifndef _VR_CONNECTION_H_
#define _VR_CONNECTION_H_

//管理链接的结构:连接池
typedef struct conn_base {
    dlist *free_connq;           /* free conn q */
    uint64_t ntotal_conn;       /* total # connections counter from start */
    uint32_t ncurr_conn;        /* current # connections */
    uint32_t ncurr_cconn;       /* current # client connections */
}conn_base;

//链接
struct conn {
    //链接的拥有者？
    void                *owner;          /* connection owner */
    //连接池
    conn_base           *cb;             /* connect base */
    //客户端的sockfd
    int                 sd;              /* socket descriptor */
    //接收到的字节数
    size_t              recv_bytes;      /* received (read) bytes */
    //已发送的字节数
    size_t              send_bytes;      /* sent (written) bytes */
    //链接的错误信息
    err_t               err;             /* connection errno */
    //收发数据的状态
    unsigned            recv_active:1;   /* recv active? */
    unsigned            recv_ready:1;    /* recv ready? */
    unsigned            send_active:1;   /* send active? */
    unsigned            send_ready:1;    /* send ready? */
    //链接
    unsigned            connecting:1;    /* connecting? */
    unsigned            connected:1;     /* connected? */
    //终止
    unsigned            eof:1;           /* eof? aka passive close? */
    unsigned            done:1;          /* done? aka close? */

    //
    dlist                *inqueue;        /* incoming request queue */
    dlist                *outqueue;       /* outputing response queue */
};

struct conn *conn_get(conn_base *cb);
void conn_put(struct conn *conn);

int conn_init(conn_base *cb);
void conn_deinit(conn_base *cb);

ssize_t conn_recv(struct conn *conn, void *buf, size_t size);
ssize_t conn_send(struct conn *conn, void *buf, size_t nsend);
ssize_t conn_sendv(struct conn *conn, struct darray *sendv, size_t nsend);

#endif

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <getopt.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/utsname.h>

#include <vr_core.h>
#include <vr_conf.h>
#include <vr_signal.h>

#define VR_CONF_PATH        "conf/vire.conf"

#define VR_LOG_DEFAULT      LOG_NOTICE
#define VR_LOG_MIN          LOG_EMERG
#define VR_LOG_MAX          LOG_PVERB
#define VR_LOG_PATH         NULL

#define VR_PORT             8889
#define VR_ADDR             "0.0.0.0"
#define VR_INTERVAL         (30 * 1000) /* in msec */

#define VR_PID_FILE         NULL
//得到系统的cpu个数
#define VR_THREAD_NUM_DEFAULT	(sysconf(_SC_NPROCESSORS_ONLN)>6?6:sysconf(_SC_NPROCESSORS_ONLN))

static int show_help;
static int show_version;
static int test_conf;
static int daemonize;
//用于解析字符串命令行 
static struct option long_options[] = {
    { "help",           no_argument,        NULL,   'h' },
    { "version",        no_argument,        NULL,   'V' },
    { "test-conf",      no_argument,        NULL,   't' },
    { "daemonize",      no_argument,        NULL,   'd' },
    { "verbose",        required_argument,  NULL,   'v' },
    { "output",         required_argument,  NULL,   'o' },
    { "conf-file",      required_argument,  NULL,   'c' },
    { "pid-file",       required_argument,  NULL,   'p' },
    { "thread-num",     required_argument,  NULL,   'T' },
    { NULL,             0,                  NULL,    0  }
};

static char short_options[] = "hVtdv:o:c:p:T:";
//创建守护进程
static rstatus_t
vr_daemonize(int dump_core)
{
    rstatus_t status;
    pid_t pid, sid;
    int fd;
    //创建进程
    pid = fork();
    switch (pid) {
    case -1:
        log_error("fork() failed: %s", strerror(errno));
        return VR_ERROR;
    //子进程
    case 0:
        break;
    //父进程退出
    default:
        /* parent terminates */
        _exit(0);
    }

    /* 1st child continues and becomes the session leader */
    //成为新的会话组组长
    sid = setsid();
    if (sid < 0) {
        log_error("setsid() failed: %s", strerror(errno));
        return VR_ERROR;
    }
    //设置信号处理函数
    if (signal(SIGHUP, SIG_IGN) == SIG_ERR) {
        log_error("signal(SIGHUP, SIG_IGN) failed: %s", strerror(errno));
        return VR_ERROR;
    }
    //创建第二个子进程？
    pid = fork();
    switch (pid) {
    case -1:
        log_error("fork() failed: %s", strerror(errno));
        return VR_ERROR;

    case 0:
        break;

    default:
        /* 1st child terminates */
        _exit(0);
    }

    /* 2nd child continues */

    /* change working directory */
    if (dump_core == 0) {
        status = chdir("/");
        if (status < 0) {
            log_error("chdir(\"/\") failed: %s", strerror(errno));
            return VR_ERROR;
        }
    }

    /* clear file mode creation mask */
    umask(0);

    /* redirect stdin, stdout and stderr to "/dev/null" */
    //输入输出错误都重定向到/dev/null
    fd = open("/dev/null", O_RDWR);
    if (fd < 0) {
        log_error("open(\"/dev/null\") failed: %s", strerror(errno));
        return VR_ERROR;
    }

    status = dup2(fd, STDIN_FILENO);
    if (status < 0) {
        log_error("dup2(%d, STDIN) failed: %s", fd, strerror(errno));
        close(fd);
        return VR_ERROR;
    }

    status = dup2(fd, STDOUT_FILENO);
    if (status < 0) {
        log_error("dup2(%d, STDOUT) failed: %s", fd, strerror(errno));
        close(fd);
        return VR_ERROR;
    }

    status = dup2(fd, STDERR_FILENO);
    if (status < 0) {
        log_error("dup2(%d, STDERR) failed: %s", fd, strerror(errno));
        close(fd);
        return VR_ERROR;
    }

    if (fd > STDERR_FILENO) {
        status = close(fd);
        if (status < 0) {
            log_error("close(%d) failed: %s", fd, strerror(errno));
            return VR_ERROR;
        }
    }

    return VR_OK;
}

static void
vr_print_run(struct instance *nci)
{
    int status;
    struct utsname name;

    status = uname(&name);

    if (nci->log_filename) {
        char *ascii_logo =
"                _._                                                  \n"
"           _.-``__ ''-._                                             \n"
"      _.-``    `.  *_.  ''-._           Vire %s %s bit\n"
"  .-`` .-```.  ```\-/    _.,_ ''-._                                   \n"
" (    |      |       .-`    `,    )     Running in %s mode\n"
" |`-._`-...-` __...-.``-._;'` _.-'|     Port: %d\n"
" |    `-._   `._    /     _.-'    |     PID: %ld\n"
"  `-._    `-._  `-./  _.-'    _.-'      OS: %s %s %s\n"
" |`-._`-._    `-.__.-'    _.-'_.-'|                                  \n"
" |    `-._`-._        _.-'_.-'    |     https://github.com/vipshop/vire\n"
"  `-._    `-._`-.__.-'_.-'    _.-'                                   \n"
" |`-._`-._    `-.__.-'    _.-'_.-'|                                  \n"
" |    `-._`-._        _.-'_.-'    |                                  \n"
"  `-._    `-._`-.__.-'_.-'    _.-'                                   \n"
"      `-._    `-.__.-'    _.-'                                       \n"
"          `-._        _.-'                                           \n"
"              `-.__.-'                                               \n\n";
        char *buf = dalloc(1024*16);
        snprintf(buf,1024*16,ascii_logo,
            VR_VERSION_STRING,
            (sizeof(long) == 8) ? "64" : "32",
            "standalone", server.port,
            (long) nci->pid,
            status < 0 ? " ":name.sysname,
            status < 0 ? " ":name.release,
            status < 0 ? " ":name.machine);
        log_write_len(buf, strlen(buf));
        dfree(buf);
    }else {
        char buf[256];
        snprintf(buf,256,"Vire %s, %s bit, %s mode, port %d, pid %ld, built for %s %s %s ready to run.\n",
            VR_VERSION_STRING, (sizeof(long) == 8) ? "64" : "32",
            "standalone", server.port, (long) nci->pid,
            status < 0 ? " ":name.sysname,
            status < 0 ? " ":name.release,
            status < 0 ? " ":name.machine);
        log_write_len(buf, strlen(buf));
    }
}

static void
vr_print_done(void)
{
    loga("done, rabbit done");
}

static void
vr_show_usage(void)
{
    log_stderr(
        "Usage: vire [-?hVdt] [-v verbosity level] [-o output file]" CRLF
        "            [-c conf file] [-p pid file]" CRLF
        "            [-T worker threads number]" CRLF
        "");
    log_stderr(
        "Options:" CRLF
        "  -h, --help             : this help" CRLF
        "  -V, --version          : show version and exit" CRLF
        "  -t, --test-conf        : test configuration for syntax errors and exit" CRLF
        "  -d, --daemonize        : run as a daemon");
    log_stderr(
        "  -v, --verbose=N        : set logging level (default: %d, min: %d, max: %d)" CRLF
        "  -o, --output=S         : set logging file (default: %s)" CRLF
        "  -c, --conf-file=S      : set configuration file (default: %s)" CRLF
        "  -p, --pid-file=S       : set pid file (default: %s)" CRLF
        "  -T, --thread_num=N     : set the worker threads number (default: %d)" CRLF
        "",
        VR_LOG_DEFAULT, VR_LOG_MIN, VR_LOG_MAX,
        VR_LOG_PATH != NULL ? VR_LOG_PATH : "stderr",
        VR_CONF_PATH,
        VR_PID_FILE != NULL ? VR_PID_FILE : "off",
        VR_THREAD_NUM_DEFAULT);
}

static rstatus_t
vr_create_pidfile(struct instance *nci)
{
    char pid[VR_UINTMAX_MAXLEN];
    int fd, pid_len;
    ssize_t n;

    fd = open(nci->pid_filename, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        log_error("opening pid file '%s' failed: %s", nci->pid_filename,
                  strerror(errno));
        return VR_ERROR;
    }
    nci->pidfile = 1;

    pid_len = dsnprintf(pid, VR_UINTMAX_MAXLEN, "%d", nci->pid);

    n = vr_write(fd, pid, pid_len);
    if (n < 0) {
        log_error("write to pid file '%s' failed: %s", nci->pid_filename,
                  strerror(errno));
        return VR_ERROR;
    }

    close(fd);

    return VR_OK;
}

static void
vr_remove_pidfile(struct instance *nci)
{
    int status;

    status = unlink(nci->pid_filename);
    if (status < 0) {
        log_error("unlink of pid file '%s' failed, ignored: %s",
                  nci->pid_filename, strerror(errno));
    }
}

static void
vr_set_default_options(struct instance *nci)
{
    int status;
    //日志等级为可见
    nci->log_level = VR_LOG_DEFAULT;
    //日志文件路径:NULL
    nci->log_filename = VR_LOG_PATH;
    //配置文件名："conf/vire.conf
    nci->conf_filename = VR_CONF_PATH;
    //设置主机名:主机名称长度最长为256,调用了一个封装了底层函数gethostname的宏
    status = vr_gethostname(nci->hostname, VR_MAXHOSTNAMELEN);
    //判断出错记录日志
    if (status < 0) {
        log_warn("gethostname failed, ignored: %s", strerror(errno));
        dsnprintf(nci->hostname, VR_MAXHOSTNAMELEN, "unknown");
    }
    //向主机名数组末尾添加‘0’，
    nci->hostname[VR_MAXHOSTNAMELEN - 1] = '\0';
    //进程id为-1
    nci->pid = (pid_t)-1;
    //pid文件名
    nci->pid_filename = NULL;
    //pid文件创建标志
    nci->pidfile = 0;
    //线程数目为6或者cpu个数
    nci->thread_num = (int)VR_THREAD_NUM_DEFAULT;
}

static rstatus_t
vr_get_options(int argc, char **argv, struct instance *nci)
{
    int c, value;

    opterr = 0;

    for (;;) {
        //调用api解析命令行
        //static char short_options[] = "hVtdv:o:c:p:T:";
            //static struct option long_options[] = {
            //    { "help",           no_argument,        NULL,   'h' },
            //    { "version",        no_argument,        NULL,   'V' },
            //    { "test-conf",      no_argument,        NULL,   't' },
            //    { "daemonize",      no_argument,        NULL,   'd' },
            //    { "verbose",        required_argument,  NULL,   'v' },
            //    { "output",         required_argument,  NULL,   'o' },
            //    { "conf-file",      required_argument,  NULL,   'c' },
            //    { "pid-file",       required_argument,  NULL,   'p' },
            //    { "thread-num",     required_argument,  NULL,   'T' },
            //    { NULL,             0,                  NULL,    0  }
            //};
        c = getopt_long(argc, argv, short_options, long_options, NULL);
        if (c == -1) {
            /* no more options */
            break;
        }
        //根据解析的结果设置对应的字符串标记
        switch (c) {
        case 'h':
            show_version = 1;
            show_help = 1;
            break;

        case 'V':
            show_version = 1;
            break;

        case 't':
            test_conf = 1;
            break;

        case 'd':
            daemonize = 1;
            break;

        case 'v':
            //optarg为库函数的全局变量，其指向当前选项的参数
            value = vr_atoi(optarg, strlen(optarg));
            if (value < 0) {
                log_stderr("vire: option -v requires a number");
                return VR_ERROR;
            }
            nci->log_level = value;
            break;

        case 'o':
            nci->log_filename = optarg;
            break;

        case 'c':
            nci->conf_filename = optarg;
            break;

        case 'p':
            nci->pid_filename = optarg;
            break;
            
        case 'T':
            value = vr_atoi(optarg, strlen(optarg));
            if (value < 0) {
                log_stderr("vire: option -T requires a number");
                return VR_ERROR;
            }

            nci->thread_num = value;
            break;

        case '?':
            switch (optopt) {
            case 'o':
            case 'c':
            case 'p':
                log_stderr("vire: option -%c requires a file name",
                           optopt);
                break;

            case 'v':
            case 'T':
                log_stderr("vire: option -%c requires a number", optopt);
                break;

            default:
                log_stderr("vire: invalid option -- '%c'", optopt);
                break;
            }
            return VR_ERROR;

        default:
            log_stderr("vire: invalid option -- '%c'", optopt);
            return VR_ERROR;

        }
    }

    return VR_OK;
}

/*
 * Returns true if configuration file has a valid syntax, otherwise
 * returns false
 */
static bool
vr_test_conf(struct instance *nci, int test)
{
    vr_conf *cf;
    //打开配置文件:创建配置树
    cf = conf_create(nci->conf_filename);
    if (cf == NULL) {
        if (test)
            log_stderr("vire: configuration file '%s' syntax is invalid",
                nci->conf_filename);
        return false;
    }
    //释放配置文件结构体
    conf_destroy(cf);

    if (test)
        log_stderr("vire: configuration file '%s' syntax is ok",
            nci->conf_filename);
    return true;
}

static int
vr_pre_run(struct instance *nci)
{
    int ret;
    //初始化日志：单例模式的日志
    ret = log_init(nci->log_level, nci->log_filename);
    if (ret != VR_OK) {
        return ret;
    }

    log_debug(LOG_VERB, "Vire used logfile: %s", nci->conf_filename);

    //测试加载配置文件：并且本次操作不记录日志
    if (!vr_test_conf(nci, false)) {
        log_error("conf file %s is error", nci->conf_filename);
        return VR_ERROR;
    }
    //守护进程
    if (daemonize) {
        ret = vr_daemonize(1);
        if (ret != VR_OK) {
            return ret;
        }
    }

    //得到进程id
    nci->pid = getpid();
    //设置信号处理函数
    ret = signal_init();
    if (ret != VR_OK) {
        return ret;
    }

    //进程pid文件
    if (nci->pid_filename) {
        ret = vr_create_pidfile(nci);
        if (ret != VR_OK) {
            return VR_ERROR;
        }
    }

    //redis中的重头戏
    ret = init_server(nci);
    if (ret != VR_OK) {
        return VR_ERROR;
    }

    vr_print_run(nci);

    return VR_OK;
}

static void
vr_post_run(struct instance *nci)
{
    /* deinit the threads */
    workers_deinit();
    backends_deinit();
    master_deinit();
    
    if (nci->pidfile) {
        vr_remove_pidfile(nci);
    }

    signal_deinit();

    vr_print_done();

    log_deinit();
}

static void
vr_run(struct instance *nci)
{
    if (nci->thread_num <= 0) {
        log_error("number of work threads must be greater than 0");
        return;
    } else if (nci->thread_num > 64) {
        log_warn("WARNING: Setting a high number of worker threads is not recommended."
            " Set this value to the number of cores in your machine or less.");
    }

    /* run the threads */
    master_run();
    workers_run();
    backends_run();

    /* wait for the threads finish */
    workers_wait();
    backends_wait();
}

int
main(int argc, char **argv)
{
    //运行状态 typedef int rstatus_t; /* return type */
    rstatus_t status;
    struct instance nci;
//设置缺省的instance 配置选项
    vr_set_default_options(&nci);
//解析命令行参数，并设置对应的标记值
    status = vr_get_options(argc, argv, &nci);
    if (status != VR_OK) {
        vr_show_usage();
        exit(1);
    }
//根据上一步解析的命令行参数设置的标志启动函数
    if (show_version) {
        log_stderr("This is vire-%s" CRLF, VR_VERSION_STRING);
        //打印使用方法
        if (show_help) {
            vr_show_usage();
        }
        exit(0);
    }
    //这里主要加载配置文件
    if (test_conf) {
        if (!vr_test_conf(&nci, true)) {
            exit(1);
        }
        exit(0);
    }

    status = vr_pre_run(&nci);
    if (status != VR_OK) {
        vr_post_run(&nci);
        exit(1);
    }

    server.executable = getAbsolutePath(argv[0]);

    vr_run(&nci);

    vr_post_run(&nci);

    return VR_OK;
}

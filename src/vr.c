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
//#include "./vr_util.h"
//配置文件路径
#define VR_CONF_PATH        "conf/vire.conf"

//#define VR_LOG_DEFAULT      LOG_NOTICE
#define VR_LOG_DEFAULT      LOG_ZC
#define VR_LOG_MIN          LOG_EMERG
#define VR_LOG_MAX          LOG_PVERB
#define VR_LOG_PATH         NULL

//端口号
#define VR_PORT             8889
//服务器的地址
#define VR_ADDR             "0.0.0.0"
//
#define VR_INTERVAL         (30 * 1000) /* in msec */

#define VR_PID_FILE         NULL

#define VR_THREAD_NUM_DEFAULT	(sysconf(_SC_NPROCESSORS_ONLN)>6?6:sysconf(_SC_NPROCESSORS_ONLN))

const int SHOWIMG = 1;//我自己定义的开关用来显示自己的log:
//启动模式
//帮助
static int show_help;
//打印版本信息
static int show_version;
//测试配置
static int test_conf;
//守护进程
static int daemonize;
//命令行参数
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

//命令行参数解析所用
static char short_options[] = "hVtdv:o:c:p:T:";

//创建守护进程
static rstatus_t
vr_daemonize(int dump_core)
{
    rstatus_t status;
    pid_t pid, sid;
    int fd;

    pid = fork();
    switch (pid) {
    case -1:
        log_error("fork() failed: %s", strerror(errno));
        return VR_ERROR;

    case 0:
        break;

    default:
        /* parent terminates */
        _exit(0);
    }

    /* 1st child continues and becomes the session leader */

    sid = setsid();
    if (sid < 0) {
        log_error("setsid() failed: %s", strerror(errno));
        return VR_ERROR;
    }

    if (signal(SIGHUP, SIG_IGN) == SIG_ERR) {
        log_error("signal(SIGHUP, SIG_IGN) failed: %s", strerror(errno));
        return VR_ERROR;
    }

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
//打印信息
static void
vr_print_run(struct instance *nci)
{
    int status;
    struct utsname name;
    
    status = uname(&name);

    //--------------------------------------------------------------
    //if(SHOWIMG)
    //{
        ///
        //
        char *ascii_logo1 =
"                                                         \n \
        ,/  \\.                                           \n \
       |(    )|                                           \n \
  \\`-._:,\\  /.;_,-'/                                    \n \
   `.\\_`\\')(`/'_/,'                                     \n \ 
       )/`.,'\\(                                          \n \
       |.    ,|                                           \n \
       :6)  (6;                                           \n \
        \\`\\ _(\\                                        \n \
         \\._'; `.___...---..________...------._          \n \
         \\   |   ,'   .  .     .       .     .`:.        \n \
           \\`.' .  .         .   .   .     .   . \\      \n \
            `.       .   .  \\  .   .   ..::: .    ::     \n \
              \\ .    .  .   ..::::::::''  ':    . ||     \n \
               \\   `. :. .:'            \\  '. .   ;;    \n \
                `._  \\ ::: ;           _,\  :.  |/(      \n \
                   `.`::: /--....---''' \\ `. :. :`\\`    \n \
                    | |:':               \\  `. :.\\      \n \
                    | |' ;                \\  (\\  .\\    \n \
                    | |.:                  \\  \\`.  :    \n \
                    |.| |                   ) /  :.|      \n \
                    | |.|                  /./   | |      \n \
                    |.| |                 / /    | |      \n \
                    | | |                /./     |.|      \n \
                    ;_;_;              ,'_/      ;_|      \n \
                   '-/_(              '--'      /,'       \n \
"  ; 
	printf("%s",ascii_logo1);

    //}
    
//-------------------------------------------------------------

    if (nci->log_filename) {
        char *ascii_logo = ascii_logo1;
//"                _._                                                  \n"
//"           _.-``__ ''-._                                             \n"
//"      _.-``    `.  *_.  ''-._           Vire %s %s bit\n"
//"  .-`` .-```.  ```\-/    _.,_ ''-._                                   \n"
//" (    |      |       .-`    `,    )     Running in %s mode\n"
//" |`-._`-...-` __...-.``-._;'` _.-'|     Port: %d\n"
//" |    `-._   `._    /     _.-'    |     PID: %ld\n"
//"  `-._    `-._  `-./  _.-'    _.-'      OS: %s %s %s\n"
//" |`-._`-._    `-.__.-'    _.-'_.-'|                                  \n"
//" |    `-._`-._        _.-'_.-'    |     https://github.com/vipshop/vire\n"
//"  `-._    `-._`-.__.-'_.-'    _.-'                                   \n"
//" |`-._`-._    `-.__.-'    _.-'_.-'|                                  \n"
//" |    `-._`-._        _.-'_.-'    |                                  \n"
//"  `-._    `-._`-.__.-'_.-'    _.-'                                   \n"
//"      `-._    `-.__.-'    _.-'                                       \n"
//"          `-._        _.-'                                           \n"
//"              `-.__.-'                                               \n\n";
    //printf("%s",ascii_logo);
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
//打印使用手册
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
//创建pid文件
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

//移除pid文件
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
//设置缺省的配置
static void
vr_set_default_options(struct instance *nci)
{
    
    int status;

    nci->log_level = VR_LOG_DEFAULT;
    nci->log_filename = VR_LOG_PATH;

    nci->conf_filename = VR_CONF_PATH;


    status = vr_gethostname(nci->hostname, VR_MAXHOSTNAMELEN);
    if (status < 0) {
        log_warn("gethostname failed, ignored: %s", strerror(errno));
        dsnprintf(nci->hostname, VR_MAXHOSTNAMELEN, "unknown");
    }
    nci->hostname[VR_MAXHOSTNAMELEN - 1] = '\0';

    nci->pid = (pid_t)-1;
    nci->pid_filename = NULL;
    nci->pidfile = 0;

    nci->thread_num = (int)VR_THREAD_NUM_DEFAULT;
}
//得到特定的命令行参数，并且设置全局的变量
static rstatus_t
vr_get_options(int argc, char **argv, struct instance *nci)
{
    int c, value;

    opterr = 0;

    for (;;) {
        //解析命令行参数
        c = getopt_long(argc, argv, short_options, long_options, NULL);
        if (c == -1) {
            /* no more options */
            break;
        }

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
//测试配置文件
static bool
vr_test_conf(struct instance *nci, int test)
{
    vr_conf *cf;
    //创建配置树
    cf = conf_create(nci->conf_filename);
    //创建失败时打印日志
    if (cf == NULL) {
        if (test)
            log_stderr("vire: configuration file '%s' syntax is invalid",
                nci->conf_filename);
        return false;
    }
    //销毁配置树
    conf_destroy(cf);

    if (test)
        log_stderr("vire: configuration file '%s' syntax is ok",
            nci->conf_filename);
    return true;
}

//预先启动的函数
static int
vr_pre_run(struct instance *nci)
{
    int ret;
    //初始化日志
    ret = log_init(nci->log_level, nci->log_filename);
    if (ret != VR_OK) {
        return ret;
    }
    //记录日志
    log_debug(LOG_VERB, "Vire used logfile: %s", nci->conf_filename);

    if (!vr_test_conf(nci, false)) {
        log_error("conf file %s is error", nci->conf_filename);
        return VR_ERROR;
    }
    //创建守护进程
    if (daemonize) {
        ret = vr_daemonize(1);
        if (ret != VR_OK) {
            return ret;
        }
    }
    //得到pid
    nci->pid = getpid();
    //初始化信号函数
    ret = signal_init();
    if (ret != VR_OK) {
        return ret;
    }
    //创建pid文件
    if (nci->pid_filename) {
        ret = vr_create_pidfile(nci);
        if (ret != VR_OK) {
            return VR_ERROR;
        }
    }
    //初始化server
    ret = init_server(nci);
    if (ret != VR_OK) {
        return VR_ERROR;
    }
    //打印图标
    vr_print_run(nci);

    return VR_OK;
}
//
static void
vr_post_run(struct instance *nci)
{
    /* deinit the threads */
    //析构worker线程
    workers_deinit();
    //析构后台线程
    backends_deinit();
    //析构主线程
    master_deinit();
    //移除pid文件
    if (nci->pidfile) {
        vr_remove_pidfile(nci);
    }
    //卸载信号处理函数
    signal_deinit();
    //打印结束信息
    vr_print_done();
    //日志结构析构
    log_deinit();
}

//主运行函数
static void
vr_run(struct instance *nci)
{
    //校验线程数目
    if (nci->thread_num <= 0) {
        log_error("number of work threads must be greater than 0");
        return;
    } else if (nci->thread_num > 64) {
        log_warn("WARNING: Setting a high number of worker threads is not recommended."
            " Set this value to the number of cores in your machine or less.");
    }

    /* run the threads */
    //运行主线程
    master_run();
    //运行worker线程
    workers_run();
    //运行后台线程
    backends_run();

    /* wait for the threads finish */
    //等待worker退出
    workers_wait();
    //等待后台线程退出
    backends_wait();
}

//main函数
int
main(int argc, char **argv)
{
    rstatus_t status;
    struct instance nci;
//设置默认的参数配置
    vr_set_default_options(&nci);
    //获取命令行的参数：
    status = vr_get_options(argc, argv, &nci);
    if (status != VR_OK) {
        //出错时打印使用帮助
        vr_show_usage();
        exit(1);
    }
    //处理version命令
    if (show_version) {
        log_stderr("This is vire-%s" CRLF, VR_VERSION_STRING);
        if (show_help) {
            vr_show_usage();
        }
        exit(0);
    }
    //测试配置文件
    if (test_conf) {
        if (!vr_test_conf(&nci, true)) {
            exit(1);
        }
        exit(0);
    }
    //预先启动的函数
    status = vr_pre_run(&nci);
    if (status != VR_OK) {
        vr_post_run(&nci);
        exit(1);
    }

    //得到程序的全路径
    server.executable = getAbsolutePath(argv[0]);
    //运行函数
    vr_run(&nci);
    //析构资源
    vr_post_run(&nci);

    return VR_OK;
}

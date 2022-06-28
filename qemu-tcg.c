
#define NOCPU
#include "qemu/osdep.h"
#include <sys/ipc.h>
#include <sys/shm.h>
#include "qemu/error-report.h"
#include "qemu-version.h"
#include "qemu-common.h"
#define NB_OPS 0
#include "tcg/profile.h"

#define QEMU_TCG_VERSION "qemu-tcg version " QEMU_FULL_VERSION  \
                          "\n" QEMU_COPYRIGHT "\n"

typedef struct tcg_cmd_t {
    const char *name;
    int (*handler)(int argc, char **argv);
} tcg_cmd_t;

static QEMU_NORETURN G_GNUC_PRINTF(1, 2)
void error_exit(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    error_vreport(fmt, ap);
    va_end(ap);

    error_printf("Try 'qemu-tcg --help' for more information\n");
    exit(EXIT_FAILURE);
}

static QEMU_NORETURN
void missing_argument(const char *option)
{
    error_exit("missing argument for option '%s'", option);
}

static QEMU_NORETURN
void unrecognized_option(const char *option)
{
    error_exit("unrecognized option '%s'", option);
}

static QEMU_NORETURN
void help(void)
{
    const char *help_msg =
           QEMU_TCG_VERSION
           "usage: qemu-tcg [standard options] key command [command options]\n"
           "QEMU TCG tool\n\n"
           "commands:\n"
           "prof - show profiling information\n"
           "del - delete the shm key\n";

    printf("%s", help_msg);
    printf("\n\n" QEMU_HELP_BOTTOM "\n");
    exit(EXIT_SUCCESS);
}

    key_t tcg_key;
int tcg_id;

static void *attach_shm_region(key_t key, size_t size)
{
    void *s;

    tcg_id = shmget(key, size, 0600);
    if (tcg_id == -1) {
        fprintf(stderr, "shm key %d shm find error: %s\n",
                key, strerror(errno));
        exit(1);
    }
    s = shmat(tcg_id, NULL, 0);
    if (s == ((void *) -1)) {
        fprintf(stderr, "shm key %d shm attach error: %s\n",
                key, strerror(errno));
        exit(1);
    }
    return s;
}

static TCGProfile *prof;

static const char *tb_flush_type_str(int type)
{
    switch(type) {
    case TB_FLUSH_TYPE_FORK:  return "f";
    case TB_FLUSH_TYPE_EXIT:  return "e";
    case TB_FLUSH_TYPE_SHMAT: return "s";
    case TB_FLUSH_TYPE_MMAP:  return "m";
    case TB_FLUSH_TYPE_SPAPR: return "p";
    default: return "?";
    }
}

static int tcg_prof_handler(int argc, char **argv)
{
#ifndef CONFIG_PROFILER
    error_exit("Profiler not enabled in qemu\n");
#else
    int i;
    int *type = prof->tb_flush_pcs_type;
    uint64_t *pcs = prof->tb_flush_pcs;
#define DUMP64(v) printf("%s:\t%lld\n", stringify(v), (long long) prof->v)
#define DUMP32(v) printf("%s:\t%d\n", stringify(v), prof->v)
#define DUMP(v) printf("%s:\t%d\n", stringify(v), prof->v)
    DUMP64(cpu_exec_time);
    DUMP64(tb_count1);
    DUMP64(tb_count);
    DUMP64(op_count);
    DUMP(op_count_max);
    DUMP(temp_count_max);
    DUMP64(temp_count);
    DUMP64(del_op_count);
    DUMP64(code_in_len);
    DUMP64(code_out_len);
    DUMP64(search_out_len);
    DUMP64(interm_time);
    DUMP64(code_time);
    DUMP64(la_time);
    DUMP64(opt_time);
    DUMP64(restore_count);
    DUMP64(restore_time);
    DUMP64(tb_flush);
    DUMP64(tb_flush_when_full);
    DUMP64(tb_flush_cpu);
    DUMP64(tb_flush_gdbstub);
    DUMP64(tb_flush_mmap);
    DUMP64(tb_flush_shmat);
    DUMP64(tb_flush_fork);
    DUMP64(tb_flush_loader);
    DUMP64(tb_flush_exit);
    DUMP64(tb_flush_spapr);
    DUMP32(tb_flush_pcs_pos);
    for (i = 0; i < NR_TB_FLUSH_PCS; i += 4) {
        printf("%3d: %s:%16.16" PRIx64 " %s:%16.16" PRIx64
               " %s:%16.16" PRIx64 " %s:%16.16" PRIx64 "\n",
               i,
               tb_flush_type_str(type[i]), pcs[i],
               tb_flush_type_str(type[i + 1]), pcs[i + 1],
               tb_flush_type_str(type[i + 2]), pcs[i + 2],
               tb_flush_type_str(type[i + 3]), pcs[i + 3]);
    }
    for (i = 0; i < prof->num_ops; i++) {
        printf("table_op_count[%d]:\t%lld\n", i,
               (long long) prof->table_op_count[i]);
    }
#undef DUMP64
#undef DUMP
    return 0;
#endif
}

static int tcg_del_handler(int argc, char **argv)
{
    int rv;

    rv = shmctl(tcg_id, IPC_RMID, NULL);
    if (rv == -1) {
        error_exit("Unable to delete key %d: %s", tcg_key, strerror(errno));
    }
    return 0;
}

static const tcg_cmd_t tcg_cmds[] = {
    { .name = "prof", .handler = tcg_prof_handler },
    { .name = "del", .handler = tcg_del_handler },
    { NULL }
};

int main(int argc, char **argv)
{
    const tcg_cmd_t *cmd;
    const char *cmdname;
    int c;
    static const struct option long_options[] = {
        {"help", no_argument, 0, 'h'},
        {"version", no_argument, 0, 'V'},
        {0, 0, 0, 0}
    };

    while ((c = getopt_long(argc, argv, "+:hV", long_options, NULL)) != -1) {
        switch (c) {
        case ':':
            missing_argument(argv[optind - 1]);
            return 0;
        case '?':
            unrecognized_option(argv[optind - 1]);
            return 0;
        case 'h':
            help();
            return 0;
        case 'V':
            printf(QEMU_TCG_VERSION);
            return 0;
        }
    }

    argc -= optind;
    if (argc < 1) {
        error_exit("No tcg key given");
    }
    tcg_key = strtol(argv[optind], NULL, 0);
    prof = attach_shm_region(tcg_key, sizeof(*prof));
    argc--;
    optind++;

    cmdname = argv[optind];

    /* reset getopt_long scanning */
    if (argc < 1) {
        return 0;
    }
    argv += optind;
    qemu_reset_optind();

    /* find the command */
    for (cmd = tcg_cmds; cmd->name != NULL; cmd++) {
        if (!strcmp(cmdname, cmd->name)) {
            return cmd->handler(argc, argv);
        }
    }

    /* not found */
    error_exit("Command not found: %s", cmdname);
}

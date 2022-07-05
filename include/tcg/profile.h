
#define TB_FLUSH_TYPE_FORK      1
#define TB_FLUSH_TYPE_EXIT      2
#define TB_FLUSH_TYPE_SHMAT     3
#define TB_FLUSH_TYPE_MMAP      4
#define TB_FLUSH_TYPE_SPAPR     5

#define NR_TB_FLUSH_PCS 128 /* Must be a power of 2 */
#define NR_TB_INSERT_FAILS 128 /* Must be a power of 2 */

struct prof_tb_insert_fail {
    uint64_t pc;
    uint64_t cs_base;
    uint64_t page_addr[2];
    uint32_t flags;
    uint32_t cflags;
    uint32_t trace_vcpu_dstate;
};

typedef struct TCGProfile {
    int64_t cpu_exec_time;
    int64_t tb_count1;
    int64_t tb_count;
    int64_t op_count; /* total insn count */
    int op_count_max; /* max insn per TB */
    int temp_count_max;
    int64_t temp_count;
    int64_t del_op_count;
    int64_t code_in_len;
    int64_t code_out_len;
    int64_t search_out_len;
    int64_t interm_time;
    int64_t code_time;
    int64_t la_time;
    int64_t opt_time;
    int64_t restore_count;
    int64_t restore_time;
    int64_t tb_flush;
    int64_t tb_flush_when_full;
    int64_t tb_flush_cpu;
    int64_t tb_flush_gdbstub;
    int64_t tb_flush_mmap;
    int64_t tb_flush_shmat;
    int64_t tb_flush_fork;
    int64_t tb_flush_loader;
    int64_t tb_flush_exit;
    int64_t tb_flush_spapr;
    int64_t tb_jmp_hash_collision;
    int64_t tb_hash_physpc_bad;
    int64_t tb_hash_lookup_fail;
    int64_t tb_hash_lookup_succeed1;
    int64_t tb_hash_lookup_succeed2;
    int64_t tb_hash_lookup_fail1;
    int64_t tb_hash_lookup_fail2;
    int64_t tb_hash_lookup_fail3;
    int64_t tb_hash_lookup_fail4;
    int64_t tb_hash_lookup_fail5;
    int64_t tb_hash_lookup_fail6;
    int64_t tb_hash_lookup_fail7;
    int64_t tb_hash_insert_fail;
    struct prof_tb_insert_fail fail_old[NR_TB_INSERT_FAILS];
    struct prof_tb_insert_fail fail_new[NR_TB_INSERT_FAILS];
    int64_t tb_overflow;
    int64_t tb_buffer_overflow1;
    int64_t tb_buffer_overflow2;
    int tb_flush_pcs_type[NR_TB_FLUSH_PCS];
    uint64_t tb_flush_pcs[NR_TB_FLUSH_PCS];
    uint32_t tb_flush_pcs_pos;
    int num_ops;
    int64_t table_op_count[NB_OPS];
} TCGProfile;

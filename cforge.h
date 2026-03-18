#define _POSIX_C_SOURCE 200809L

#ifndef CFORGE_H
#define CFORGE_H

#if 0
[ "cforge.c" -nt ".b" ] || [ "cforge.h" -nt ".b" ] && cc -O2 -Wall -Wextra -Wshadow -Wpedantic -Wconversion -Wstrict-prototypes -Wformat=2 -Wmissing-prototypes -Wold-style-definition -Wdouble-promotion -Wno-unused-parameter -std=c11 "cforge.c" -o "./.b"
exec "./.b" "$@"
exit 0
#endif

#include <glob.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* TODO: Port this to Windows someday */
#include <sys/stat.h>

/* TODO: Add other threading implementations (pthreads, WinAPI) */
#ifdef CF_ENABLE_PARALLEL
#ifdef __STDC_NO_THREADS__
#error I need threads to build this program!
#else
#include <threads.h>
#endif // __STDC_NO_THREADS__
#endif // CF_ENABLE_PARALLEL

/* TODO: Port this environment variable system to Windows */
extern char** environ;
uint64_t denv_hash = 0;
uint64_t cenv_hash = 0;

#define CF_MAX_TARGETS 64
#define CF_MAX_CONFIGS 64
#define CF_MAX_GLOBS 64
#define CF_MAX_THRDS 16
#define CF_MAX_JOBS 64
#define CF_MAX_ENVS 256
#define CF_MAX_JOIN_STRINGS 256
#define CF_MAX_MAP_ATTRS 8
#define CF_MAX_MAPS 64
#define CF_INIT_PENDING_ENTRIES 64
#define CF_INIT_PENDING_STRING_SZ (4 * 1024)

#define CF_MAGIC_HEADER_VALUE 0xDBCF
#define CF_DB_CVERSION 0x4

#define CF_MAX_NAME_LENGTH 127
#define CF_MAX_OUTSTR_LENGTH 511
#define CF_MAX_COMMAND_LENGTH (1 * 1024)
#define CF_MAX_JOIN_STRING_LEN 8192

#define CF_ERR_LOG(...) fprintf(stderr, __VA_ARGS__)
#define CF_WRN_LOG(...) fprintf(stdout, __VA_ARGS__)

#define CF_SUCCESS_EC 0
#define CF_MAX_TARGETS_EC 1
#define CF_TARGET_NOT_FOUND_EC 2
#define CF_MAX_CONFIGS_EC 3
#define CF_INVALID_STATE_EC 4
#define CF_NAME_TOO_LONG_EC 5
#define CF_CLIB_FAIL_EC 6
#define CF_MAX_GLOBS_EC 7
#define CF_MAX_COMMAND_LENGTH_EC 8
#define CF_TARGET_DEP_CYCLE_EC 10
#define CF_CONFIG_NOT_FOUND_EC 11
#define CF_UNKNOWN_ATTR_EC 12
#define CF_MAX_ENVS_EC 13
#define CF_MAX_JSTRINGS_EC 14
#define CF_MAX_MAPS_EC 15
#define CF_IMPOSSIBLE_EC 16
#define CF_DB_IO_FAIL 17
#define CF_DB_MAGIC_FAIL 18
#define CF_DB_VERSION_FAIL 19
#define CF_DB_OOM_EC 20

typedef void (*cf_target_fn)(void);
typedef void (*cf_config_fn)(void);


typedef enum {
    UNKNOWN = 0,
    DEPENDENCY,
#ifdef CF_ENABLE_CONFIG
    CONFIG_SET,
#endif // CF_ENABLE_CONFIG
} cf_attr_type_t;

typedef struct {
    cf_attr_type_t type;
    union {
        struct {
            const char* target_name;
        } depends;
        struct {
            const char* config_name;
        } configset;
    } arg;
} cf_attr_t;

typedef enum {
    UNVISITED = 0,
    VISITING,
    DONE
} cf_dfs_node_status_t;

typedef struct {
    const char* name;
    cf_target_fn fn;
    cf_attr_t* attribs;
    size_t attribs_size;
    cf_dfs_node_status_t node_status;
} cf_target_decl_t;

typedef enum {
    REGISTER_PHASE = 0,
    TARGET_PLAN_PHASE = 1,
    TARGET_EXECUTE_PHASE = 2,
} cf_state_t;

static cf_target_decl_t cf_targets[CF_MAX_TARGETS] = { 0 };
static size_t cf_num_targets = 0;

#ifdef CF_ENABLE_CONFIG

typedef struct {
    const char* name;
    cf_config_fn fn;
} cf_config_decl_t;

typedef struct {
    const char* envname;
    char* value;
    bool was_set;
} cf_env_restore_t;

static cf_config_decl_t cf_configs[CF_MAX_CONFIGS] = { 0 };
static size_t cf_num_configs = 0;

static cf_env_restore_t cf_envs[CF_MAX_ENVS] = { 0 };
static size_t cf_num_envs = 0;

#endif // CF_ENABLE_CONFIG

#ifdef CF_ENABLE_UTILS

typedef struct {
    size_t c;
    char** p;
} cf_glob_t;

typedef enum {
    MAP_UNKNOWN = 0,
    MAP_EXT,
    MAP_PARENT,
} cf_map_attr_type_t;

typedef struct {
    cf_map_attr_type_t type;
    union {
        struct {
            char* n_ext;
        };
        struct {
            char* n_parent;
        };
    };
} cf_map_attr_t;

typedef struct {
    char** oarray;
    size_t size;
} cf_map_entry_t;

static glob_t cf_globs[CF_MAX_GLOBS] = { 0 };
static size_t cf_num_globs = 0;

static char* cf_jstrings[CF_MAX_JOIN_STRINGS] = { 0 };
static size_t cf_num_jstrings = 0;

static cf_map_entry_t cf_maps[CF_MAX_MAPS] = { 0 };
static size_t cf_num_maps = 0;

#endif // CF_ENABLE_UTILS

static cf_state_t cf_state = REGISTER_PHASE;

static size_t cf_find_target_index(const char* target_name) {
    for (size_t i = cf_num_targets; i-- > 0;) {
        if (strncmp(target_name, cf_targets[i].name, CF_MAX_NAME_LENGTH) == 0) {
            return i;
        }
    }

    return CF_MAX_TARGETS + 1;
}

#ifdef CF_ENABLE_PARALLEL

typedef struct {
    char* command;
} cf_thrd_job;

typedef struct {
    cf_thrd_job jobs[CF_MAX_JOBS];
    uint16_t active_jobs;
    int32_t front;
    int32_t back;
    mtx_t lock;
    cnd_t free_slot;
    cnd_t new_job;
    cnd_t no_job;
} cf_work_queue;

static cf_work_queue* global_workq = NULL;

static thrd_t cf_thrd_pool[CF_MAX_THRDS] = { 0 };
static size_t cf_num_thrds = 0;

static inline bool cf_empty_job(void) {
    return global_workq->front == global_workq->back;
}

static inline bool cf_full_job(void) {
    return ((global_workq->back + 1) % CF_MAX_JOBS) == global_workq->front;
}

static bool cf_enqueue_job(cf_thrd_job job) {
    if (cf_full_job()) {
        return false;
    }

    global_workq->jobs[global_workq->back] = job;
    global_workq->back = (global_workq->back + 1) % CF_MAX_JOBS;
    return true;
}

static bool cf_dequeue_job(cf_thrd_job* job) {
    if (cf_empty_job()) {
        return false;
    }

    *job = global_workq->jobs[global_workq->front];
    global_workq->jobs[global_workq->front] = (cf_thrd_job) { 0 };
    global_workq->front = (global_workq->front + 1) % CF_MAX_JOBS;
    return true;
}

static int cf_thrd_helper(void* queue) {
    cf_work_queue* q = (cf_work_queue*) queue;
    cf_thrd_job job;
    mtx_t* lock = &q->lock;

    while (true) {
        mtx_lock(lock);
        while (cf_empty_job()) {
            if (q->active_jobs == 0) {
                cnd_signal(&q->no_job);
            }
            cnd_wait(&q->new_job, lock);
        }
        cf_dequeue_job(&job);
        cnd_signal(&q->free_slot);
        mtx_unlock(lock);

        if (job.command == NULL) {
            break;
        }

        if (system((char*) job.command) != 0) {
            CF_ERR_LOG("Error: Executing command \"%s\" failed\n", (char*) job.command);
            exit(CF_CLIB_FAIL_EC);
        }

        free(job.command);
        mtx_lock(lock);
        --q->active_jobs;
        mtx_unlock(lock);
    }
    
    return 0;
}


#endif // CF_ENABLE_PARALLEL

#ifdef CF_ENABLE_UTDDB

typedef struct {
    /* Magic header should be CFDB */
    uint16_t magic_header;
    uint16_t version;
    uint32_t reserved;
    size_t entry_cnt;
    size_t string_sz;
} cf_db_hdr_t __attribute__((aligned(8)));

typedef struct {
    uint64_t path_hash;
    uint64_t env_hash;
    uint64_t content_hash;
    uint64_t mtime;
    uint64_t size;
    size_t path_offset;
} cf_db_entry_t __attribute__((aligned(8)));

/* Technically never used */
typedef struct {
    /* Maximum path on Linux is 4KiB by default */
    uint16_t len;
    char* string;
} cf_db_lstring_t __attribute__((aligned(2)));

typedef struct {
    cf_db_hdr_t* header;
    cf_db_entry_t* entries;
    cf_db_lstring_t* strings;
    cf_db_entry_t* pending_entries;
    cf_db_lstring_t* pending_strings;
    /* max index */
    size_t pentries_max;
    size_t pentries_idx;
    size_t pstrings_sz;
    size_t pstrings_off;
} cf_db_mem_t;

static cf_db_mem_t* global_db = NULL;

#endif // CF_ENABLE_UTDDB


#ifdef CF_ENABLE_UTILS

static cf_glob_t __attribute__((unused)) cf_glob(const char* expr) {
    glob_t glob_res = { 0 };
    int32_t rc = glob(expr, GLOB_NOSORT | GLOB_MARK | GLOB_NOESCAPE, NULL, &glob_res);
    
    if (rc == GLOB_NOMATCH) {
        globfree(&glob_res);
        return (cf_glob_t) {
            .c = 0,
            .p = NULL,
        };
    } else if (rc == GLOB_NOSPACE) {
        CF_ERR_LOG("Error: glob() ran out of memory during cf_glob() call!\n");
        exit(CF_CLIB_FAIL_EC);
    } else if (rc == GLOB_ABORTED) {
        CF_ERR_LOG("Error: glob() aborted due to a read error during cf_glob() call!\n");
        exit(CF_CLIB_FAIL_EC);
    }

    if (cf_num_globs >= CF_MAX_GLOBS) {
        CF_ERR_LOG("Error: Maximum globs of %d was reached!\n", CF_MAX_GLOBS);
        exit(CF_MAX_GLOBS_EC);
    }

    cf_globs[cf_num_globs++] = glob_res;
    return (cf_glob_t) {
        .c = glob_res.gl_pathc,
        .p = glob_res.gl_pathv,
    };
}

static void cf_free_glob(size_t checkpoint) {
    while (cf_num_globs > checkpoint) {
        globfree(&cf_globs[--cf_num_globs]);
        cf_globs[cf_num_globs] = (glob_t) { 0 };
    }
}

static char* __attribute__((unused)) cf_join(char* strings[], char* separator, size_t length) {
    if (length < 1) {
        return (char*) "";
    }

    if (cf_num_jstrings >= CF_MAX_JOIN_STRINGS) {
        CF_ERR_LOG("Error: Maximum joined strings of %d was reached!\n", CF_MAX_JOIN_STRINGS);
        exit(CF_MAX_JSTRINGS_EC);
    }

    char* jstring = (char*) malloc(CF_MAX_JOIN_STRING_LEN);
    if (jstring == NULL) {
        CF_ERR_LOG("Error: malloc() failed in cf_join()\n");
        exit(CF_CLIB_FAIL_EC);
    }

    cf_jstrings[cf_num_jstrings++] = jstring;

    const char* endptr = jstring + CF_MAX_JOIN_STRING_LEN - 1;
    char* cptr = stpncpy(jstring, strings[0], (size_t) (endptr - jstring));
    for (size_t i = 1; i < length; i++) {
        cptr = stpncpy(cptr, separator, (size_t) (endptr - cptr));
        cptr = stpncpy(cptr, strings[i], (size_t) (endptr - cptr));
    }

    *cptr = '\0';
    return jstring;
}

static void cf_free_jstrings(size_t checkpoint) {
    while (cf_num_jstrings > checkpoint) {
        free(cf_jstrings[--cf_num_jstrings]);
        cf_jstrings[cf_num_jstrings] = NULL;
    }
}

static char** cf_map(const char** sources, size_t src_length, cf_map_attr_t* attrs, size_t attr_length) {
    if (cf_num_maps >= CF_MAX_MAPS) {
        CF_ERR_LOG("Error: Maximum maps of %d was reached!\n", CF_MAX_MAPS);
        exit(CF_MAX_MAPS_EC);
    }
    
    
    char** oarray = (char**) malloc(src_length * sizeof(char*));
    if (oarray == NULL) {
        CF_ERR_LOG("Error: malloc() failed in cf_map()\n");
        exit(CF_CLIB_FAIL_EC);
    }

    cf_maps[cf_num_maps++] = (cf_map_entry_t) {
        .oarray = oarray,
        .size = src_length
    };

    size_t oarray_idx = 0;
    for (size_t i = 0; i < src_length; i++) {
        char* outstr = (char*) malloc(CF_MAX_OUTSTR_LENGTH);
        if (outstr == NULL) {
            CF_ERR_LOG("Error: malloc() failed for outstr in cf_map()\n");
            exit(CF_CLIB_FAIL_EC);
        }

        strcpy(outstr, sources[i]);
        if (oarray_idx >= src_length) {
            CF_ERR_LOG("Error: Impossible error in cf_map()\n");
            exit(CF_IMPOSSIBLE_EC);
        }

        for (size_t j = 0; j < attr_length; j++) {
            cf_map_attr_t attr = attrs[j];
            switch (attr.type) {
                case MAP_EXT: {
                    const char* dot = strrchr(outstr, '.');
                    size_t length = (size_t) (dot - outstr + 1);
                    size_t ext_len = strlen(attr.n_ext);
                    memcpy(outstr + length, attr.n_ext, ext_len);
                    outstr[length + ext_len] = '\0';
                    break;
                }
                case MAP_PARENT: {
                    /* TODO: Sanitize Windows path... */
                    const char* slash = strchr(outstr, '/');
                    if (slash == NULL) {
                        CF_WRN_LOG("Warning: No parent directory to replace in cf_map()\n");
                        break;
                    }

                    size_t length = strlen(attr.n_parent);
                    memmove(outstr + length, slash, strlen(slash) + 1);
                    memcpy(outstr, attr.n_parent, length);
                    break;
                }
                case MAP_UNKNOWN: {
                    CF_ERR_LOG("Error: MAP_UNKNOWN attribute detected!");
                    exit(CF_UNKNOWN_ATTR_EC);
                }
            }
         }

        oarray[oarray_idx++] = outstr;
    }

    return oarray;
}

static void cf_free_maps(size_t checkpoint) {
    while (cf_num_maps > checkpoint) {
        cf_map_entry_t entry = cf_maps[--cf_num_maps];
        size_t rem_elems = entry.size;
        while (rem_elems > 0) {
            free(entry.oarray[--rem_elems]);
        }

        free(entry.oarray);
        cf_maps[cf_num_maps] = (cf_map_entry_t) { 0 };
    }

}

#endif // CF_ENABLE_UTILS


#ifdef CF_ENABLE_CONFIG

static void __attribute__((unused)) cf_register_config(const char* name, cf_config_fn fn) {
    if (cf_state != REGISTER_PHASE) {
        CF_ERR_LOG("Error: Invalid cf_state (%d) when registering config!\n", cf_state);
        exit(CF_INVALID_STATE_EC);
    }

    if (strlen(name) > CF_MAX_NAME_LENGTH) {
        CF_ERR_LOG("Error: The name \"%s\" when registering config is too long (max name length: %d)\n", name, CF_MAX_NAME_LENGTH);
        exit(CF_NAME_TOO_LONG_EC);
    }

    if (cf_num_configs >= CF_MAX_CONFIGS) {
        CF_ERR_LOG("Error: Maximum configs of %d was reached!\n", CF_MAX_CONFIGS);
        exit(CF_MAX_CONFIGS_EC);
    }

    cf_configs[cf_num_configs++] = (cf_config_decl_t) {
        .name = name,
        .fn = fn
    };
}

static void __attribute__((unused)) cf_setenv_wrapper(const char* ident, char* value) {
    if (cf_num_envs >= CF_MAX_ENVS) {
        CF_ERR_LOG("Error: Maximum environment variables of %d was reached!\n", CF_MAX_ENVS);
        exit(CF_MAX_ENVS_EC);
    }

    char* envvar = getenv(ident);
    if (envvar == NULL) {
        cf_envs[cf_num_envs++] = (cf_env_restore_t) {
            .envname = ident,
            .value = NULL,
            .was_set = false
        };
    } else {
        char* value_block = (char*) malloc(strlen(envvar) + 1);
        if (value_block == NULL) {
            CF_ERR_LOG("Error: malloc() failed in cf_setenv_wrapper()\n");
            exit(CF_CLIB_FAIL_EC);
        }

        strcpy(value_block, envvar);
        cf_envs[cf_num_envs++] = (cf_env_restore_t) {
            .envname = ident,
            .value = value_block,
            .was_set = true
        };
    }

    if (setenv(ident, value, 1) != 0) {
        CF_ERR_LOG("Error: setenv() failed in cf_setenv_wrapper()\n");
        exit(CF_CLIB_FAIL_EC);
    }
}

static void  cf_restore_env(size_t env_checkpoint) {
    cf_env_restore_t envres;
    while (cf_num_envs > env_checkpoint) {
        envres = cf_envs[--cf_num_envs];
        if (!envres.was_set) {
            if (unsetenv(envres.envname) != 0) {
                CF_ERR_LOG("Error: unsetenv() failed in cf_restore_env()\n");
                exit(CF_CLIB_FAIL_EC);
            }
        } else {
            if (setenv(envres.envname, envres.value, 1) != 0) {
                CF_ERR_LOG("Error: setenv() failed in cf_restore_env()\n");
                exit(CF_CLIB_FAIL_EC);
            }
        }

        free(envres.value);
    }
}

#endif // CF_ENABLE_CONFIG


#ifdef CF_ENABLE_UTDDB

/* CForge DB implementation */
static void cf_db_free(cf_db_mem_t* db) {
    if (db == NULL) {
        return;
    }

    cf_db_hdr_t* hdr = db->header;
    if (hdr != NULL) {
        free(hdr);
        db->header = NULL;
    }

    cf_db_entry_t* entries = db->entries;
    if (entries != NULL) {
        free(entries);
        db->entries = NULL;
    }

    cf_db_lstring_t* strings = db->strings;
    if (strings != NULL) {
        free(strings);
        db->strings = NULL;
    }

    cf_db_entry_t* pentries = db->pending_entries;
    if (pentries != NULL) {
        free(pentries);
        db->pending_entries = NULL;
    }

    cf_db_lstring_t* pstrings = db->pending_strings;
    if (pstrings != NULL) {
        free(pstrings);
        db->pending_strings = NULL;
    }

    free(db);
}

static cf_db_mem_t* cf_db_load(const char* db_path) {
    FILE* fp = fopen(db_path, "rb");

    cf_db_mem_t* db = (cf_db_mem_t*) malloc(sizeof(cf_db_mem_t));
    memset(db, 0, sizeof(cf_db_mem_t));

    cf_db_hdr_t* hdr = (cf_db_hdr_t*) malloc(sizeof(cf_db_hdr_t));
    cf_db_entry_t* pentries = (cf_db_entry_t*) malloc(CF_INIT_PENDING_ENTRIES * sizeof(cf_db_entry_t));
    cf_db_lstring_t* pstrings = (cf_db_lstring_t*) malloc(CF_INIT_PENDING_STRING_SZ);
    if (db == NULL || hdr == NULL || pentries == NULL || pstrings == NULL) {
        CF_ERR_LOG("Error: malloc() failed in cf_load_db() for db_mem\n");
        if (fp != NULL) {
            fclose(fp);
        }
        cf_db_free(db);
        exit(CF_CLIB_FAIL_EC);
    }

    db->header = hdr;
    db->pending_entries = pentries;
    db->pending_strings = pstrings;
    db->pentries_max = CF_INIT_PENDING_ENTRIES;
    db->pstrings_sz = CF_INIT_PENDING_STRING_SZ;
    db->pentries_idx = 0;
    db->pstrings_off = 0;

    /* Default when DB not found */
    if (fp == NULL) {
        CF_WRN_LOG("Warning: DB at path not found, using default\n");
        hdr->magic_header = CF_MAGIC_HEADER_VALUE;
        hdr->version = CF_DB_CVERSION;
        hdr->reserved = 0;
        hdr->entry_cnt = 0;
        hdr->string_sz = 0;
        db->entries = NULL;
        db->strings = NULL;
        return db;
    }

    if (fread(hdr, sizeof(cf_db_hdr_t), 1, fp) != 1) {
        CF_ERR_LOG("Error: Could not read database header\n");   
        fclose(fp);
        cf_db_free(db);
        exit(CF_DB_IO_FAIL);
    }

    if (hdr->magic_header != CF_MAGIC_HEADER_VALUE) {
        CF_ERR_LOG("Error: Magic database header code is invalid!\n");
        fclose(fp);
        cf_db_free(db);
        exit(CF_DB_MAGIC_FAIL);
    }

    if (hdr->version != CF_DB_CVERSION) {
        CF_ERR_LOG("Error: CForge version (v%d) does not match database version (v%d)\n", CF_DB_CVERSION, hdr->version);
        fclose(fp);
        cf_db_free(db);
        exit(CF_DB_VERSION_FAIL);
    }

    cf_db_entry_t* entries = (cf_db_entry_t*) malloc(hdr->entry_cnt * sizeof(cf_db_entry_t));
    if (entries == NULL) {
        CF_ERR_LOG("Error: malloc() failed in cf_load_db() for entries\n");
        fclose(fp);
        cf_db_free(db);
        exit(CF_CLIB_FAIL_EC);
    }

    if (fread(entries, sizeof(cf_db_entry_t), hdr->entry_cnt, fp) != hdr->entry_cnt) {  
        CF_ERR_LOG("Error: Could not read database entries\n");
        fclose(fp);
        cf_db_free(db);
        exit(CF_DB_IO_FAIL);
    }

    cf_db_lstring_t* strings = (cf_db_lstring_t*) malloc(hdr->string_sz);
    if (strings == NULL) {
        CF_ERR_LOG("Error: malloc() failed in cf_load_db() for strings\n");
        fclose(fp);
        cf_db_free(db);
        exit(CF_CLIB_FAIL_EC);
    }

    if (fread(strings, 1, hdr->string_sz, fp) != hdr->string_sz) {  
        CF_ERR_LOG("Error: Could not read database entries\n");
        fclose(fp);
        cf_db_free(db);
        exit(CF_DB_IO_FAIL);
    }
    
    db->entries = entries;
    db->strings = strings;
    fclose(fp);
    return db;
}

static void cf_db_save(const char* db_path, cf_db_mem_t* db) {
    if (db == NULL) {
        CF_ERR_LOG("Error: db passed to cf_save_db() is NULL");
        return;
    }

    FILE* fp = fopen(db_path, "wb");
    if (fp == NULL) {
        CF_ERR_LOG("Error: Could not open database file\n");
        cf_db_free(db);
        exit(CF_DB_IO_FAIL);
    }

    cf_db_hdr_t* hdr = db->header;
    size_t entry_cnt = hdr->entry_cnt;
    size_t string_sz = hdr->string_sz;
    hdr->entry_cnt += db->pentries_idx;
    hdr->string_sz += db->pstrings_off;
    if(fwrite(hdr, sizeof(cf_db_hdr_t), 1, fp) != 1) {
        CF_ERR_LOG("Error: Could not write database header\n");
        fclose(fp);
        cf_db_free(db);
        exit(CF_DB_IO_FAIL);
    }

    if (db->entries != NULL) {
        if (fwrite(db->entries, sizeof(cf_db_entry_t), entry_cnt, fp) != entry_cnt) {
            CF_ERR_LOG("Error: Could not write database entries\n");
            fclose(fp);
            cf_db_free(db);
            exit(CF_DB_IO_FAIL);
        }
    }
    
    if (db->pentries_idx > 0) {
        if (fwrite(db->pending_entries, sizeof(cf_db_entry_t), db->pentries_idx, fp) != db->pentries_idx) {
            CF_ERR_LOG("Error: Could not write new database entries\n");
            fclose(fp);
            cf_db_free(db);
            exit(CF_DB_IO_FAIL);
        }
    }

    if (db->strings != NULL) {
        if (fwrite(db->strings, 1, string_sz, fp) != string_sz) {
            CF_ERR_LOG("Error: Could not write database strings\n");
            fclose(fp);
            cf_db_free(db);
            exit(CF_DB_IO_FAIL);
        }
    }

    if (db->pstrings_off > 0) {
        if (fwrite(db->pending_strings, 1, db->pstrings_off, fp) != db->pstrings_off) {
            CF_ERR_LOG("Error: Could not write new database strings\n");
            fclose(fp);
            cf_db_free(db);
            exit(CF_DB_IO_FAIL);
        }
    }

    fclose(fp);
    cf_db_free(db);
}

static cf_db_entry_t* cf_db_find(char* path, cf_db_mem_t* db) {
    size_t plen = strlen(path);
    uint64_t hash = xxh64((uint8_t*) path, plen, 0);
    for (size_t i = 0; i < db->pentries_idx; i++) {
        cf_db_entry_t* entry = &db->pending_entries[i];
        if (hash == entry->path_hash) {
            uint8_t* slab = (uint8_t*) db->pending_strings;
            uint16_t* len_slot = (uint16_t*) (slab + entry->path_offset);
            uint16_t strl = *len_slot;
            char* strptr = (char*) (len_slot + 1);
            if (strncmp(path, strptr, strl) == 0) {
                return entry;
            } else {
                CF_WRN_LOG("Warning: Path hash collision detected!\n");
            }
        }
    }

    if (db->entries == NULL || db->strings == NULL) {
        return NULL;
    }

    for (size_t i = 0; i < db->header->entry_cnt; i++) {
        cf_db_entry_t* entry = &db->entries[i];
        if (hash == entry->path_hash) {
            uint8_t* slab = (uint8_t*) db->strings;
            uint16_t* len_slot = (uint16_t*) (slab + entry->path_offset);
            uint16_t strl = *len_slot;
            char* strptr = (char*) (len_slot + 1);
            if (strncmp(path, strptr, strl) == 0) {
                return &db->entries[i];
            } else {
                CF_WRN_LOG("Warning: Path hash collision detected!\n");
            }
        }
    }

    return NULL;
}

static bool cf_db_hash_file(char* path, uint64_t* hash) {
    FILE* fp = fopen(path, "rb");
    if (fp == NULL) {
        return false;
    }

    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return false;
    }

    ssize_t sz = ftell(fp);
    if (sz < 0) {
        fclose(fp);
        return false;
    }

    rewind(fp);
    uint8_t* buf = (uint8_t*) malloc((size_t) sz + 1);
    if (buf == NULL) {
        CF_ERR_LOG("Error: malloc() failed in cf_db_hash_file()\n"); \
        exit(CF_CLIB_FAIL_EC); \
    }

    if (fread(buf, 1, (size_t) sz, fp) != (size_t) sz) {
        fclose(fp);
        free(buf);
        return false;
    }

    fclose(fp);
    buf[sz] = '\0';
    *hash = xxh64(buf, (size_t) sz, 0);
    free(buf);
    return true;

}

static void __attribute__((unused)) cf_db_mark_utd(char* path, cf_db_mem_t* db) {
    cf_db_entry_t* entry = cf_db_find(path, global_db);
    bool is_new = (entry == NULL);
    size_t saved_pstrings_off = db->pstrings_off;
    size_t saved_pentries_idx = db->pentries_idx;

    if (is_new) {
        size_t str_offset = db->pstrings_off;
        size_t strl = strlen(path);
        if (strl > UINT16_MAX) {
            CF_ERR_LOG("Error: Path length exceeds UINT16 length\n");
            exit(CF_DB_OOM_EC);
        }
        size_t needed = sizeof(uint16_t) + strl + 1;
        if (str_offset + needed > db->pstrings_sz) {
            CF_ERR_LOG("Error: pending strings buffer ran out of space!");
            exit(CF_DB_OOM_EC);
        }

        uint8_t* slab = (uint8_t*) db->pending_strings;
        uint16_t* len_slot = (uint16_t*) (slab + db->pstrings_off);
        *len_slot = (uint16_t) strl;
        memcpy(len_slot + 1, path, strl + 1);

        size_t idx = db->pentries_idx;
        if (idx >= db->pentries_max) {
            CF_ERR_LOG("Error: pending entries buffer ran out of space!");
            exit(CF_DB_OOM_EC);
        }

        entry = &db->pending_entries[idx];
        entry->path_offset = str_offset;
        entry->path_hash = xxh64((uint8_t*) path, strl, 0);
        db->pentries_idx++;
        db->pstrings_off += needed;
    }

    struct stat st;
    if (stat(path, &st) == -1) {
        db->pstrings_off = saved_pstrings_off;
        db->pentries_idx = saved_pentries_idx;
        return;
    }

    uint64_t hash = 0;
    if (!cf_db_hash_file(path, &hash)) {
        db->pstrings_off = saved_pstrings_off;
        db->pentries_idx = saved_pentries_idx;
        return;
    }

    entry->mtime = (uint64_t) st.st_mtim.tv_nsec;
    entry->size = (uint64_t) st.st_size;
    entry->env_hash = cenv_hash;
    entry->content_hash = hash;
}

static bool __attribute__((unused)) cf_file_utd(char* path) {
    cf_db_entry_t* entry = cf_db_find(path, global_db);
    if (entry == NULL) {
        return false;
    }

    struct stat st;
    if (stat(path, &st) == -1) {
        return false;
    }

    if (entry->size != (uint64_t) st.st_size) {
        return false;
    }

    if (entry->mtime != (uint64_t) st.st_mtim.tv_nsec) {
        return false;
    }

    if (cenv_hash != entry->env_hash) {
        return false;
    }

    /* TODO: Optimize the above so that this never has to run */
    uint64_t hash = 0;
    if (cf_db_hash_file(path, &hash) == false) {
        return false;
    }

    if (hash != entry->content_hash) {
        return false;
    }

    return true;
}

static inline uint64_t cf_hash_env(char** env) {
    uint64_t hash = 0;
    for (char** entry = env; *entry != NULL; entry++) {
        size_t len = strlen(*entry);
        hash ^= xxh64((uint8_t*) *entry, len, 0);
    }

    return hash;
}

/* Compact XXH64 implementation */
static const uint64_t XXH64_P1 = 0x9E3779B185EBCA87;
static const uint64_t XXH64_P2 = 0xC2B2AE3D27D4EB4F;
static const uint64_t XXH64_P3 = 0x165667B19E3779F9;
static const uint64_t XXH64_P4 = 0x85EBCA77C2B2AE63;
static const uint64_t XXH64_P5 = 0x27D4EB2F165667C5;

static inline uint64_t xxh64_rotl(uint64_t x, int r) {
    return (x << r) | (x >> (64 - r));
}

static inline uint64_t xxh64_read64(const void *p) {
    uint64_t v;
    memcpy(&v, p, 8);
    return v;
}

static inline uint64_t xxh64_read32(const void *p) {
    uint32_t v;
    memcpy(&v, p, 4);
    return v;
}

static inline uint64_t xxh64_round(uint64_t acc, uint64_t input) {
    return xxh64_rotl(acc + input * XXH64_P2, 31) * XXH64_P1;
}

uint64_t xxh64(uint8_t* data, size_t len, uint64_t seed) {
    uint8_t* p = data;
    uint8_t *end = p + len;
    uint64_t h;

    if (len >= 32) {
        uint64_t v[] = {seed + XXH64_P1 + XXH64_P2, seed + XXH64_P2, seed, seed - XXH64_P1};
        while (p <= end - 32) {
            for (int i = 0; i < 4; i++) {
                v[i] = xxh64_round(v[i], xxh64_read64(p + i * 8));
            }

            p += 32;
        }

        h = xxh64_rotl(v[0],1) + xxh64_rotl(v[1],7) + xxh64_rotl(v[2],12) + xxh64_rotl(v[3],18);
        for (int i = 0; i < 4; i++) {
            h = (h ^ xxh64_round(0, v[i])) * XXH64_P1 + XXH64_P4;
        }
    } else {
        h = seed + XXH64_P5;
    }

    h += (uint64_t)len;

    while (p + 8 <= end) {
        h ^= xxh64_round(0, xxh64_read64(p));
        h = xxh64_rotl(h, 27) * XXH64_P1 + XXH64_P4;
        p += 8;
    }
    while (p + 4 <= end) {
        h ^= xxh64_read32(p) * XXH64_P1;
        h = xxh64_rotl(h, 23) * XXH64_P2 + XXH64_P3;
        p += 4;
    }
    while (p < end) {
        h ^= *p * XXH64_P5;
        h = xxh64_rotl(h, 11) * XXH64_P1;
        p++;
    }

    h ^= h >> 33;
    h *= XXH64_P2;
    h ^= h >> 29;
    h *= XXH64_P3;
    h ^= h >> 32;
    return h;
}


#endif // CF_ENABLE_UTDDB

static void cf_register_target(const char* name, cf_target_fn fn, const cf_attr_t* attribs, size_t attribs_size) {
    if (cf_state != REGISTER_PHASE) {
        CF_ERR_LOG("Error: Invalid cf_state (%d) when registering target!\n", cf_state);
        exit(CF_INVALID_STATE_EC);
    }

    if (strlen(name) > CF_MAX_NAME_LENGTH) {
        CF_ERR_LOG("Error: The name \"%s\" when registering target is too long (max name length: %d)\n", name, CF_MAX_NAME_LENGTH);
        exit(CF_NAME_TOO_LONG_EC);
    }

    if (cf_num_targets >= CF_MAX_TARGETS) {
        CF_ERR_LOG("Error: Maximum targets of %d was reached!\n", CF_MAX_TARGETS);
        exit(CF_MAX_TARGETS_EC);
    }

    void* attribs_block;
    if (attribs_size > 0) {
        attribs_block = malloc(attribs_size * sizeof(cf_attr_t));
        if (attribs_block == NULL) {
            CF_ERR_LOG("Error: malloc() failed in cf_register_target()\n");
            exit(CF_CLIB_FAIL_EC);
        }

        memcpy(attribs_block, attribs, attribs_size * sizeof(cf_attr_t));
    } else {
        attribs_block = NULL;
    }

    cf_targets[cf_num_targets++] = (cf_target_decl_t) {
        .name = name,
        .fn = fn,
        .attribs = (cf_attr_t*) attribs_block,
        .attribs_size = attribs_size,
        .node_status = UNVISITED
    };
}

static void cf_dfs_execute(cf_target_decl_t* target) {
    if (target->node_status == DONE) {
        return;
    } else if (target->node_status == VISITING) {
        CF_ERR_LOG("Error: Dependency cycle detected for \"%s\"\n", target->name);
        exit(CF_TARGET_DEP_CYCLE_EC);
    }

    target->node_status = VISITING;
#ifdef CF_ENABLE_CONFIG
    cf_config_decl_t* config = NULL;
#endif // CF_ENABLE_CONFIG
    for (size_t i = 0; i < target->attribs_size; i++) {
        cf_attr_t* attrib = &target->attribs[i];
        switch (attrib->type) {
            case DEPENDENCY: {
                const char* dep_target_name = attrib->arg.depends.target_name;
                size_t dep_idx = cf_find_target_index(dep_target_name);
                if (dep_idx >= cf_num_targets) {
                    CF_ERR_LOG("Error: Target \"%s\" not found!\n", dep_target_name);
                    exit(CF_TARGET_NOT_FOUND_EC);
                }
                
                cf_dfs_execute(&cf_targets[dep_idx]);
                break;
            }
#ifdef CF_ENABLE_CONFIG
            case CONFIG_SET: {
                if (config != NULL) {
                    CF_WRN_LOG("Warning: Cannot set two or more configs per target. Ignoring...\n");
                    goto next_attr;
                }
                
                const char* conf_name = attrib->arg.configset.config_name;
                for (size_t c_idx = 0; c_idx < cf_num_configs; c_idx++) {
                    if (strncmp(conf_name, cf_configs[c_idx].name, CF_MAX_NAME_LENGTH) == 0) {
                        config = &cf_configs[c_idx];
                        goto next_attr;
                    }
                }
                
                CF_ERR_LOG("Error: Config \"%s\" not found!\n", conf_name);
                exit(CF_CONFIG_NOT_FOUND_EC);
                break;
            }
#endif // CF_ENABLE_CONFIG
            case UNKNOWN: {
                CF_ERR_LOG("Error: Unknown attribute given for target \"%s\"\n", target->name);
                exit(CF_UNKNOWN_ATTR_EC);
            }
        }

#ifdef CF_ENABLE_CONFIG
next_attr:
        continue;
#endif // CF_ENABLE_CONFIG
    }

#ifdef CF_ENABLE_CONFIG
    size_t env_checkpoint = cf_num_envs;
    if (config != NULL) {
        config->fn();
    }
#endif // CF_ENABLE_CONFIG

#if defined(CF_ENABLE_UTDDB) && defined(CF_ENABLE_CONFIG)
    if (config != NULL) {
        cenv_hash = cf_hash_env(environ);
    } else {
        /* Commands in system() can't change parent environment! */
        cenv_hash = denv_hash;
    }
#elif defined(CF_ENABLE_UTDDB)
    cenv_hash = denv_hash;
#endif // CF_ENABLE_UTDDB

#ifdef CF_ENABLE_UTILS
    size_t glob_checkpoint = cf_num_globs;
    size_t jstrings_checkpoint = cf_num_jstrings;
    size_t maps_checkpoint = cf_num_maps;
#endif // CF_ENABLE_UTILS

    target->fn();

#ifdef CF_ENABLE_PARALLEL
    mtx_t* lock = &global_workq->lock;
    mtx_lock(lock);
    while (global_workq->active_jobs > 0 || !cf_empty_job()) {
        cnd_wait(&global_workq->no_job, lock);
    }
    mtx_unlock(lock);
#endif // CF_ENABLE_PARALLEL

#ifdef CF_ENABLE_UTILS
    cf_free_maps(maps_checkpoint);
    cf_free_jstrings(jstrings_checkpoint);
    cf_free_glob(glob_checkpoint);
#endif // CF_ENABLE_UTILS
#ifdef CF_ENABLE_CONFIG
    cf_restore_env(env_checkpoint);
#endif // CF_ENABLE_CONFIG
    target->node_status = DONE;
}

static void __attribute__((unused)) cf_execute_command(bool is_parallel, char* buffer) {
#ifdef CF_ENABLE_PARALLEL
    if (is_parallel) {
        mtx_t* lock = &global_workq->lock;
        mtx_lock(lock);

        while (cf_full_job()) {
            while (cf_num_thrds < CF_MAX_THRDS) {    
                thrd_t worker_thread = 0;
                if (thrd_create(&worker_thread, &cf_thrd_helper, (void*) global_workq) != thrd_success) {
                    CF_ERR_LOG("Error: Thread failed during creation in cf_execute_command()\n");
                    exit(CF_CLIB_FAIL_EC);
                }

                cf_thrd_pool[cf_num_thrds++] = worker_thread;
            }

            cnd_wait(&global_workq->free_slot, lock);
        }

        cf_enqueue_job((cf_thrd_job) {
            .command = buffer,
        });
        ++global_workq->active_jobs;
        cnd_signal(&global_workq->new_job);

        if (global_workq->active_jobs > cf_num_thrds && cf_num_thrds < CF_MAX_THRDS) {
            thrd_t worker_thread;
            if (thrd_create(&worker_thread, &cf_thrd_helper, (void*) global_workq) != thrd_success) {
                CF_ERR_LOG("Error: Thread failed during creation in cf_execute_command()\n");
                exit(CF_CLIB_FAIL_EC);
            }

            cf_thrd_pool[cf_num_thrds++] = worker_thread;
        }

        mtx_unlock(lock);
        return;
    }
#endif // CF_ENABLE_PARALLEL

    if (system(buffer) != 0) {
        CF_ERR_LOG("Error: Executing command \"%s\" failed", (char*) buffer);
        exit(CF_CLIB_FAIL_EC);
    }

    free(buffer);
}


__attribute__((weak)) int main(int argc, char** argv) {
    if (argc == 1) {
        goto cleanup;
    }

#ifdef CF_ENABLE_UTDDB
    global_db = cf_db_load(".cforge.db");
    denv_hash = cf_hash_env(environ);
#endif // CF_ENABLE_UTDDB

#ifdef CF_ENABLE_PARALLEL
    global_workq = (cf_work_queue*) malloc(sizeof(cf_work_queue));
    if (global_workq == NULL) {
        CF_ERR_LOG("Error: malloc() failed in main()\n");
        exit(CF_CLIB_FAIL_EC);
    }
    global_workq->front = 0;
    global_workq->back = 0;
    global_workq->active_jobs = 0;
    mtx_init(&global_workq->lock, mtx_plain);
    cnd_init(&global_workq->free_slot);
    cnd_init(&global_workq->new_job);
    cnd_init(&global_workq->no_job);
#endif // CF_ENABLE_PARALLEL

    cf_state = TARGET_EXECUTE_PHASE;
    for (int32_t i = 1; i < argc; i++) {
        for (size_t j = 0; j < cf_num_targets; j++) {
            cf_target_decl_t* target = &cf_targets[j];
            if (strcmp(target->name, argv[i]) == 0) {
                if (target->node_status == DONE) {
                    CF_WRN_LOG("Warning: Target \"%s\" was executed already! Skipping target...\n", argv[i]);
                    goto next_iter;
                }
                cf_dfs_execute(target);
                goto next_iter;
            }
        }

        CF_ERR_LOG("Error: Target \"%s\" not found!\n", argv[i]);
        return CF_TARGET_NOT_FOUND_EC;

        next_iter:
            continue;
    }

#ifdef CF_ENABLE_UTDDB
    cf_db_save(".cforge.db", global_db);
#endif // CF_ENABLE_UTDDB

#ifdef CF_ENABLE_PARALLEL
    mtx_lock(&global_workq->lock);
    while (cf_full_job()) {
        cnd_wait(&global_workq->no_job, &global_workq->lock);
    }
    for (size_t t = 0; t < cf_num_thrds; t++) {

        cf_enqueue_job((cf_thrd_job) {
            .command = NULL
        });
        cnd_signal(&global_workq->new_job);
    }
    mtx_unlock(&global_workq->lock);

    for (size_t t = cf_num_thrds; t > 0; t--) {
        thrd_join(cf_thrd_pool[t - 1], NULL);
        cf_thrd_pool[t - 1] = (thrd_t) { 0 };
    }

    mtx_destroy(&global_workq->lock);
    cnd_destroy(&global_workq->free_slot);
    cnd_destroy(&global_workq->new_job);
    cnd_destroy(&global_workq->no_job);
    free(global_workq);
#endif // CF_ENABLE_PARALLEL

cleanup:
    for (size_t t_idx = 0; t_idx < cf_num_targets; t_idx++) {
        free(cf_targets[t_idx].attribs);
    }

    return CF_SUCCESS_EC;
}


#define CF_TARGET(name_ident, ...) \
    static void cf_target_##name_ident(void); \
    __attribute__((constructor)) static void cf_target_reg_##name_ident(void) { \
        const cf_attr_t attribs[] = { __VA_ARGS__ }; \
        cf_register_target(#name_ident, cf_target_##name_ident, attribs, sizeof(attribs)/sizeof(cf_attr_t)); \
    } \
    static void cf_target_##name_ident(void)

#define CF_DEPENDS(target_ident) \
    (cf_attr_t) { \
        .type = DEPENDENCY, \
        .arg.depends = { \
            .target_name = #target_ident \
        } \
    }

#ifdef CF_ENABLE_CONFIG
#define CF_CONFIG(name_ident) \
    static void cf_config_##name_ident(void); \
    __attribute__((constructor)) static void cf_config_reg_##name_ident(void) { \
        cf_register_config(#name_ident, cf_config_##name_ident); \
    } \
    static void cf_config_##name_ident(void)

#define CF_WITH_CONFIG(config_ident) \
    (cf_attr_t) { \
        .type = CONFIG_SET, \
        .arg.configset = { \
            .config_name = #config_ident \
        } \
    }

#define CF_SET_ENV(ident, value) cf_setenv_wrapper(#ident, value);
#define CF_ENV(ident) getenv(#ident)
#endif // CF_ENABLE_CONFIG

#ifdef CF_ENABLE_UTILS
#define CF_GLOB(expr) \
    cf_glob(expr);


/* Hack to make the `for` syntax possible for `CF_GLOBS_EACH()` */
typedef struct {
    cf_glob_t glob;
    size_t    checkpoint;
} cf_glob_iter_hack_t;

static inline cf_glob_iter_hack_t cf_glob_begin_hack(const char *expr) {
    /* Inlined so this is fine... */
    size_t local_num_globs = cf_num_globs;
    return (cf_glob_iter_hack_t){
        .glob = cf_glob(expr),
        .checkpoint = local_num_globs,
    };
}

#define CF_GLOBS_EACH(expr, filename) \
    (cf_glob_iter_hack_t cf_cgh_##filename = cf_glob_begin_hack(expr); \
    cf_cgh_##filename.glob.p != NULL; \
    cf_free_glob(cf_cgh_##filename.checkpoint), (void)(cf_cgh_##filename.glob.p = NULL)) \
    for (char **cf_ci_##filename = cf_cgh_##filename.glob.p, \
        *filename = *cf_ci_##filename; \
        cf_ci_##filename < cf_cgh_##filename.glob.p + cf_cgh_##filename.glob.c; \
        filename = *++cf_ci_##filename)

#define CF_MAPA(sources, len, ...) \
    cf_map(sources, len, (cf_map_attr_t[]) { __VA_ARGS__ }, (sizeof((cf_map_attr_t[]) { __VA_ARGS__ })/sizeof(cf_map_attr_t)))

#define CF_MAP(source, ...) \
    CF_MAPA((const char*[]) { source }, 1, __VA_ARGS__)[0]

#define CF_MAP_EXT(new_ext) \
    (cf_map_attr_t) { \
        .type = MAP_EXT, \
        .n_ext = new_ext \
    }

#define CF_MAP_PARENT(new_parent) \
    (cf_map_attr_t) { \
        .type = MAP_PARENT, \
        .n_parent = new_parent \
    }

#define CF_NOP \
    do {} while (0);

#define CF_BOLD "\x1b[1m"
#define CF_UNDERLINE "\x1b[4m"
#define CF_INVERSE "\x1b[7m"
#define CF_BLACK "\x1b[30m"
#define CF_RED "\x1b[31m"
#define CF_GREEN "\x1b[32m"
#define CF_YELLOW "\x1b[33m"
#define CF_BLUE "\x1b[34m"
#define CF_MAGENTA "\x1b[35m"
#define CF_CYAN "\x1b[36m"
#define CF_WHITE "\x1b[37m"
#define CF_BG_BLACK "\x1b[40m"
#define CF_BG_RED "\x1b[41m"
#define CF_BG_GREEN "\x1b[42m"
#define CF_BG_YELLOW "\x1b[43m"
#define CF_BG_BLUE "\x1b[44m"
#define CF_BG_MAGENTA "\x1b[45m"
#define CF_BG_CYAN "\x1b[46m"
#define CF_BG_WHITE "\x1b[37m"
#define CF_RESET "\x1b[0m"

#endif // CF_ENABLE_UTILS

#define CF_INTERNAL_RUNNER(parallel, format_str, ...) \
    do { \
        char* buffer = malloc(CF_MAX_COMMAND_LENGTH); \
        if (buffer == NULL) { \
            CF_ERR_LOG("Error: malloc() failed in CF_INTERNAL_RUNNER\n"); \
            exit(CF_CLIB_FAIL_EC); \
        } \
        int n = snprintf(buffer, CF_MAX_COMMAND_LENGTH, format_str, ##__VA_ARGS__); \
        if (n < 0) { \
            CF_ERR_LOG("Error: snprintf() failed in CF_INTERNAL_RUNNER\n"); \
            exit(CF_CLIB_FAIL_EC); \
        }else if (n >= CF_MAX_COMMAND_LENGTH) { \
            CF_ERR_LOG("Error: Maximum command length of %d was reached!\n", CF_MAX_COMMAND_LENGTH); \
            exit(CF_MAX_COMMAND_LENGTH_EC); \
        } \
        cf_execute_command(parallel, buffer); \
    } while (0);

#define CF_RUN(format_str, ...) CF_INTERNAL_RUNNER(false, format_str, ##__VA_ARGS__)

#ifdef CF_ENABLE_PARALLEL
#define CF_RUNP(format_str, ...) CF_INTERNAL_RUNNER(true, format_str, ##__VA_ARGS__)
#endif // CF_ENABLE_PARALLEL

#ifdef CF_ENABLE_UTDDB

#define CF_FILE_UTD(filepath) \
    (cf_file_utd((char*) filepath))

#define CF_FILE_NOT_UTD(filepath) \
    (!cf_file_utd((char*) filepath))

#define CF_FILE_MARK_UTD(filepath) \
    cf_db_mark_utd(filepath, global_db);

#endif // CF_ENABLE_UTDDB


#endif // CFORGE_H

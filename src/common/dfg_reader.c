#include <strings.h>
#include <stdbool.h>
#include <string.h>
#include "dfg.h"
#include "dfg_reader.h"
#include "jsmn_parser.h"
#include "jsmn.h"
#include "logging.h"

enum object_type {
    ROOT=0, RUNTIMES=1, ROUTES=2, DESTINATIONS=3, MSUS=4, PROFILING=5,
    META_ROUTING=6, SOURCE_TYPES=7, SCHEDULING=8, DEPENDENCIES=9, MSU_TYPES = 10
};

static int fix_num_threads(struct dedos_dfg *dfg) {
    for (int i=0; i<dfg->n_runtimes; i++) {
        struct dfg_runtime *rt = dfg->runtimes[i];

        int n_pinned = rt->n_pinned_threads;
        int n_unpinned = rt->n_unpinned_threads;
        int total = n_pinned + n_unpinned;
        for (int t=0; t < total; t++) {
            struct dfg_thread *thread = rt->threads[t];
            if (thread->mode == PINNED_THREAD) {
                n_pinned--;
            } else if (thread->mode == UNPINNED_THREAD) {
                n_unpinned--;
            } else {
                if (n_pinned > 0) {
                    thread->mode = PINNED_THREAD;
                    log(LOG_DFG_READER, "Setting thread %d with no MSUs to pinned", t);
                    n_pinned--;
                } else if (n_unpinned > 0) {
                    thread->mode = UNPINNED_THREAD;
                    log(LOG_DFG_READER, "Setting thread %d with no MSUs to unpinned", t);
                    n_unpinned--;
                } else {
                    log_error("Cannot determine pinned/unpinned status of threads "
                              "based on number of blocking/nonblock MSUs");
                    return -1;
                }
            }
        }
        if (n_pinned < 0 || n_unpinned < 0) {
            log_error("More pinned/unpinned threads specified by MSUs than by DFG: "
                      "%d more pinned, %d more unpinned", -1 * n_pinned, -1 * n_unpinned);
            return -1;
        }
    }
    return 0;
}

static struct key_mapping key_map[];

struct dedos_dfg *parse_dfg_json_file(const char *filename){

    struct dedos_dfg *dfg = calloc(1, sizeof(*dfg));

    set_dfg(dfg);

    int rtn = parse_file_into_obj(filename, dfg, key_map);

    if (rtn >= 0){
        rtn = fix_num_threads(dfg);
        if (rtn < 0) {
            return NULL;
        }
        return dfg;
    } else {
        log_error("Failed to parse jsmn");
        return NULL;
    }
}

PARSE_FN(set_app_name) {
    struct dedos_dfg *dfg = GET_PARSE_OBJ();
    char *name = GET_STR_TOK();
    if (strlen(name) > MAX_APP_NAME_LENGTH) {
        log_error("Application name '%s' is too long", name);
        return -1;
    }
    strcpy(dfg->application_name, name);
    return 0;
}

PARSE_FN(set_ctl_ip) {
    struct dedos_dfg *dfg = GET_PARSE_OBJ();
    char *ip = GET_STR_TOK();
    struct in_addr addr;
    int rtn = inet_pton(AF_INET, ip, &addr);
    if (rtn <= 0) {
        log_perror("Error converting '%s' to IP address", ip);
        return -1;
    }
    dfg->global_ctl_ip = addr.s_addr;
    return 0;
}

PARSE_FN(set_ctl_port) {
    struct dedos_dfg *dfg = GET_PARSE_OBJ();
    dfg->global_ctl_port = GET_INT_TOK();
    return 0;
}

INIT_OBJ_FN(init_runtime) {
    struct dedos_dfg *dfg = GET_PARSE_OBJ();
    int index = GET_OBJ_INDEX();

    dfg->n_runtimes++;
    dfg->runtimes[index] = calloc(1, sizeof(*dfg->runtimes[index]));

    RETURN_OBJ(dfg->runtimes[index], RUNTIMES);
}

PARSE_OBJ_LIST_FN(set_runtimes, init_runtime);

INIT_OBJ_FN(init_dfg_msu_from_json) {
    struct dedos_dfg *dfg = GET_PARSE_OBJ();
    int index = GET_OBJ_INDEX();

    dfg->n_msus++;
    dfg->msus[index] = calloc(1, sizeof(struct dfg_msu));

    RETURN_OBJ(dfg->msus[index], MSUS);
}

PARSE_OBJ_LIST_FN(set_msus, init_dfg_msu_from_json);

INIT_OBJ_FN(init_dfg_msu_type) {
    struct dedos_dfg *dfg = GET_PARSE_OBJ();

    int index = GET_OBJ_INDEX();

    dfg->n_msu_types++;
    dfg->msu_types[index] = calloc(1, sizeof(**dfg->msu_types));

    RETURN_OBJ(dfg->msu_types[index], MSU_TYPES);
}

PARSE_OBJ_LIST_FN(set_msu_types, init_dfg_msu_type);

PARSE_FN(set_msu_type_id) {
    struct dfg_msu_type *type = GET_PARSE_OBJ();
    type->id = GET_INT_TOK();
    return 0;
}

PARSE_FN(set_msu_type_name) {
    struct dfg_msu_type *type = GET_PARSE_OBJ();
    char *name = GET_STR_TOK();

    if (strlen(name) > MAX_MSU_NAME_LEN) {
        log_error("MSU name %s too long", name);
        return -1;
    }
    strcpy(type->name, name);
    return 0;
}

PARSE_OBJ_FN(set_meta_routing, struct dfg_msu_type, meta_routing, META_ROUTING);

INIT_OBJ_FN(init_dependencies) {
    struct dfg_msu_type *type = GET_PARSE_OBJ();
    int index = GET_OBJ_INDEX();

    type->n_dependencies++;
    type->dependencies[index] = calloc(1, sizeof(*type->dependencies[index]));

    RETURN_OBJ(type->dependencies[index], DEPENDENCIES);
}

PARSE_OBJ_LIST_FN(set_dependencies, init_dependencies);

PARSE_FN(set_cloneable) {
    struct dfg_msu_type *type = GET_PARSE_OBJ();
    type->cloneable = GET_INT_TOK();
    return 0;
}

PARSE_FN(set_colocation_group) {
    struct dfg_msu_type *type = GET_PARSE_OBJ();
    type->colocation_group = GET_INT_TOK();
    return 0;
}

PARSE_FN(set_msu_init_data) {
    struct dfg_msu *vertex = GET_PARSE_OBJ();

    char *init_data = GET_STR_TOK();
    if (strlen(init_data) > MAX_INIT_DATA_LEN) {
        log_error("Init data '%s' too long", init_data);
        return -1;
    }
    strcpy(vertex->init_data.init_data, init_data);
    return 0;
}

PARSE_FN(set_msu_id) {
    struct dfg_msu *msu = GET_PARSE_OBJ();
    msu->id = GET_INT_TOK();
    return 0;
}

PARSE_FN(set_msu_vertex_type) {
    struct dfg_msu *msu = GET_PARSE_OBJ();
    char *str_type = GET_STR_TOK();
    msu->vertex_type = str_to_vertex_type(str_type);
    return 0;
}


PARSE_FN(set_msu_type) {
    struct dfg_msu *vertex = GET_PARSE_OBJ();

    struct dfg_msu_type *type = get_dfg_msu_type(GET_INT_TOK());
    if (type == NULL) {
        // Return once type has been instantiated
        return 1;
    }
    vertex->type = type;

    type->n_instances++;
    type->instances[type->n_instances] = vertex;
    return 0;
}


PARSE_FN(set_blocking_mode) {
    struct dfg_msu *vertex = GET_PARSE_OBJ();

    char *str_mode = GET_STR_TOK();

    enum blocking_mode mode = str_to_blocking_mode(str_mode);
    if (mode == UNKNOWN_BLOCKING_MODE) {
        log_error("Unknown blocking mode specified: %s", str_mode);
        return -1;
    }

    struct dfg_runtime *rt = vertex->scheduling.runtime;
    if (rt == NULL) {
        return 1;
    }

    struct dfg_thread *thread = vertex->scheduling.thread;
    if (thread == NULL) {
        log(LOG_DFG_READER, "Waiting for thread instantiation");
        return 1;
    }

    if (thread->mode == UNSPECIFIED_THREAD_MODE) {
        thread->mode = (mode == NONBLOCK_MSU) ? PINNED_THREAD : UNPINNED_THREAD;
    }

    if ((thread->mode == PINNED_THREAD) ^ (mode == NONBLOCK_MSU)) {
        log_error("Can only put blocking MSUs on pinned threads, and nonblock MSUs on unpinned");
        return -1;
    }
    return 0;
}

PARSE_OBJ_FN(set_scheduling, struct dfg_msu, scheduling, SCHEDULING);

PARSE_FN(set_rt_id) {
    struct dfg_runtime *rt = GET_PARSE_OBJ();
    rt->id = GET_INT_TOK();
    return 0;
}

PARSE_FN(set_rt_ip) {
    struct dfg_runtime *rt = GET_PARSE_OBJ();
    char *ip = GET_STR_TOK();
    struct in_addr addr;
    int rtn = inet_pton(AF_INET, ip, &addr);
    if (rtn <= 0) {
        log_perror("Error converting '%s' to IP address", ip);
        return -1;
    }
    rt->ip = addr.s_addr;
    return 0;
}

PARSE_FN(set_rt_port) {
    struct dfg_runtime *rt = GET_PARSE_OBJ();
    rt->port = GET_INT_TOK();
    return 0;
}

PARSE_FN(set_rt_n_cores) {
    struct dfg_runtime *rt = GET_PARSE_OBJ();
    rt->n_cores = GET_INT_TOK();
    return 0;
}

PARSE_FN(set_num_pinned_threads) {
    struct dfg_runtime *rt = GET_PARSE_OBJ();

    int n_existing = rt->n_unpinned_threads;
    int n_new = GET_INT_TOK();

    for (int i=n_existing; i<n_existing+n_new; i++) {
        rt->threads[i] = calloc(1, sizeof(**rt->threads));
        rt->threads[i]->id = i+1;
    }
    rt->n_pinned_threads = n_new;
    return 0;
}

PARSE_FN(set_num_unpinned_threads) {
    struct dfg_runtime *rt = GET_PARSE_OBJ();

    int n_existing = rt->n_pinned_threads;
    int n_new = GET_INT_TOK();

    for (int i=n_existing; i < n_existing + n_new; i++) {
        rt->threads[i] = calloc(1, sizeof(**rt->threads));
        rt->threads[i]->id = i+1;
    }
    rt->n_unpinned_threads = n_new;
    return 0;
}

INIT_OBJ_FN(init_route) {
    struct dfg_runtime *rt = GET_PARSE_OBJ();
    int index = GET_OBJ_INDEX();
    rt->n_routes++;
    rt->routes[index] = calloc(1, sizeof(**rt->routes));

    RETURN_OBJ(rt->routes[index], ROUTES);
}

PARSE_OBJ_LIST_FN(set_rt_routes, init_route);

PARSE_FN(set_route_id) {
    struct dfg_route *route = GET_PARSE_OBJ();
    int id = GET_INT_TOK();
    struct dfg_route *existing = get_dfg_route(id);
    if (existing != NULL) {
        log_error("Route with ID %d exists twice in DFG", id);
        return -1;
    }
    route->id = id;
    log(LOG_DFG_PARSING, "Created route with id: %d", id);
    return 0;
}

PARSE_FN(set_route_type) {
    struct dfg_route *route = GET_PARSE_OBJ();
    int type_id = GET_INT_TOK();
    struct dfg_msu_type *type = get_dfg_msu_type(type_id);
    if (type == NULL) {
        // Return once type is instantiated
        return -1;
    }
    route->msu_type = type;
    return 0;
}

INIT_OBJ_FN(init_endpoint) {
    struct dfg_route *route = GET_PARSE_OBJ();

    int index = GET_OBJ_INDEX();
    route->n_endpoints++;
    route->endpoints[index] = calloc(1, sizeof(**route->endpoints));

    RETURN_OBJ(route->endpoints[index], DESTINATIONS);
}

PARSE_OBJ_LIST_FN(set_route_endpoints, init_endpoint);

PARSE_FN(set_dest_key) {
    struct dfg_route_endpoint *dest = GET_PARSE_OBJ();
    dest->key = GET_INT_TOK();
    return 0;
}

PARSE_FN(set_dest_msu) {
    struct dfg_route_endpoint *dest = GET_PARSE_OBJ();
    int msu_id = GET_INT_TOK();

    struct dfg_msu *msu = get_dfg_msu(msu_id);
    if (msu == NULL) {
        // Wait for MSU to be instantiated
        log(LOG_DFG_PARSING, "MSU %d is not yet instantiated", msu_id);
        return 1;
    }
    dest->msu = msu;
    return 0;
}

PARSE_FN(set_source_types) {
    struct dfg_meta_routing *meta = GET_PARSE_OBJ();
    int i;
    bool found_types = true;
    START_ITER_TOK_LIST(i) {
        int str_type = GET_INT_TOK();
        struct dfg_msu_type *type = get_dfg_msu_type(str_type);
        if (type == NULL) {
            // Wait for type to be instantiated
            log(LOG_DFG_PARSING, "Type %d is not yet instantiated", str_type);
            found_types = false;
        } else {
            meta->src_types[i] = type;
        }
    } END_ITER_TOK_LIST(i)
    meta->n_src_types = i;
    if (found_types == false)
        return 1;
    return 0;
}

PARSE_FN(set_dst_types) {
    struct dfg_meta_routing *meta = GET_PARSE_OBJ();
    bool found_types = true;
    int i;
    START_ITER_TOK_LIST(i) {
        int str_type = GET_INT_TOK();
        struct dfg_msu_type *type = get_dfg_msu_type(str_type);
        if (type == NULL) {
            log(LOG_DFG_PARSING, "Type %d is not yet instantiated", str_type);
            found_types = false;
        } else {
            meta->dst_types[i] = type;
        }
    } END_ITER_TOK_LIST(i)

    meta->n_dst_types = i;
    if (found_types == false) {
        return 1;
    }
    return 0;
}

PARSE_FN(set_dep_type) {
    struct dfg_dependency *dep = GET_PARSE_OBJ();

    int type_id = GET_INT_TOK();
    struct dfg_msu_type *type = get_dfg_msu_type(type_id);

    if (type == NULL) {
        // Return once type has been instantiated
        log(LOG_DFG_PARSING, "Type %d is not yet instantiated", type_id);
        return 1;
    }
    dep->type = type;
    return 0;
}

PARSE_FN(set_dep_locality) {
    struct dfg_dependency *dep = GET_PARSE_OBJ();

    char *str_loc = GET_STR_TOK();
    if (strcasecmp(str_loc, "local") == 0) {
        dep->locality = MSU_IS_LOCAL;
    } else if (strcasecmp(str_loc, "remote") == 0) {
        dep->locality = MSU_IS_REMOTE;
    } else {
        log_error("Unknown locality %s specified. Must be 'local' or 'remote'", str_loc);
    }
    return 0;
}

PARSE_FN(set_msu_runtime) {
    struct dfg_scheduling *sched = GET_PARSE_OBJ();

    int id = GET_INT_TOK();
    struct dfg_runtime *rt = get_dfg_runtime(id);
    if (rt == NULL) {
        log(LOG_DFG_PARSING, "Runtime %d is not yet instantiated", id);
        return 1;
    }
    sched->runtime = rt;
    return 0;
}

PARSE_FN(set_msu_thread) {
    struct dfg_scheduling *sched = GET_PARSE_OBJ();

    if (sched->runtime == NULL) {
        // Runtime must be instantiated before thread
        log(LOG_DFG_PARSING, "MSU runtime not yet instantiated");
        return 1;
    }
    int id = GET_INT_TOK();
    struct dfg_thread *thread = get_dfg_thread(sched->runtime, id);
    if (thread == NULL) {
        // Thread must be instantiated too
        log(LOG_DFG_PARSING, "Thread %d not yet instantiated", id);
        return 1;
    }
    sched->thread = thread;

    // This is frustrating...
    // Have to iterate through all MSUs to find the one this scheduling object refers to
    // Should perhaps provide parse object as a linked list so you can traverse upwards?
    struct dedos_dfg *dfg = get_root_jsmn_obj();
    struct dfg_msu *msu = NULL;
    for (int i=0; i<dfg->n_msus; i++) {
        if (&dfg->msus[i]->scheduling == sched) {
            msu = dfg->msus[i];
            break;
        }
    }
    // Shouldn't happen, really...
    if (msu == NULL) {
        log(LOG_DFG_PARSING, "Msu for thead %d not yet instantiated", id);
        return 1;
    }
    thread->msus[thread->n_msus] = msu;
    thread->n_msus++;

    return 0;
}

PARSE_FN(set_msu_routes) {
    struct dfg_scheduling *sched = GET_PARSE_OBJ();

    bool set_routes = true;
    int i;
    log(LOG_DFG_PARSING, "MSU_ROUTE pre TOK: %s", GET_STR_TOK());
    START_ITER_TOK_LIST(i) {
        log(LOG_DFG_PARSING, "MSU_ROUTE TOK: %s", GET_STR_TOK());
        int route_id = GET_INT_TOK();
        struct dfg_route *route = get_dfg_route(route_id);
        if (route == NULL) {
            log(LOG_DFG_PARSING, "Route %d not yet instantiated for msu", route_id);
            set_routes = false;
        } else {
            sched->routes[i] = route;
        }
    } END_ITER_TOK_LIST(i)

    sched->n_routes = i;
    if (!set_routes) {
        return 1;
    }
    return 0;
}


static int not_implemented(jsmntok_t **tok, char *j, struct json_state *in, struct json_state **saved) {
    log_warn("JSON key %s is not implemented in DFG reader", tok_to_str((*tok)-1, j));
    return 0;
}

static struct key_mapping key_map[] = {
    { "application_name", ROOT, set_app_name },
    { "global_ctl_ip", ROOT, set_ctl_ip },
    { "global_ctl_port", ROOT, set_ctl_port },

    { "MSU_types", ROOT, set_msu_types },
    { "MSUs", ROOT, set_msus },
    { "runtimes", ROOT, set_runtimes },

    { "id", MSU_TYPES, set_msu_type_id},
    { "name", MSU_TYPES, set_msu_type_name },
    { "meta_routing", MSU_TYPES, set_meta_routing },
    { "dependencies", MSU_TYPES, set_dependencies },
    { "cloneable", MSU_TYPES, set_cloneable },
    { "colocation_group", MSU_TYPES, set_colocation_group },

    { "id", MSUS, set_msu_id },
    { "vertex_type", MSUS, set_msu_vertex_type },
    { "init_data", MSUS, set_msu_init_data },
    { "type", MSUS, set_msu_type },
    { "blocking_mode", MSUS,  set_blocking_mode },
    { "scheduling", MSUS,  set_scheduling },

    { "id", RUNTIMES, set_rt_id },
    { "ip", RUNTIMES,  set_rt_ip },
    { "port", RUNTIMES, set_rt_port },
    { "num_cores", RUNTIMES, set_rt_n_cores },
    { "num_pinned_threads", RUNTIMES, set_num_pinned_threads },
    { "num_unpinned_threads", RUNTIMES, set_num_unpinned_threads },
    { "routes", RUNTIMES, set_rt_routes },

    { "id", ROUTES, set_route_id },
    { "type", ROUTES, set_route_type },
    { "endpoints", ROUTES, set_route_endpoints },

    { "key", DESTINATIONS, set_dest_key },
    { "msu", DESTINATIONS, set_dest_msu },

    { "source_types", META_ROUTING,  set_source_types },
    { "dst_types", META_ROUTING, set_dst_types },

    { "type", DEPENDENCIES, set_dep_type },
    { "locality", DEPENDENCIES, set_dep_locality },

    { "runtime", SCHEDULING, set_msu_runtime },
    { "thread_id", SCHEDULING, set_msu_thread },
    { "routes", SCHEDULING, set_msu_routes },

    { "load_mode", ROOT, not_implemented },
    { "application_deadline", ROOT, not_implemented },
    { "profiling", MSUS, not_implemented },
    { "statistics", MSUS, not_implemented },
    { "dram", RUNTIMES, not_implemented },
    { "io_network_bw", RUNTIMES, not_implemented},
    { "deadline", SCHEDULING,  not_implemented},
    { NULL, 0, NULL }
};

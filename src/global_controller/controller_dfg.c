#include "dfg.h"
#include "logging.h"
#include "dfg_reader.h"
#include "msu_stats.h"

#include <stdlib.h>

static struct dedos_dfg *DFG = NULL;

int init_controller_dfg(char *filename) {

    DFG = parse_dfg_json_file(filename);

    if (DFG == NULL) {
        log_error("Error initializing DFG");
        return -1;
    }
    set_dfg(DFG);

    for (int i=0; i<DFG->n_msus; i++) {
        register_stat_item(DFG->msus[i]->id);
    }

    return 0;
}

int local_listen_port() {
    if (DFG == NULL) {
        return -1;
    }
    return DFG->global_ctl_port;
}

static int max_msu_id = -1;

int generate_msu_id() {
    for (int i=0; i<DFG->n_msus; i++) {
        if (DFG->msus[i]->id > max_msu_id) {
            max_msu_id = DFG->msus[i]->id;
        }
    }
    max_msu_id++;
    return max_msu_id;
}

#define MAX_ROUTE_ID 9999

int generate_route_id() {
    for (int id = 0; id < MAX_ROUTE_ID; id++) {
        if (get_dfg_route(id) == NULL) {
            return id;
        }
    }
    return -1;
}

uint32_t generate_endpoint_key(struct dfg_route *route) {
    if (route->n_endpoints == 0) {
        return 1;
    }
    return route->endpoints[route->n_endpoints-1]->key + 1;
}

struct dedos_dfg *get_dfg(void) {
    return DFG;
}
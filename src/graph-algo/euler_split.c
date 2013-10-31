/*
 * euler_split.c
 *
 *  Created on: October 21, 2013
 *      Author: aousterh
 */

#include "euler_split.h"
#include "graph.h"

// Splits graph_in into graph_1 and graph_2
void split(struct graph *graph_in, struct graph *graph_1,
           struct graph *graph_2) {
    assert(graph_in != NULL);
    assert(graph_1 != NULL);
    assert(graph_2 != NULL);

    uint8_t n = graph_in->n;
    uint8_t start_degree;
    assert((start_degree = get_max_degree(graph_in)) > 1);

    uint8_t node, cur_node, new_node;
    for (node = 0; node < n; node++) {
        cur_node = node;

        while (get_degree(graph_in, node) > 0) {
            // Peel off two edges and add them to g1 and g2
            new_node = get_neighbor(graph_in, cur_node);
            add_edge(graph_1, cur_node, new_node);
            remove_edge(graph_in, cur_node, new_node);
            cur_node = new_node;

            new_node = get_neighbor(graph_in, cur_node);
            add_edge(graph_2, cur_node, new_node);
            remove_edge(graph_in, cur_node, new_node);
            cur_node = new_node;
        }
    }

    assert(get_max_degree(graph_1) <= start_degree / 2);
    assert(get_max_degree(graph_2) <= start_degree / 2);
}

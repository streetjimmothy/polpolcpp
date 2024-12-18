#include "utilities.h"

void print_graph(const igraph_t* graph) {
	igraph_integer_t vertex_count = igraph_vcount(graph);
	igraph_integer_t edge_count = igraph_ecount(graph);

	for (igraph_integer_t i = 0; i < vertex_count; ++i) {
		const string v_name = igraph_cattribute_VAS(graph, "name", i);
		if (v_name != "") {
			std::cout << "Vertex " << i << " (" << v_name << ") -> Edges: ";
		} else {
			std::cout << "Vertex " << i << " -> Edges: ";
		}
		igraph_vector_int_t edges;
		igraph_vector_int_init(&edges, 0);
		igraph_incident(graph, &edges, i, IGRAPH_OUT);

		for (int j = 0; j < igraph_vector_int_size(&edges); ++j) {
			igraph_integer_t edge = VECTOR(edges)[j];
			igraph_integer_t from, to;
			igraph_edge(graph, edge, &from, &to);
			const string e_name = igraph_cattribute_VAS(graph, "name", to);
			if (e_name != "") {
				std::cout << to << " (" << e_name << ") ";
			} else {
				std::cout << to << " ";
			}
		}
		igraph_vector_int_destroy(&edges);
		std::cout << std::endl;
	}
};

void print_matrix(const igraph_matrix_t* matrix) {
	long int rows = igraph_matrix_nrow(matrix);
	long int cols = igraph_matrix_ncol(matrix);

	for (long int i = 0; i < rows; ++i) {
		for (long int j = 0; j < cols; ++j) {
			std::cout << MATRIX(*matrix, i, j) << " ";
		}
		std::cout << std::endl;
	}
};

void sehTranslator(unsigned int code, EXCEPTION_POINTERS* pExp) {
	throw std::runtime_error("SEH exception occurred");
}
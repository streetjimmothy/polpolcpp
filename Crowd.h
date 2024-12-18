#pragma once
#include "utilities.h"

class Crowd
{
private:
	

	const unsigned char min_k = 2;
	unsigned char max_k = UCHAR_MAX;
	const unsigned char min_m = 1;
	unsigned char max_m = UCHAR_MAX;
	string node_key = "T";

	ptr<Graph> graph; //leidenalg graph
	igraph_t* i_graph; //igraph graph - this is not a ptr because we need to destroy it manually anyway
	igraph_vector_t* weights = NULL;

	//map<auto, auto> precomputed_path_dict = {}; // "holds unconditional paths" - figure out types later?
	//map<auto, auto> precomputed_paths_by_hole_node = {}; // "holds dict of paths per node" - figure out types later?
	ptr<vector<ptr<pair<uint, uint>>>> create_pair_list(const ptr<vector<uint>> list);
	uint len_shortest_path_excluding_v(uint source, uint target, uint v);
public:
	const bool verbose = VERBOSE;	//we have a property for this so it can be accessed from the test project
	Crowd(ptr<Graph> G, bool weighted = false, string node_key = "T");
	Crowd(igraph_t* i_g, bool weighted = false, string node_key = "T");
	~Crowd();

	bool is_mk_observer(uint v, ubyte m, ubyte k);
};


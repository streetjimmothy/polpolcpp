#include "Crowd.h"

using namespace std;

Crowd::Crowd(ptr<Graph> G, bool weighted, string node_key)
{
	graph = G;
	i_graph = G->get_igraph();
	max_m = max_m;
	node_key = node_key;
	if (weighted) {
		weights = new igraph_vector_t();
		igraph_vector_init(weights, 0);
		igraph_cattribute_EANV(graph->get_igraph(), "weight", igraph_ess_all(IGRAPH_EDGEORDER_ID), weights);
	}
}

Crowd::Crowd(igraph_t* i_g, bool weighted, string node_key)
{
	i_graph = i_g;
	graph = _ptr<Graph>(i_g);
	node_key = node_key;
	if (weighted) {
		weights = new igraph_vector_t();
		igraph_vector_init(weights, 0);
		igraph_cattribute_EANV(i_g, "weight", igraph_ess_all(IGRAPH_EDGEORDER_ID), weights);
	}
};

Crowd::~Crowd()
{
	if (weights != NULL) {
		igraph_vector_destroy(weights);
		delete weights;
	}
	igraph_destroy(i_graph);
	delete i_graph;
};

bool Crowd::is_mk_observer(uint v, ubyte m, ubyte k)
{
	if (verbose) { cout << "Checking if vertex " << v << " is an (" << (int)m << "," << (int)k << ")-observer" << endl; }

	//TODO: cache?
	/*is_mk_observer: checks if the vertex v is an (m,k)-observer as defined by (Sullivan et al., 2020);*/
	/*
	optimized clique-finding algo by CVK.

		Args:
			v: vertex to evaluate
			m: m as defined in (Sullivan et al., 2020); m >= 1
			k: k as defined in (Sullivan et al., 2020); k > 1
		Returns:
			a boolean indicating the m,k-observer status of v
	*/
	if (m < 1 || k <= 1) {
		throw std::invalid_argument("Invalid m or k value. m needs to be integer >= 1; k needs to be integer > 1.");
	}

	//TODO: refactor to just use the igraph neighbours object directly
	igraph_vs_t* neighbours = new igraph_vs_t();
	igraph_vs_adj(neighbours, v, IGRAPH_IN);
	igraph_vit_t* neighbours_iterator = new igraph_vit_t();
	igraph_vit_create(graph->get_igraph(), *neighbours, neighbours_iterator);
	ptr<vector<uint>> neighbour_list = _ptr<vector<uint>>();
	while (!IGRAPH_VIT_END(*neighbours_iterator)) {
		neighbour_list->push_back(IGRAPH_VIT_GET(*neighbours_iterator));
		IGRAPH_VIT_NEXT(*neighbours_iterator);
	}

	// Clean up neighbours and neighbours_iterator
	igraph_vit_destroy(neighbours_iterator);
	delete neighbours_iterator;
	igraph_vs_destroy(neighbours);
	delete neighbours;

	// if you have fewer than k neighbours, then you can't hear from at least k
	if (neighbour_list->size() < k) {
		return false;
	}

	// Special case to ensure that a node with one input is a (1,1)-observer
	//this is impossible to reach becasue if k == 1, then we will have excepted above
	if (neighbour_list->size() == 1 && k == 1 && m == 1) {
		return true;
	}

	if (verbose) {
		cout << "Neighbours of " << v << " are: " << endl;
		for (const uint& n : *neighbour_list) {
			cout << n << endl;
		}
	}

	bool max_k_found = false;
	map<uint, ptr<vector<ptr<set<uint>>>>> clique_dict; // this will get used to look for cliques

	//we need to get each pair of neighbours
	ptr<vector<ptr<pair<uint, uint>>>> neighbour_pairs = create_pair_list(neighbour_list);

	if (verbose) {
		cout << "neighbour_pairs of " << v << " are: " << endl;
		for (const ptr<pair<uint, uint>>& n : *neighbour_pairs) {
			cout << n->first << "," << n->second << endl;
		}
	}

	//we take a pair of neighbours and 
	for (const ptr<pair<uint, uint>> p : *neighbour_pairs) {
		uint a = p->first;
		uint b = p->second;

		if (verbose) cout << "Checking pair " << a << "," << b << endl;

		// If the shortest path between a and b is less than m, then the nodes aren't m-independent 
		uint a_path_length = len_shortest_path_excluding_v(a, b, v);
		uint b_path_length = len_shortest_path_excluding_v(b, a, v);
		if ((a_path_length < m) || (b_path_length < m)) {
			continue;
		}

		if (k <= 2) {
			return true;
		}

		// Now we do the clique updating
		// First, each pair trivially forms a clique
		// Pairs are unique so we don't have to double-check as we go (hopefully!)
		ptr<set<uint>> trivial_clique = _ptr<set<uint>>();
		trivial_clique->insert(a);
		trivial_clique->insert(b);
		if (clique_dict.find(a) == clique_dict.end()) {
			clique_dict[a] = _ptr<vector<ptr<set<uint>>>>();
		}
		clique_dict[a]->emplace_back(trivial_clique);
		if (clique_dict.find(b) == clique_dict.end()) {
			clique_dict[b] = _ptr<vector<ptr<set<uint>>>>();
		}
		clique_dict[b]->emplace_back(trivial_clique);

		if (verbose) {
			cout << "Clique dict for " << a << " is: " << endl;
			for (const ptr<set<uint>>& c : *clique_dict[a]) {
				for (const uint& n : *c) {
					cout << n << ",";
				}
				cout << endl;
			}
			cout << "Clique dict for " << b << " is: " << endl;
			for (const ptr<set<uint>>& c : *clique_dict[b]) {
				for (const uint& n : *c) {
					cout << n << ",";
				}
				cout << endl;
			}
		}

		ptr<vector<ptr<pair<ptr<set<uint>>, ptr<set<uint>>>>>> cliques = cartesian_product(clique_dict[a], clique_dict[b]);
		if (verbose) {
			cout << "Cartesian product of cliques is: " << endl;
			for (const ptr<pair<ptr<set<uint>>, ptr<set<uint>>>> p2 : *cliques) {
				ptr<set<uint>> c1 = p2->first;
				ptr<set<uint>> c2 = p2->second;
				for (const uint& n : *c1) {
					cout << n << ",";
				}
				cout << " and ";
				for (const uint& n : *c2) {
					cout << n << ",";
				}
				cout << endl;
			}
		}
		for (const ptr<pair<ptr<set<uint>>, ptr<set<uint>>>> p2 : *cliques) {
			ptr<set<uint>> c1 = p2->first;
			ptr<set<uint>> c2 = p2->second;
			int lena = c1->size();
			int lenb = c2->size();
			if (lena != lenb) {
				continue;
			}
			// Avoid double counting
			// Though you can probably do this faster by not adding the trivial clique until later?
			if (c1 == trivial_clique || c2 == trivial_clique) {
				continue;
			}

			if (verbose) {
				cout << "Checking clique: " << endl;
				for (const uint& n : *c1) {
					cout << n << ",";
				}
				cout << " and ";
				for (const uint& n : *c2) {
					cout << n << ",";
				}
				cout << endl;
			}

			//for some reason if we use an iterator instead of explicitly inserting them, it triggers read acccess violation later
			//.. or maybe it was just pointer shananigans?
			ptr<set<uint>> node_union = _ptr<set<uint>>();
			for (const uint& node : *c1) {
				node_union->insert(node);
			}
			for (const uint& node : *c2) {
				node_union->insert(node);
			}
			int lenu = node_union->size();
			if (lenu == (lena + 1)) {
				if (lenu >= k) {  // Early termination
					return true;
				}
				else {
					for (const uint& node : *node_union) {
						clique_dict[node]->emplace_back(node_union);
					}
				}
			}

			if (verbose) {
				cout << "State of clique dict is: " << endl;
				for (const auto& kv : clique_dict) {
					cout << "For node " << kv.first << endl;
					for (const auto& c : *kv.second) {
						for (const auto& n : *c) {
							cout << n << ",";
						}
						cout << endl;
					}
				}
			}
		}
	}

	return max_k_found;
};

//iterates over a list and returns a map of each possible pair of elements
ptr<vector<ptr<pair<uint, uint>>>> Crowd::create_pair_list(const ptr<vector<uint>> list)
{
	ptr<vector<ptr<pair<uint, uint>>>> pairs = _ptr<vector<ptr<pair<uint, uint>>>>();
	for (auto itr = list->begin(); itr != list->end(); itr++)
	{
		for (auto itr2 = list->begin(); itr2 != itr; itr2++)
		{
			pairs->push_back(_ptr<pair<uint, uint>>(*itr, *itr2));
		}
	}
	return pairs;
};

uint Crowd::len_shortest_path_excluding_v(uint s, uint t, uint v)
{
	//TODO: cache

	// Remove the specified vertex
	// if you just delete the vertex, then vertex indices will change
	// so we'll just delete the edges
	igraph_t* new_graph = new igraph_t();
	igraph_copy(new_graph, i_graph); // Create a copy of the original graph
	igraph_vector_int_t edges;
	igraph_vector_int_init(&edges, 0);
	igraph_incident(i_graph, &edges, v, IGRAPH_ALL);
	igraph_delete_edges(new_graph, igraph_ess_vector(&edges));

	if (verbose) {
		//print_graph(new_graph);
	}

	igraph_matrix_t* igraph_result = new igraph_matrix_t();
	igraph_matrix_init(igraph_result, 1, 1);
	igraph_vs_t* source = new igraph_vs_t();
	igraph_vs_1(source, s);
	igraph_vs_t* target = new igraph_vs_t();
	igraph_vs_1(target, t);
	igraph_distances_dijkstra(new_graph, igraph_result, *source, *target, weights, IGRAPH_OUT);	//should this be out or all?
	igraph_real_t result = igraph_matrix_get(igraph_result, 0, 0);
	
	igraph_destroy(new_graph);
	delete new_graph;
	igraph_matrix_destroy(igraph_result);
	delete igraph_result;
	igraph_vs_destroy(source);
	delete source;
	igraph_vs_destroy(target);
	delete target;

	//TODO: They subtract one before returning. Do we need to do the same?
	//their reasoning: #z-1 because the path is a list of nodes incl start and end ; we're using distance=number of edges, which is 1 less.
	if (result == IGRAPH_INFINITY) {
		//left to it's own devices, IGRAPH_INFINITY converts to 0 in an int
		return INT_MAX;
	}
	return result;
};




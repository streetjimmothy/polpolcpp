#include "pch.h"
#include "../Crowd.h"
#include "../utilities.h"   //included for the windows exception handler
#include <gtest/gtest.h>

class CrowdTest : public ::testing::Test, public ::testing::WithParamInterface<int> {
public:
	igraph_t* __construct_test_crowd_ab_only() {
		igraph_t* g = new igraph_t();
		igraph_empty(g, 2, true);
		igraph_add_edge(g, 0, 1);
		return g;
	}

    igraph_t* __construct_test_crowd_5nodes() {
        // 0->1->2->3<-->4
        // \_____________^
        igraph_t* g = new igraph_t();
        igraph_empty(g, 5, true);
        igraph_add_edge(g, 0, 1);
        igraph_add_edge(g, 1, 2);
        igraph_add_edge(g, 2, 3);
        igraph_add_edge(g, 3, 4);
        igraph_add_edge(g, 4, 3);
        igraph_add_edge(g, 0, 4);
        return g;
    }

    igraph_t* __construct_test_crowd_4nodes_undirected() {
        // 0<->1<->2<->3
        igraph_t* g = new igraph_t();
        igraph_empty(g, 4, false);
        igraph_add_edge(g, 0, 1);
        igraph_add_edge(g, 1, 2);
        igraph_add_edge(g, 2, 3);
        return g;
    }

    igraph_t* __construct_florentine_bidirectional() {
        // from https://networkx.org/documentation/stable/_modules/networkx/generators/social.html#florentine_families_graph
        igraph_t* g = new igraph_t();
        igraph_empty(g, 16, false);

        igraph_cattribute_VAS_set(g, "name", 0, "Acciaiuoli");
        igraph_cattribute_VAS_set(g, "name", 1, "Medici");
        igraph_cattribute_VAS_set(g, "name", 2, "Castellani");
        igraph_cattribute_VAS_set(g, "name", 3, "Peruzzi");
        igraph_cattribute_VAS_set(g, "name", 4, "Strozzi");
        igraph_cattribute_VAS_set(g, "name", 5, "Barbadori");
        igraph_cattribute_VAS_set(g, "name", 6, "Ridolfi");
        igraph_cattribute_VAS_set(g, "name", 7, "Tornabuoni");
        igraph_cattribute_VAS_set(g, "name", 8, "Albizzi");
        igraph_cattribute_VAS_set(g, "name", 9, "Salviati");
        igraph_cattribute_VAS_set(g, "name", 10, "Pazzi");
        igraph_cattribute_VAS_set(g, "name", 11, "Bischeri");
        igraph_cattribute_VAS_set(g, "name", 12, "Guadagni");
        igraph_cattribute_VAS_set(g, "name", 13, "Ginori");
        igraph_cattribute_VAS_set(g, "name", 14, "Lamberteschi");
        igraph_cattribute_VAS_set(g, "name", 15, "Pucci");

        igraph_add_edge(g, 0, 1);
        igraph_add_edge(g, 2, 3);
        igraph_add_edge(g, 2, 4);
        igraph_add_edge(g, 2, 5);
        igraph_add_edge(g, 1, 5);
        igraph_add_edge(g, 1, 6);
        igraph_add_edge(g, 1, 7);
        igraph_add_edge(g, 1, 8);
        igraph_add_edge(g, 1, 9);
        igraph_add_edge(g, 9, 10);
        igraph_add_edge(g, 3, 4);
        igraph_add_edge(g, 3, 11);
        igraph_add_edge(g, 4, 6);
        igraph_add_edge(g, 4, 11);
        igraph_add_edge(g, 6, 7);
        igraph_add_edge(g, 7, 12);
        igraph_add_edge(g, 8, 12);
        igraph_add_edge(g, 8, 13);
        igraph_add_edge(g, 11, 12);
        igraph_add_edge(g, 12, 14);

		//they convert it to directed so we do too
		igraph_to_directed(g, IGRAPH_TO_DIRECTED_MUTUAL);
        return g;
    }

	void SetUp() override {
        //prevent igraph from killing everything on error. igraph fucntions will return error codes instead
        igraph_set_error_handler(igraph_error_handler_ignore);
        //prevent windows from killing everything on error. windows errors will throw regular exceptions instead
        _set_se_translator(sehTranslator);
        //allows the use of vertex and edge attributes
        //this is needed because the igraph attribute features are more intended to be used with Python and R than C++
        igraph_set_attribute_table(&igraph_cattribute_table);
	}
};

TEST_F(CrowdTest, mkvalidtests) {
    //tests: invalid m and/or k
    Crowd* c = new Crowd(__construct_test_crowd_ab_only());
    EXPECT_THROW(c->is_mk_observer(0, 0, 0), std::invalid_argument);
    EXPECT_THROW(c->is_mk_observer(0, 1, 1), std::invalid_argument);
};

TEST_F(CrowdTest, simplegraphtests) {
    Crowd* c = new Crowd(__construct_test_crowd_5nodes());
    for (unsigned char m = 1; m < 6; m++) {
        for (unsigned char k = 2; k < 6; k++) {
            if (k == 2) {
                EXPECT_TRUE(c->is_mk_observer(3, m, k));
            }
            else {
                EXPECT_FALSE(c->is_mk_observer(3, m, k));
            }
        }
    }
};

TEST_F(CrowdTest, simpleundirectedgraphtests) {
    Crowd* c = new Crowd(__construct_test_crowd_4nodes_undirected());
    for (unsigned char m = 1; m < 6; m++) {
        for (unsigned char k = 2; k < 6; k++) {
            if (k == 2) {
                EXPECT_TRUE(c->is_mk_observer(2, m, k));
            }
            else {
                EXPECT_FALSE(c->is_mk_observer(2, m, k));
            }
        }
    }
}

TEST_F(CrowdTest, florentinegraphtests) {
    Crowd* c = new Crowd(__construct_florentine_bidirectional());
    /*  NB: loops intentionally unrolled to clearly demonstrate ground truth
        m / k   1    2    3    4    5
        ------------------------------
         1     Err  Y    Y    Y    Y
         2     Err  Y    Y    Y    Y
         3     Err  Y    Y    Y    Y
         4     Err  Y    Y    Y    N
         5     Err  Y    Y    Y    N
        ------------------------------
        Note: Medici is vertex 1
    */

	unsigned char k = 1;
	for (unsigned char m = 1; m < 6; m++) {
		EXPECT_THROW(c->is_mk_observer(1, m, k), std::invalid_argument);
	}
	k = 2;
	for (unsigned char m = 1; m < 6; m++) {
		EXPECT_TRUE(c->is_mk_observer(1, m, k)); //TRUE: Accaiuoli-Salviati infinitely apart
	}
	k = 3;
	for (unsigned char m = 1; m < 6; m++) {
		EXPECT_TRUE(c->is_mk_observer(1, m, k)); //TRUE: Accaiuoli-Salviati infinitely apart from Barbadori
	}
	k = 4;
	for (unsigned char m = 1; m < 6; m++) {
		EXPECT_TRUE(c->is_mk_observer(1, m, k)); //TRUE: Accaiuoli-Salviati inf, Barbadori-Tornabuoni (OR Barbadori-Albizzi): 4 nodes, >4-independent (via Castellani...)
	}
	k = 5;
	for (unsigned char m = 1; m < 4; m++) {
		EXPECT_TRUE(c->is_mk_observer(1, m, k)); //TRUE: Accaiuoli-Salviati inf, Barbadori-Ridolfi, Barbadori-Albizzi: 5 nodes, <=3-independent (cannot consider Tornabuoni shortcut)
	}
	for (unsigned char m = 4; m < 6; m++) {
		EXPECT_FALSE(c->is_mk_observer(1, m, k)); //FALSE: ...as above, can't find any 5 nodes w/4-deg-of-separation minimum
	}
}
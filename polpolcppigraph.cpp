
#include <fstream>

#include <boost/json.hpp>
#include <boost/iostreams/filtering_streambuf.hpp>
#include <boost/iostreams/copy.hpp>
#include <boost/iostreams/filter/gzip.hpp>
#include <boost/exception/diagnostic_information.hpp>
#include <mongocxx/client.hpp>
#include <mongocxx/instance.hpp>
#include <mongocxx/exception/bulk_write_exception.hpp>
#include <mongocxx/exception/operation_exception.hpp>
#include <bsoncxx/json.hpp>

#include "utilities.h"
#include "ThreadPool.h"

using namespace std;

//forward declarations
bsoncxx::document::value convertObjectToBson(const boost::json::object& obj);

const string tweet_properties_to_delete[] = {
	"id_str",	//WE COPY THE ID_STR INTO ID AND USE ID AS THE PRIMARY KEY
	"source",
	"in_reply_to_status_id",
	"in_reply_to_status_id_str",
	"in_reply_to_user_id",
	"in_reply_to_user_id_str",
	"in_reply_to_screen_name",
	//"user",				//user is copied to another object and insterted into a different collection and remains as the id
	"quoted_status_id_str",
	"quoted_status",	//quoted_status is processed as a seperate tweet
	"retweeted_status",	//retweeted_status is processed as a seperate tweet
	"extended_tweet",	//grab full_text from extended_tweet, discard the rest
};
const string user_properties_to_delete[] = {
	"id_str",
	"translator_type",
	"is_translator",
	"profile_background_color",
	"profile_background_image_url",
	"profile_background_image_url_https",
	"profile_background_tile",
	"profile_link_color",
	"profile_sidebar_border_color",
	"profile_sidebar_fill_color",
	"profile_text_color",
	"profile_use_background_image",
	"profile_image_url",
	"profile_image_url_https",
	"profile_banner_url",
	"default_profile",
	"default_profile_image",
	"following",
	"follow_request_sent",
	"notifications"
};

int number_of_tweets_processed = 0;
int number_of_users_processed = 0;

map<string, bsoncxx::document::value> tweets_to_be_uploaded;
map<string, bsoncxx::document::value> users_to_be_uploaded;

bsoncxx::array::value convertArrayToBSON(const boost::json::array& arr) {
	bsoncxx::builder::basic::array bsonArray;
	for (const auto& elem : arr) {
		switch (elem.kind()) {
		case boost::json::kind::string:
			bsonArray.append(elem.as_string());
			break;
		case boost::json::kind::int64:
			bsonArray.append(elem.as_int64());
			break;
		case boost::json::kind::uint64:
			bsonArray.append(bsoncxx::types::b_decimal128(std::to_string(elem.as_uint64())));
			break;
		case boost::json::kind::double_:
			bsonArray.append(elem.as_double());
			break;
		case boost::json::kind::bool_:
			bsonArray.append(elem.as_bool());
			break;
		case boost::json::kind::object:
			bsonArray.append(bsoncxx::builder::concatenate_doc(convertObjectToBson(elem.as_object()).view()));
			break;
		case boost::json::kind::array:
			bsonArray.append(bsoncxx::builder::concatenate_array(convertArrayToBSON(elem.as_array()).view()));
			break;
		default:
			break;
		}
	}
	return bsonArray.extract();
}

bsoncxx::document::value convertObjectToBson(const boost::json::object& obj) {
	bsoncxx::builder::basic::document doc;

	for (const auto& [key, value] : obj) {
		string s_key = string(key);
		if (value.is_null()) continue;
		if (key == "created_at") {	//SPECIAL CASE FOR THE DATES
			// Convert the string to a BSON date object
			std::tm tm = {};
			std::istringstream ss(value.as_string().c_str());
			ss >> std::get_time(&tm, "%a %b %d %H:%M:%S +0000 %Y");

			doc.append(
				bsoncxx::builder::basic::kvp(
					"datetime",
					bsoncxx::types::b_date(
						std::chrono::duration_cast<std::chrono::milliseconds>(
							std::chrono::system_clock::from_time_t(std::mktime(&tm)).time_since_epoch()
						)
					)
				)
			);
			continue;
		}
		switch (value.kind()) {
		case boost::json::kind::string:
			doc.append(bsoncxx::builder::basic::kvp(s_key, string(value.as_string())));
			break;
		case boost::json::kind::int64:
			doc.append(bsoncxx::builder::basic::kvp(s_key, value.as_int64()));
			break;
		case boost::json::kind::uint64:
			doc.append(bsoncxx::builder::basic::kvp(s_key, bsoncxx::types::b_decimal128(std::to_string(value.as_uint64()))));
			break;
		case boost::json::kind::double_:
			doc.append(bsoncxx::builder::basic::kvp(s_key, value.as_double()));
			break;
		case boost::json::kind::bool_:
			doc.append(bsoncxx::builder::basic::kvp(s_key, value.as_bool()));
			break;
		case boost::json::kind::object:
			doc.append(bsoncxx::builder::basic::kvp(s_key, convertObjectToBson(value.as_object())));
			break;
		case boost::json::kind::array:
			doc.append(bsoncxx::builder::basic::kvp(s_key, convertArrayToBSON(value.as_array())));
			break;
		default:
			break;
		}
	}

	return doc.extract();
}

void parse_user(boost::json::object& user) {
	string user_id = user["id_str"].as_string().c_str();
	if (users_to_be_uploaded.find(user_id) != users_to_be_uploaded.end()) {
		//user has already been processed
		return;
	}

	user["id"] = user["id_str"];
	user["_id"] = "";
	user["_id"] = user["id"];
	//user.datetime = new Date(user.created_at); //we need the date to be a BSON date object, so we're going to add it to the BSON just before pushing to the database

	for (const string property_to_delete : user_properties_to_delete) {
		user.erase(property_to_delete);
	}

	users_to_be_uploaded.insert(make_pair(user_id, convertObjectToBson(user)));
	number_of_users_processed++;
};

string expand_tweet_text(boost::json::object& tweet) {
	string text = tweet["text"].as_string().c_str();
	//expand truncated tweets
	if (tweet.contains("truncated") && tweet["truncated"].as_bool()) {
		try {
			text = tweet["extended_tweet"].as_object()["full_text"].as_string().c_str();

		} catch (const boost::exception& e) {
			cerr << "Boost error: " << boost::current_exception_diagnostic_information() << '\n';
		}
	}
	return text;
}

void parse_tweet(boost::json::object& tweet) {
	//some entries look like stream metatadata 
	//e.g. {"limit":{"track":27,"timestamp_ms":"1584043576755"}}
	//we skip these
	if (!tweet.contains("id")) {
		return;
	}
	string tweet_id = tweet["id_str"].as_string().c_str();
	if (tweets_to_be_uploaded.find(tweet_id) != tweets_to_be_uploaded.end()) {
		//tweet has already been processed
		return;
	}

	//we use the str version of the tweet id everywhere to avoid things being converted into strange rounded floating point numbers
	tweet["id"] = tweet["id_str"];
	tweet["_id"] = "";
	tweet["_id"] = tweet["id_str"];

	//tweet["datetime"] = new Date(tweet.created_at); //we need the date to be a BSON date object, so we're going to add it to the BSON just before pushing to the database

	//expand truncated tweets
	tweet["text"] = expand_tweet_text(tweet);

	//first check for retweets, quotes, replies and process those
	if (tweet.contains("quoted_status") && !tweet["quoted_status"].is_null()) {
		try {
			tweet["connected_user"] = tweet["quoted_status"].as_object()["user"].as_object()["id_str"];
			tweet["connection_type"] = "quote";
			tweet["connected_tweet"] = tweet["quoted_status"].as_object()["id_str"];
			parse_tweet(tweet["quoted_status"].as_object());
			tweet.erase("quoted_status");
		} catch (const boost::exception& e) {
			cerr << "Boost error: " << boost::current_exception_diagnostic_information() << '\n';
		}
	}
	if (tweet.contains("retweeted_status") && !tweet["retweeted_status"].is_null()) {
		try {
			tweet["connected_user"] = tweet["retweeted_status"].as_object()["user"].as_object()["id_str"];
			tweet["connection_type"] = "retweet";
			tweet["connected_tweet"] = tweet["retweeted_status"].as_object()["id_str"];
			parse_tweet(tweet["retweeted_status"].as_object());
			tweet.erase("retweeted_status");
		} catch (const boost::exception& e) {
			cerr << "Boost error: " << boost::current_exception_diagnostic_information() << '\n';
		}
	}
	if (tweet.contains("in_reply_to_status_id") && !tweet["in_reply_to_status_id"].is_null()) {
		//Replies don't have the text/object of the tweet they are replying to
		try {
			tweet["connected_user"] = tweet["in_reply_to_user_id_str"];
			tweet["connection_type"] = "reply";
			tweet["connected_tweet"] = tweet["in_reply_to_status_id_str"];
		} catch (const boost::exception& e) {
			cout << tweet << endl << endl;
			cerr << "Boost error: " << boost::current_exception_diagnostic_information() << '\n';
		}
	}

	//second take user data and process that
	boost::json::object user = tweet["user"].as_object();
	if (tweet.contains("user")) {
		parse_user(user);
	}
	tweet["user"] = user["id"];

	for (const string property_to_delete : tweet_properties_to_delete) {
		tweet.erase(property_to_delete);
	}

	tweets_to_be_uploaded.insert(make_pair(tweet_id, convertObjectToBson(tweet)));
	number_of_tweets_processed++;
}

// Function to split documents into batches
vector<vector<bsoncxx::document::view>> splitIntoBatches(const std::vector<const bsoncxx::document::value*>& documents) {
	const size_t limitBytes = 48 * 1024 * 1024; // 48MB in bytes
	std::vector<std::vector<bsoncxx::document::view>> batches;
	std::vector<bsoncxx::document::view> currentBatch;
	size_t currentBatchSize = 0;

	for (const auto doc : documents) {
		size_t docSize = doc->view().length();

		// Check if adding this document would exceed the limit
		if (currentBatchSize + docSize > limitBytes) {
			// If so, save the current batch and start a new one
			batches.push_back(currentBatch);
			currentBatch.clear();
			currentBatchSize = 0;
		}

		// Add the document to the current batch and update the batch size
		currentBatch.push_back(doc->view());
		currentBatchSize += docSize;
	}

	// Add the last batch if it has documents
	if (!currentBatch.empty()) {
		batches.push_back(currentBatch);
	}

	return batches;
}

// Function to convert bsoncxx::type to string
string bsontype_to_string(bsoncxx::type bsonType) {
	static const std::unordered_map<bsoncxx::type, std::string> typeMap = {
		{bsoncxx::type::k_double, "double"},
		{bsoncxx::type::k_utf8, "string"},
		{bsoncxx::type::k_document, "document"},
		{bsoncxx::type::k_array, "array"},
		{bsoncxx::type::k_binary, "binary"},
		{bsoncxx::type::k_undefined, "undefined"},
		{bsoncxx::type::k_oid, "oid"},
		{bsoncxx::type::k_bool, "bool"},
		{bsoncxx::type::k_date, "date"},
		{bsoncxx::type::k_null, "null"},
		{bsoncxx::type::k_regex, "regex"},
		{bsoncxx::type::k_dbpointer, "dbpointer"},
		{bsoncxx::type::k_code, "code"},
		{bsoncxx::type::k_symbol, "symbol"},
		{bsoncxx::type::k_codewscope, "code with scope"},
		{bsoncxx::type::k_int32, "int32"},
		{bsoncxx::type::k_timestamp, "timestamp"},
		{bsoncxx::type::k_int64, "int64"},
		{bsoncxx::type::k_maxkey, "max key"},
		{bsoncxx::type::k_minkey, "min key"}
	};

	auto it = typeMap.find(bsonType);
	if (it != typeMap.end()) {
		return it->second;
	} else {
		return "unknown";
	}
}

// Function to convert bsoncxx::document::element to string
string bsonvalue_to_string(bsoncxx::document::element value) {
	if (value.type() == bsoncxx::type::k_double) {
		return to_string(value.get_double());
	} else if (value.type() == bsoncxx::type::k_utf8) {
		return string(value.get_string());
	} else if (value.type() == bsoncxx::type::k_int32) {
		return to_string(value.get_int32());
	} else if (value.type() == bsoncxx::type::k_int64) {
		return to_string(value.get_int64());
	} else {
		throw runtime_error("Type not implemented");
	}
}

void upload_documents(vector<const bsoncxx::document::value*> documents) {

	mongocxx::client conn{ mongocxx::uri{"mongodb://JamIs:morticiaetpollito@118.138.244.29:27017/"} };
	auto collection = conn["Tw_Covid_DB"]["tweets"];

	const auto batches = splitIntoBatches(documents);
	int batch_count = 0;
	for (const auto batch : batches) {
		batch_count++;
		mongocxx::options::insert insert_options;
		insert_options.ordered(false);
		try {
			cout << "Inserting batch " << batch_count << " of " << batches.size() << endl;
			cout << "Inserting " << batch.size() << " documents" << endl;
			auto result = collection.insert_many(batch, insert_options);
			if (result) {
				std::cout << "Inserted: " << result->inserted_count() << " documents." << std::endl;
			}
		} catch (const mongocxx::bulk_write_exception& e) {
			if (e.raw_server_error()) {
				auto error_bson = (*(e.raw_server_error())).view();
				try {
					cout << "Inserted: " << error_bson["nInserted"].get_int32() << endl;
				} catch (const exception& e1) {
					// Handle standard exceptions
					cerr << "Standard exception: " << e1.what() << '\n';
					std::cout << bsoncxx::to_json(*(e.raw_server_error())) << std::endl;
				}
				auto write_errors = error_bson["writeErrors"].get_array().value;
				for (const auto element : write_errors) {
					auto elem = element.get_document().view()["code"];
					if (elem.get_int32() != 11000) {
						cout << "REAL ERROR" << endl;
						std::cout << bsoncxx::to_json(*(e.raw_server_error())) << std::endl;
					}
				}
			}
		} catch (const mongocxx::exception& e) {
			std::cerr << "An error occurred: " << e.what() << std::endl;
		}
	}
}

bsoncxx::types::b_date createBsonDateFromString(const string& date_str) {
	tm tm = {};
	istringstream ss(date_str);
	ss >> get_time(&tm, "%Y-%m-%d %H:%M:%S");

	if (ss.fail()) {
		throw runtime_error("Failed to parse date/time string");
	}

	return bsoncxx::types::b_date(chrono::system_clock::from_time_t(mktime(&tm)));
}

void printMemoryUsage() {
	PROCESS_MEMORY_COUNTERS_EX pmc;
	if (GetProcessMemoryInfo(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS*)&pmc, sizeof(pmc))) {
		SIZE_T physMemUsedByMe = pmc.WorkingSetSize;
		std::cout << "Memory used: " << physMemUsedByMe / 1024 / 1024 << "MB" << std::endl;
	} else {
		std::cerr << "Failed to get memory usage information." << std::endl;
	}
}

igraph_vector_int_t* get_v_to_cull(igraph_t* g, int start, int end, int min_connections = 2) {
	igraph_vector_int_t* v_to_cull = new igraph_vector_int_t();
	igraph_vector_int_init(v_to_cull, 0);
	igraph_vector_int_t* degrees = new igraph_vector_int_t();
	igraph_vector_int_init(degrees, 0);
	igraph_degree(g, degrees, igraph_vss_all(), IGRAPH_ALL, IGRAPH_NO_LOOPS);
	for (int i = start; i < end; i++) {
		if (VECTOR(*degrees)[i] < min_connections) {
			igraph_vector_int_push_back(v_to_cull, i);
		}
	}
	igraph_vector_int_destroy(degrees);
	return v_to_cull;
}


void iteratively_prune_graph(igraph_t* g, int min_connections = 2, int min_edge_weight = 1) {
	bool done = false;
	int iter = 0;
	while (!done) {
		cout << "Iteration: " << iter << endl;
		cout << "Vertices: " << igraph_vcount(g) << endl;
		cout << "Edges: " << igraph_ecount(g) << endl;
		auto start = chrono::high_resolution_clock::now();
		igraph_vector_int_t* v_to_cull = new igraph_vector_int_t();
		igraph_vector_int_init(v_to_cull, 0);

		ThreadPool::getInstance()->distribute(get_v_to_cull, g, min_connections);

		igraph_vector_int_destroy(v_to_cull);
	}
}

void build_graph(mongocxx::cursor* cursor, uint64_t proj_graph_size, 
	//out params
	igraph_t* out_graph, 
	ptr<map<string, int>> out_user_vertex_IDs, //map of userIDs to vertex IDs - TODO: should this be the other way around?
	ptr<map<string, ptr<vector<string>>>> out_user_tweets //map of userIDs to tweets
) {
	cout << "Building graph..." << endl;
	auto start = chrono::high_resolution_clock::now();
	igraph_sparsemat_t* sm = new igraph_sparsemat_t();
	igraph_sparsemat_init(
		sm,
		proj_graph_size,
		proj_graph_size,
		proj_graph_size //"expected maximum number of non-zero elements". The maximum maximum would be count*count, but we're not expecting the matrix to be anywhere near that dense
	);

	cout << "Adding users to graph..." << endl;
	// Iterate over the cursor and add the documents to an adjacency matrix
	for (const bsoncxx::document::view doc : *cursor) {
		string user_id = bsonvalue_to_string(doc["user"]);
		string connected_user_id = bsonvalue_to_string(doc["connected_user"]);
		if (user_id == connected_user_id) {
			continue;
		}

		if (out_user_vertex_IDs->find(user_id) == out_user_vertex_IDs->end()) {
			out_user_vertex_IDs->insert(make_pair(user_id, out_user_vertex_IDs->size()));
		}
		if (out_user_vertex_IDs->find(connected_user_id) == out_user_vertex_IDs->end()) {
			out_user_vertex_IDs->insert(make_pair(connected_user_id, out_user_vertex_IDs->size()));
		}
		if (out_user_tweets->find(user_id) == out_user_tweets->end()) {
			out_user_tweets->insert(make_pair(user_id, new vector<string>()));
		}

		boost::json::object tweet_JSON = boost::json::parse(bsoncxx::to_json(doc)).as_object();
		(*out_user_tweets)[user_id]->push_back(expand_tweet_text(tweet_JSON));
		//it will probably make some operations faster later if we assume that both user and connected_user have a vector in this map
		//even if we can't add anything ot the vectoe at this point
		if (out_user_tweets->find(connected_user_id) == out_user_tweets->end()) {
			out_user_tweets->insert(make_pair(connected_user_id, new vector<string>()));
		}

		igraph_sparsemat_entry(
			sm,
			(*out_user_vertex_IDs)[user_id],
			(*out_user_vertex_IDs)[connected_user_id],
			1	//f you add multiple entries in the same position, they will all be saved, and the resulting value is the sum of all entries in that position. (https://igraph.org/c/doc/igraph-Data-structures.html#igraph_sparsemat_entry)
		);
	}
	cout << "Users: " << out_user_vertex_IDs->size() << endl;
	cout << "Adding edges to graph..." << endl;

	igraph_sparsemat_t* smc = new igraph_sparsemat_t();
	igraph_sparsemat_compress(sm, smc);	//Converts a sparse matrix from triplet format to column-compressed format
	//optional step: a sparse matrix has one entry for each non-zero element, even if there are multiple entries in the same position. This function removes duplicates, so that each position has at most one entry. (https://igraph.org/c/doc/igraph-Data-structures.html#igraph_sparsemat_dupl)
	//if anything this should be done in the loop above in order to save RAM? but at what CPU cost?
	// doing it here is useless because we immediately delete it
	//igraph_sparsemat_dupl(smc); 

	igraph_t* g = new igraph_t();
	igraph_vector_t* weights = new igraph_vector_t();
	igraph_vector_init(weights, 0);
	igraph_sparse_weighted_adjacency(g, smc, IGRAPH_ADJ_DIRECTED, weights, IGRAPH_NO_LOOPS);

	igraph_sparsemat_destroy(sm);
	igraph_sparsemat_destroy(smc);

	igraph_vector_int_t* el = new igraph_vector_int_t();
	igraph_vector_int_init(el, 0);
	igraph_get_edgelist(g, el, 0);
	uint64_t n = igraph_ecount(g);

	uint64_t k = 0;
#if VERBOSE
		for (uint64_t i = 0, j = 0; i < n; i++, j += 2) {
			if (VECTOR(*weights)[i] > 4) {
				k++;
				printf("%" IGRAPH_PRId " --> %" IGRAPH_PRId ": %g\n",
					VECTOR(*el)[j], VECTOR(*el)[j + 1], VECTOR(*weights)[i]);
			}
		}
	}
#endif
	cout << k << endl;
	cout << "Edges: " << n << endl;
	cout << "Graph created." << endl;
	cout << "Operation took " << (chrono::high_resolution_clock::now() - start).count() / 1000 / 1000 / 1000 << " seconds." << endl;
	printMemoryUsage();
}

void community_detection(igraph_t* i_g) {
	auto start = chrono::high_resolution_clock::now();
	cout << "Community detection..." << endl;
	Graph* graph = new Graph(i_g);

	ModularityVertexPartition* partitioned = new ModularityVertexPartition(graph);

	Optimiser o;
	o.optimise_partition(partitioned);

	map<int, int> community_sizes;
	for (int i = 0; i < graph->vcount(); i++) {
		community_sizes[partitioned->membership(i)]++;
	}
	for (const auto& [community, size] : community_sizes) {
		if (size > 50) {
			cout << "Community " << community << " has " << size << " members" << endl;
		}
	}
	cout << "Number of communities: " << community_sizes.size() << endl;
	cout << "Community detection done." << endl;
	auto end = chrono::high_resolution_clock::now();
	auto duration = chrono::duration_cast<chrono::seconds>(end - start);
	cout << "Community detection took " << duration.count() << " seconds" << endl;
	printMemoryUsage();
}

int main()
{
	//prevent igraph from killing everything on error. igraph fucntions will return error codes instead
	igraph_set_error_handler(igraph_error_handler_ignore);
	//prevent windows from killing everything on error. windows errors will throw regular exceptions instead
	_set_se_translator(sehTranslator);
	//allows the use of vertex and edge attributes
	//this is needed because the igraph attribute features are more intended to be used with Python and R than C++
	igraph_set_attribute_table(&igraph_cattribute_table);

	auto start = chrono::high_resolution_clock::now();
	try {
		cout << "Establishing database connection..." << endl;
		mongocxx::instance inst{};
		mongocxx::client conn{ mongocxx::uri{"mongodb://JamIs:morticiaetpollito@118.138.244.29:27017/"} };
		auto collection = conn["Tw_Covid_DB"]["tweets"];

		// Build the query
		auto query = bsoncxx::builder::basic::make_document(
			bsoncxx::builder::basic::kvp("datetime",
				bsoncxx::builder::basic::make_document(
					bsoncxx::builder::basic::kvp("$gte", createBsonDateFromString("2020-03-01 00:00:00")),
					bsoncxx::builder::basic::kvp("$lt", createBsonDateFromString("2020-03-15 00:00:00"))
				)
			),
			bsoncxx::builder::basic::kvp("lang", "en"),
			bsoncxx::builder::basic::kvp("connection_type", bsoncxx::builder::basic::make_document(bsoncxx::builder::basic::kvp("$exists", true))),
			bsoncxx::builder::basic::kvp("connection_type", bsoncxx::builder::basic::make_document(bsoncxx::builder::basic::kvp("$ne", bsoncxx::types::b_null{}))),
			bsoncxx::builder::basic::kvp("connected_user", bsoncxx::builder::basic::make_document(bsoncxx::builder::basic::kvp("$ne", bsoncxx::types::b_null{})))
		);

		//set a large default, then try and get the actual number from the DB - this call seems to time out a lot, which is why we do it this way
		int64_t count = 6000000;
		try {
			cout << "Getting query size to pre-size graph..." << endl;
			count = collection.count_documents(query.view());
			cout << "Query projected size: " << count << endl;
		} catch (const mongocxx::exception& e) {
			std::cerr << "document count error: " << e.what() << std::endl;
			cout << "Using default query size of " << count << endl;
		}

		// Execute the query
		cout << "Running query..." << endl;
		auto cursor = collection.find(query.view());
		cout << "Operation took " << (chrono::high_resolution_clock::now() - start).count() / 1000 / 1000 / 1000 << " seconds." << endl;
		printMemoryUsage();

		igraph_t* g = new igraph_t();
		ptr<map<string, int>> user_vertex_IDs = _ptr<map<string, int>>();
		ptr<map<string, ptr<vector<string>>>> user_tweets = _ptr<map<string, ptr<vector<string>>>>();
		build_graph(&cursor, count, g, user_vertex_IDs, user_tweets);

		//iteratively_prune_graph(g);

		community_detection(g);

	} catch (const exception& e) {
		// Handle standard exceptions
		cerr << "Standard exception: " << e.what() << '\n';
	} catch (...) {
		// Catch-all handler: can be used to catch all types of exceptions
		cerr << "Unknown failure occurred." << '\n';
	}

	
}


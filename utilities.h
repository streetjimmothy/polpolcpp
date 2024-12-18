#pragma once
#ifndef UTIL_H
#define UTIL_H

#include <iostream>
#include <memory>
#include <vector>
#include <utility> // for std::pair
#include <queue>
#include <algorithm>
#include <functional>
#include <type_traits>
#include <climits>

#include <igraph/igraph.h>
#include <libleidenalg/GraphHelper.h>
#include <libleidenalg/Optimiser.h>
#include <libleidenalg/ModularityVertexPartition.h>

#include <chrono>
#include <windows.h>
#include <psapi.h>

typedef unsigned int uint;
typedef unsigned char uchar;
typedef unsigned char ubyte;
#define VERBOSE false;

template <typename T>
using ptr = std::shared_ptr<T>;

// Wrapper function for std::make_shared
template <typename T, typename... Args>
ptr<T> make_ptr(Args&&... args) {
    return std::make_shared<T>(std::forward<Args>(args)...);
}
template <typename T, typename... Args>
ptr<T> _ptr(Args&&... args) {
    return std::make_shared<T>(std::forward<Args>(args)...);
}


void print_graph(const igraph_t* graph);
void print_matrix(const igraph_matrix_t* matrix);
void sehTranslator(unsigned int code, EXCEPTION_POINTERS* pExp);

//cartesian product, same as itertools.product in python
template <typename T1, typename T2>
ptr<std::vector<ptr<std::pair<T1, T2>>>> cartesian_product(const ptr<std::vector<T1>> vec1, const ptr<std::vector<T2>> vec2) {
    ptr<std::vector<ptr<std::pair<T1, T2>>>> product = _ptr<std::vector<ptr<std::pair<T1, T2>>>>();
    for (const auto& item1 : *vec1) {
        for (const auto& item2 : *vec2) {
            product->emplace_back(_ptr<std::pair<T1, T2>>(item1, item2));
        }
    }
    return product;
};



#endif 
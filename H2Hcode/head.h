/*
 * head.h
 *
 *  Created on: 22 Dec 2020
 *      Author: zhangmengxuan
 */

#ifndef HEAD_H_
#define HEAD_H_

#include <stdio.h>
#include <math.h>
#include <vector>
#include <map>
#include <set>
#include<boost/algorithm/string/split.hpp>
#include<boost/algorithm/string/classification.hpp>
#include <boost/heap/fibonacci_heap.hpp>
#include<iostream>
#include<fstream>
#include<math.h>
#include <chrono>
#include <unordered_map>
#include <unordered_set>
#include <boost/thread/thread.hpp>

#include <functional>
//#include <bits/stdc++.h>
#include<vector>
#include<iostream>
#include <utility>


#define INF 999999999
using namespace std;
using namespace boost;

struct Nei
{
    int posID, neiID, weight, count;
};

struct Node{//tree node
	vector<pair<int,pair<int,int>>> vert;//neighID/weight/count
	vector<pair<int,Nei>> neighInf;//posID,neighID,weight,count(for shortcut information maintenance)
	vector<int> pos, pos2;
	vector<int> dis, cnt;//the distance value and corresponding count number
	//vector<set<int>> FromNode;
	set<int> changedPos;
	vector<bool> FN;//another succint way of FromNode
	set<int> DisRe;
	vector<int> ch;
	int height, hdepth;//hdepty is the deepest node that a vertex still exists
	int pa;//parent
	int uniqueVertex;
	vector<int> piv;//pivot vetex, used in path retrieval
	Node(){
		vert.clear();
		neighInf.clear();
		pos.clear();
		dis.clear();
		cnt.clear();
		ch.clear();
		pa = -1;
		uniqueVertex = -1;
		height = 0;
		hdepth = 0;
		changedPos.clear();
		FN.clear();
		DisRe.clear();
		piv.clear();
	}
};

class Graph{
public:
	int nodenum;
	int edgenum;

	int eNum;//(a,b) is a edge with a<b
	vector<pair<int,int>> Edge;//edgeID->(a,b) with a<b
	vector<set<int>> E1;

	//bidirectional graph
	vector<vector<pair<int,int>>> Neighbor;
	vector<pair<double,double>> GraphLocation;//location used in A* algorithm

	//vertex order & its inverted list
	vector<int> NodeOrder;
	vector<int> vNodeOrder;
	
	//ReadGraph
	//all node IDs start from 0; files in the form of (ID1,ID2,weight)
	void ReadGraph(const string& filename);
    void readGraph(const string &filename);
    void readGraph1(const string &filename);
	//CH without pruning
	void CHcons();//vertex order is unknown beforehand
	vector<int> DD,DD2;
	vector<map<int,pair<int,int>>> E;
	void insertE(int u,int v,int w);
	void deleteE(int u,int v);
	vector<vector<pair<int,pair<int,int>>>> NeighborCon;//for query processing


	//H2H
	void H2Hcon();//vertex order is unknown beforehand
	vector<int> rank;
	vector<Node> Tree;
	int heightMax;
	void makeTree();
	int match(int x,vector<pair<int,pair<int,int>>> &vert);
	vector<vector<int>> VidtoTNid;//one vertex exist in those tree nodes (nodeID--->tree node rank)
	vector<int> eulerSeq;//prepare for the LCA calculation
	vector<int> toRMQ;
	vector<vector<int>> RMQIndex;
	void makeRMQDFS(int p, int height);
	void makeRMQ();
	int LCAQuery(int _p, int _q);
	void makeIndex();//make index
	void makeIndexDFS(int p, vector<int> &list);

    int queryH2H(int ID1, int ID2);
    int DijkstraDis(int ID1, int ID2);

    void writeOrder(const string &basicString);
};

namespace benchmark {

#define NULLINDEX 0xFFFFFFFF

template<int log_k, typename k_t, typename id_t>
class heap {

public:

	// Expose types.
	typedef k_t key_t;
	typedef id_t node_t;

	// Some constants regarding the elements.
	//static const node_t NULLINDEX = 0xFFFFFFFF;
	static const node_t k = 1 << log_k;

	// A struct defining a heap element.
	struct element_t {
		key_t key;
		node_t element;
		element_t() : key(0), element(0) {}
		element_t(const key_t k, const node_t e) : key(k), element(e) {}
	};


public:

	// Constructor of the heap.
	heap(node_t n) : n(0), max_n(n), elements(n), position(n, NULLINDEX) {
	}

	heap() {

	}

	// Size of the heap.
	inline node_t size() const {
		return n;
	}

	// Heap empty?
	inline bool empty() const {
		return size() == 0;
	}

	// Extract min element.
	inline void extract_min(node_t &element, key_t &key) {
		assert(!empty());

		element_t &front = elements[0];

		// Assign element and key.
		element = front.element;
		key = front.key;

		// Replace elements[0] by last element.
		position[element] = NULLINDEX;
		--n;
		if (!empty()) {
			front = elements[n];
			position[front.element] = 0;
			sift_down(0);
		}
	}

	inline key_t top() {
		assert(!empty());

		element_t &front = elements[0];

		return front.key;

	}

	inline node_t top_value() {

		assert(!empty());

		element_t &front = elements[0];

		return front.element;
	}

	// Update an element of the heap.
	inline void update(const node_t element, const key_t key) {
		if (position[element] == NULLINDEX) {
			element_t &back = elements[n];
			back.key = key;
			back.element = element;
			position[element] = n;
			sift_up(n++);
		} else {
			node_t el_pos = position[element];
			element_t &el = elements[el_pos];
			if (key > el.key) {
				el.key = key;
				sift_down(el_pos);
			} else {
				el.key = key;
				sift_up(el_pos);
			}
		}
	}


	// Clear the heap.
	inline void clear() {
		for (node_t i = 0; i < n; ++i) {
			position[elements[i].element] = NULLINDEX;
		}
		n = 0;
	}

	// Cheaper clear.
	inline void clear(node_t v) {
		position[v] = NULLINDEX;
	}

	inline void clear_n() {
		n = 0;
	}


	// Test whether an element is contained in the heap.
	inline bool contains(const node_t element) const {
		return position[element] != NULLINDEX;
	}


protected:

	// Sift up an element.
	inline void sift_up(node_t i) {
		assert(i < n);
		node_t cur_i = i;
		while (cur_i > 0) {
			node_t parent_i = (cur_i-1) >> log_k;
			if (elements[parent_i].key > elements[cur_i].key)
				swap(cur_i, parent_i);
			else
				break;
			cur_i = parent_i;
		}
	}

	// Sift down an element.
	inline void sift_down(node_t i) {
		assert(i < n);

		while (true) {
			node_t min_ind = i;
			key_t min_key = elements[i].key;

			node_t child_ind_l = (i << log_k) + 1;
			node_t child_ind_u = std::min(child_ind_l + k, n);

			for (node_t j = child_ind_l; j < child_ind_u; ++j) {
				if (elements[j].key < min_key) {
					min_ind = j;
					min_key = elements[j].key;
				}
			}

			// Exchange?
			if (min_ind != i) {
				swap(i, min_ind);
				i = min_ind;
			} else {
				break;
			}
		}
	}

	// Swap two elements in the heap.
	inline void swap(const node_t i, const node_t j) {
		element_t &el_i = elements[i];
		element_t &el_j = elements[j];

		// Exchange positions
		position[el_i.element] = j;
		position[el_j.element] = i;

		// Exchange elements
		element_t temp = el_i;
		el_i = el_j;
		el_j = temp;
	}



private:

	// Number of elements in the heap.
	node_t n;

	// Number of maximal elements.
	node_t max_n;

	// Array of length heap_elements.
	vector<element_t> elements;

	// An array of positions for all elements.
	vector<node_t> position;
};
}

#endif /* HEAD_H_ */

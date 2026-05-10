//
// Created by yehong.xu on 27/12/21.
//

/*
Copyright (c) 2017 Theodoros Chondrogiannis
*/

#include "kspwlo.h"

Path next_spwlo_bounds(RoadNetwork *rN, NodeId source, NodeId target, double theta, unordered_map<Edge, vector<int>,boost::hash<Edge>> &resEdges, vector<Path> &resPaths, vector<int> &bounds);

/*
 *
 *	multipass(RoadNetwork*, NodeId, NodeId, int, double)
 *	-----
 *	Implementation of the MultiPass KSP.
 * 	Calls function next_spwlo_bounds
 *
 */

vector<Path> multipass(RoadNetwork *rN, NodeId source, NodeId target, unsigned int k, double theta) {
    int count = 0;
    Edge edge;
    unordered_map<Edge, vector<int>,boost::hash<Edge>> resEdges;
    unordered_map<Edge, vector<int>,boost::hash<Edge>>::iterator iterE;

    vector<Path> resPaths;
    pair<Path,vector<int>> resDijkstra= dijkstra_path_and_bounds(rN,source,target);

    Path resNext = resDijkstra.first;

    resPaths.push_back(resNext);

    if(k==1)
        return resPaths;

    for(unsigned int i=1;i<k;i++) {

        for(unsigned int j=0; j < resNext.nodes.size()-1; j++) {
            edge = make_pair(resNext.nodes[j],resNext.nodes[j+1]);
            if ((iterE = resEdges.find(edge)) == resEdges.end())
                resEdges.insert(make_pair(edge, vector<int>(1, count)));
            else
                iterE->second.push_back(count);
        }
        count++;

        resNext = next_spwlo_bounds(rN, source, target, theta, resEdges, resPaths, resDijkstra.second);

        if(resNext.length == -1)
            break;

        resPaths.push_back(resNext);
    }

    return resPaths;
}

/*
	next_spwlo_bounds(RoadNetwork, NodeId, NodeId, double, unordered_map<Edge, vector<int>,boost::hash<Edge>>, vector<Path>, vector<int>)
	-----
	This is the internal function called by MultiPass to produce the shortest
	alternative to the provided set of paths.
*/

Path next_spwlo_bounds(RoadNetwork *rN, NodeId source, NodeId target, double theta, unordered_map<Edge, vector<int>,boost::hash<Edge>> &resEdges, vector<Path> &resPaths, vector<int> &bounds) {
    Path resPath;
    resPath.length = -1;
    PriorityQueueAS2 Q;
    SkylineContainer skyline;
    int newLength = 0;
    vector<double> newOverlap;
    EdgeList::iterator iterAdj;
    unordered_map<Edge, vector<int>,boost::hash<Edge>>::iterator iterE;
    Edge edge;
    bool check = true;
    vector<OlLabel*> allCreatedLabels;

    newOverlap.resize(resPaths.size(), 0);
    int newLowerBound = bounds[source];
    Q.push(new OlLabel(source, newLength, newLowerBound, newOverlap, -1));

    while (!Q.empty()) {
        auto *curLabel = static_cast<OlLabel*> (Q.top());
        Q.pop();

        // Found target.
        if (curLabel->node_id == target) {
            OlLabel *tempLabel = curLabel;

            while(tempLabel != nullptr) {
                resPath.nodes.push_back(tempLabel->node_id);
                tempLabel = static_cast<OlLabel*> (tempLabel->previous);
            }
            reverse(resPath.nodes.begin(),resPath.nodes.end());
            resPath.length = curLabel->length;
            break;
        }
        if(skyline.dominates(curLabel))
            continue;
        skyline.insert(curLabel);

        // Expand search. For each outgoing edge.
        for(iterAdj = rN->adjListOut[curLabel->node_id].begin(); iterAdj != rN->adjListOut[curLabel->node_id].end(); iterAdj++) {

            if(curLabel->previous !=nullptr && curLabel->previous->node_id == iterAdj->first)
                continue;

            newLength = curLabel->length + iterAdj->second;
            newOverlap = curLabel->overlapList;
            newLowerBound = newLength + bounds[iterAdj->first];
            OlLabel* newPrevious = curLabel;
            edge = make_pair(curLabel->node_id,iterAdj->first);
            check = true;

            if ((iterE = resEdges.find(edge)) != resEdges.end()) {
                for(int j : iterE->second) {
                    newOverlap[j] += iterAdj->second;
                    if (newOverlap[j]/resPaths[j].length > theta) {
                        check = false;
                        break;
                    }
                }
            }

            if (check) {
                auto *label = new OlLabel(iterAdj->first, newLength, newLowerBound, newOverlap, -1,newPrevious);
                Q.push(label);
                allCreatedLabels.push_back(label);
            }
        }
    }

    for(auto & allCreatedLabel : allCreatedLabels)
        delete allCreatedLabel;

    return resPath;
}
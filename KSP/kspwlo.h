//
// Created by yehong.xu on 27/12/21.
//

#ifndef TRAFFIC_ASSIGNMENT_KSPWLO_H
#define TRAFFIC_ASSIGNMENT_KSPWLO_H

/*
Copyright (c) 2017 Theodoros Chondrogiannis
*/


#include <iostream>
#include <vector>
#include <unordered_map>

#include "../model/Graph.h"
#include "../tools/tools.h"
#include "../traffic/Traffic.h"

using namespace std;

// Class for the Label which contains an overlap vector
class OlLabel : public Label
{
public:
    //float ratio;
    vector<double> overlapList;
    unsigned int overlapForK;

    OlLabel(NodeId node_id, int length, vector<double> &overlapList, int overlapForK) : Label(node_id, length)
    {
        this->overlapList = overlapList;
        this->overlapForK = overlapForK;
        this->previous = nullptr;
    };

    OlLabel(NodeId node_id, int length, vector<double> &overlapList, int overlapForK, OlLabel *previous) : Label(
            node_id, length, previous)
    {
        this->overlapList = overlapList;
        this->overlapForK = overlapForK;
    };

    OlLabel(NodeId node_id, int length, int fDist, vector<double> &overlapList, int overlapForK) : Label(node_id,
                                                                                                         length, fDist)
    {
        this->overlapList = overlapList;
        this->overlapForK = overlapForK;
        this->previous = NULL;
    };

    OlLabel(NodeId node_id, int length, int fDist, vector<double> &overlapList, int overlapForK, OlLabel *previous)
            : Label(node_id, length, fDist, previous)
    {
        this->overlapList = overlapList;
        this->overlapForK = overlapForK;
    };

};

/*
 *	SkylineContainer class stores the skyline in each node.
 * 	Responsible for executing the domination check.
 */

class SkylineContainer
{
public:
    unordered_map<int, vector<OlLabel *>> container; // Consider replacing the unordered_map with a vector
    void insert(OlLabel *);

    bool contains(int);

    vector<OlLabel *> get(int);

    bool dominates(OlLabel *);

    unsigned long contentsSize();
};

class AstarComparator2
{
    bool reverse;
public:
    explicit AstarComparator2(const bool &revparam = false)
    {
        reverse = revparam;
    }

    bool operator()(const OlLabel *lhs, const OlLabel *rhs) const
    {
        if (lhs->lowerBound > rhs->lowerBound)
            return true;
        else if (lhs->lowerBound < rhs->lowerBound)
            return false;
        else
        {
            float minLhs = 1;
            for (double i : lhs->overlapList)
            {
                if (i < minLhs)
                    minLhs = i;
            }

            float minRhs = 1;
            for (double i : rhs->overlapList)
            {
                if (i < minRhs)
                    minRhs = i;
            }

            return minLhs > minRhs;
        }
    }
};

typedef priority_queue<OlLabel *, std::vector<OlLabel *>, AstarComparator2> PriorityQueueAS2;

// Declarations of exact algorithms
vector<Path> onepass(RoadNetwork *rN, NodeId source, NodeId target, unsigned int k, double theta);

vector<Path> multipass(RoadNetwork *rN, NodeId source, NodeId target, unsigned int k, double theta);

// Declarations of performance-oriented heuristic algorithms
vector<Path> svp_plus(RoadNetwork *rN, NodeId source, NodeId target, unsigned int k, double theta);

vector<Path> onepass_plus(RoadNetwork *rN, NodeId source, NodeId target, unsigned int k, double theta);

vector<Path> esx(RoadNetwork *rN, NodeId source, NodeId target, unsigned int k, double theta);

// Declarations of completeness-oriented heuristic algorithms
pair<vector<Path>, double> esx_complete(RoadNetwork *rN, NodeId source, NodeId target, unsigned int k, double theta);

pair<vector<Path>, double>
svp_plus_complete(RoadNetwork *rN, NodeId source, NodeId target, unsigned int k, double theta);

// Supporting function for the completeness-oriented heuristic algorithms
pair<vector<Path>, double>
completeness_function(RoadNetwork *rN, vector<Path> inputPaths, unsigned int k, double theta);

#endif //TRAFFIC_ASSIGNMENT_KSPWLO_H

//
// Created by yehong.xu on 27/12/21.
//

/*
Copyright (c) 2017 Theodoros Chondrogiannis
*/

#include "kspwlo.h"

/*
 *
 *	onepass_plus(RoadNetwork*, NodeId, NodeId, int, double)
 *	-----
 *	Implementation of the OnePass+ KSP.
 *
 */

vector<Path> onepass_plus(RoadNetwork *rN, NodeId source, NodeId target, unsigned int k, double theta)
{
    vector<Path> resPaths;

    unsigned int count = 0;
    NodeId resid;
    PriorityQueueAS queue;
    int newLength = 0;
    int newLowerBound = 0;
    vector<double> newOverlap;
    EdgeList::iterator iterAdj;
    Edge edge;
    bool check;
    SkylineContainer skyline;

    /* DEBUG */
    vector<int> visitsNo(rN->numNodes, 0);

    unordered_map<Edge, vector<int>, boost::hash<Edge>> resEdges;
    unordered_map<Edge, vector<int>, boost::hash<Edge>>::iterator iterE;
    vector<OlLabel *> allCreatedLabels;

    pair<Path, vector<int>> resDijkstra = dijkstra_path_and_bounds(rN, source, target);

    Path resNext = resDijkstra.first;

    resPaths.push_back(resNext);

    // Only the shortest path is requested
    if (k == 1)
        return resPaths;

    for (unsigned int j = 0; j < resNext.nodes.size() - 1; j++)
    {
        edge = make_pair(resNext.nodes[j], resNext.nodes[j + 1]);
        if ((iterE = resEdges.find(edge)) == resEdges.end())
            resEdges.insert(make_pair(edge, vector<int>(1, count)));
        else
            iterE->second.push_back(count);
    }
    count++;

    newOverlap.resize(k, 0);
    queue.push(new OlLabel(source, newLength, resDijkstra.second[source], newOverlap, -1));

    while (!queue.empty())
    {
        auto *curLabel = static_cast<OlLabel *> (queue.top());
        queue.pop();

        /* DEBUG */
        visitsNo[curLabel->node_id]++;

        if (curLabel->overlapForK < count - 1)
        {
            check = true;

            OlLabel *tempLabel = curLabel;
            Path tempPath;
            while (tempLabel != nullptr)
            {
                tempPath.nodes.push_back(tempLabel->node_id);
                tempLabel = static_cast<OlLabel *> (tempLabel->previous);
            }

            reverse(tempPath.nodes.begin(), tempPath.nodes.end());

            for (unsigned int j = 0; j < tempPath.nodes.size() - 1 && check; j++)
            {
                edge = make_pair(tempPath.nodes[j], tempPath.nodes[j + 1]);
                if ((iterE = resEdges.find(edge)) != resEdges.end())
                {
                    for (int i : iterE->second)
                    {
                        resid = i;

                        if (resid > curLabel->overlapForK && resid < count)
                        {
                            curLabel->overlapList[resid] += rN->getEdgeWeight(edge.first, edge.second);
                            if (curLabel->overlapList[resid] / resPaths[resid].length > theta)
                            {
                                check = false;
                                break;
                            }
                        }
                    }
                }
            }
            curLabel->overlapForK = count - 1;
            if (!check)
                continue;
        }

        if (curLabel->node_id == target)
        { // Found target.

            OlLabel *tempLabel = curLabel;
            Path tempPath;

            while (tempLabel != nullptr)
            {
                tempPath.nodes.push_back(tempLabel->node_id);
                tempLabel = static_cast<OlLabel *> (tempLabel->previous);
            }
            reverse(tempPath.nodes.begin(), tempPath.nodes.end());
            tempPath.length = curLabel->length;
            resPaths.push_back(tempPath);

            if (count == k - 1)
                break;

            for (unsigned int j = 0; j < tempPath.nodes.size() - 1; j++)
            {
                edge = make_pair(tempPath.nodes[j], tempPath.nodes[j + 1]);
                if ((iterE = resEdges.find(edge)) == resEdges.end())
                    resEdges.insert(make_pair(edge, vector<int>(1, count)));
                else
                    iterE->second.push_back(count);
            }

            count++;
        } else
        { // Expand Search
            if (skyline.dominates(curLabel))
                continue;
            skyline.insert(curLabel);
            // For each outgoing edge
            for (iterAdj = rN->adjListOut[curLabel->node_id].begin();
                 iterAdj != rN->adjListOut[curLabel->node_id].end(); iterAdj++)
            {
                // Avoid cycles.
                bool containsLoop = false;
                OlLabel *tempLabel = curLabel;
                while (tempLabel != nullptr)
                {
                    if (tempLabel->node_id == iterAdj->first)
                    {
                        containsLoop = true;
                        break;
                    }
                    tempLabel = static_cast<OlLabel *> (tempLabel->previous);
                }
                if (!containsLoop)
                {
                    newLength = curLabel->length + iterAdj->second;;
                    newOverlap = curLabel->overlapList;
                    newLowerBound = newLength + resDijkstra.second[iterAdj->first];
                    OlLabel *newPrevious = curLabel;
                    edge = make_pair(curLabel->node_id, iterAdj->first);
                    check = true;

                    if ((iterE = resEdges.find(edge)) != resEdges.end())
                    {
                        for (int j : iterE->second)
                        {
                            newOverlap[j] += iterAdj->second;
                            if (newOverlap[j] / resPaths[j].length > theta)
                            {
                                check = false;
                                break;
                            }
                        }
                    }

                    if (check)
                    {
                        auto *label = new OlLabel(iterAdj->first, newLength, newLowerBound, newOverlap, (count - 1),
                                                     newPrevious);
                        queue.push(label);
                        allCreatedLabels.push_back(label);
                    }
                }
            }
        }
    }

    for (auto & allCreatedLabel : allCreatedLabels)
        delete allCreatedLabel;

    return resPaths;
}

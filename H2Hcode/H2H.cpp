/*
 * Labelcon.cpp
 *
 *  Created on: 22 Dec 2020
 *      Author: zhangmengxuan
 */
#include "head.h"

void Graph::H2Hcon()
{
    CHcons();
    makeTree();
    makeIndex();
}

vector<int> _DD, _DD2; // exclusively used to define vertices' orders in set<DegComp>

struct DegComp
{
    int x;

    explicit DegComp(int _x)
    {
        x = _x;
    }

    bool operator<(const DegComp d) const
    {
        if (_DD[x] != _DD[d.x])
            return _DD[x] < _DD[d.x];
        if (_DD2[x] != _DD2[x])
            return _DD2[x] < _DD2[d.x];
        return x < d.x;
    }
};

void Graph::CHcons()
{
    //initialize E
    map<int, pair<int, int>> m;
    E.assign(nodenum, m); // contracted graph vector<map<int,pair<int,int>>> E;
    for (int i = 0; i < Neighbor.size(); i++)
    {
        for (int j = 0; j < Neighbor[i].size(); j++)
            // E[u].insert(u, v, weight, count )
            E[i].insert(make_pair(Neighbor[i][j].first, make_pair(Neighbor[i][j].second, 1)));
    }

    _DD.assign(nodenum, 0);
    _DD2.assign(nodenum, 0);
    DD.assign(nodenum, 0);
    DD2.assign(nodenum, 0);

    set<DegComp> Deg;
    int degree;
    for (int i = 0; i < nodenum; i++)
    {
        degree = Neighbor[i].size();
        if (degree != 0)
        {
            _DD[i] = degree;
            _DD2[i] = degree;
            DD[i] = degree;
            DD2[i] = degree;
            Deg.insert(DegComp(i));
        }
    }

    vector<bool> exist;
    exist.assign(nodenum, true);
    vector<bool> change;
    change.assign(nodenum, false); // neighbour of affected vertex

    vector<pair<int, pair<int, int>>> vect;
    NeighborCon.assign(nodenum, vect); //NeighborCon.clear();

    cout << "Begin to contract" << endl;
    int count = 0;

    while (!Deg.empty())
    {
        if (count % 10000 == 0) cout << "count " << count << endl;

        count += 1;
        int x = (*Deg.begin()).x;

        while (true)
        {
            // update degree of affected vertices (neighbours of the contracted vertex) if it is the first in set
            if (change[x])
            {
                Deg.erase(DegComp(x));
                _DD[x] = DD[x];
                _DD2[x] = DD2[x];
                Deg.insert(DegComp(x));
                change[x] = false;
                x = (*Deg.begin()).x;
            } else
                break;
        }

        vNodeOrder.push_back(x);
        Deg.erase(Deg.begin());
        exist[x] = false;

        vector<pair<int, pair<int, int>>> Neigh; //Neigh.clear(); x's neighbours

        for (auto & it : E[x])
        {
            if (exist[it.first])
            {
                Neigh.emplace_back(it);
            }
        }
        NeighborCon[x].assign(Neigh.begin(), Neigh.end());

        for (auto & i : Neigh)
        {
            int y = i.first;
            deleteE(x, y);
            change[y] = true;
        }

        for (int i = 0; i < Neigh.size(); i++)
        {
            for (int j = i + 1; j < Neigh.size(); j++)
            {
                insertE(Neigh[i].first, Neigh[j].first, Neigh[i].second.first + Neigh[j].second.first);
                change[Neigh[i].first] = true;
                change[Neigh[j].first] = true;
            }
        }
    }

    NodeOrder.assign(nodenum, -1); // idx: vid; value: rank (increasingly important)
    for (int k = 0; k < vNodeOrder.size(); k++)
    {
        NodeOrder[vNodeOrder[k]] = k;
    }
    cout << "Finish Contract" << endl;
}

void Graph::deleteE(int u, int v)
{
    if (E[u].find(v) != E[u].end())
    {
        E[u].erase(E[u].find(v));
        DD[u]--;
    }

    if (E[v].find(u) != E[v].end())
    {
        E[v].erase(E[v].find(u));
        DD[v]--;
    }
}

void Graph::insertE(int u, int v, int w)
{
    if (E[u].find(v) == E[u].end())
    {
        E[u].insert(make_pair(v, make_pair(w, 1)));
        DD[u]++;
        DD2[u]++;
    } else
    {
        if (E[u][v].first > w)
            E[u][v] = make_pair(w, 1);
        else if (E[u][v].first == w)
            E[u][v].second += 1;
    }

    if (E[v].find(u) == E[v].end())
    {
        E[v].insert(make_pair(u, make_pair(w, 1)));
        DD[v]++;
        DD2[v]++;
    } else
    {
        if (E[v][u].first > w)
            E[v][u] = make_pair(w, 1);
        else if (E[v][u].first == w)
            E[v][u].second += 1;
    }
}

int Graph::match(int x, vector<pair<int, pair<int, int>>> &vert)
{
    int nearest = vert[0].first;
    for (int i = 1; i < vert.size(); i++)
    {
        if (rank[vert[i].first] > rank[nearest])
            nearest = vert[i].first;
    }
    int p = rank[nearest];
    return p;
}

void Graph::makeTree()
{
    vector<int> vecemp; //vecemp.clear();
    VidtoTNid.assign(nodenum, vecemp);

    rank.assign(nodenum, 0);
    //Tree.clear();
    int len = vNodeOrder.size() - 1;
    heightMax = 0;

    Node rootn; // most important vertex
    int x = vNodeOrder[len];
    //cout<<"len "<<len<<" , ID "<<x<<endl;
    while (x == -1)
    {//to skip those vertices whose ID is -1
        len--;
        x = vNodeOrder[len];
        //cout<<"len "<<len<<" , ID "<<x<<endl;
    }
    rootn.vert = NeighborCon[x];
    rootn.uniqueVertex = x;
    rootn.pa = -1;
    rootn.height = 1;
    rank[x] = 0;
    Tree.push_back(rootn);
    len--;

    int nn;
    for (; len >= 0; len--)
    {
        int x = vNodeOrder[len];
        Node nod;
        nod.vert = NeighborCon[x];
        nod.uniqueVertex = x;
        int pa = match(x, NeighborCon[x]);
        Tree[pa].ch.push_back(Tree.size());
        nod.pa = pa;
        nod.height = Tree[pa].height + 1;

        nod.hdepth = Tree[pa].height + 1;
        for (auto & i : NeighborCon[x])
        {
            nn = i.first;
            VidtoTNid[nn].push_back(Tree.size());
            if (Tree[rank[nn]].hdepth < Tree[pa].height + 1)
                Tree[rank[nn]].hdepth = Tree[pa].height + 1;
        }
        if (nod.height > heightMax) heightMax = nod.height;
        rank[x] = Tree.size();
        Tree.push_back(nod);
        //cout<<"len "<<len<<" , ID "<<x<<endl;
    }
}

void Graph::makeRMQDFS(int p, int height)
{
    toRMQ[p] = eulerSeq.size();
    eulerSeq.push_back(p);
    for (int i = 0; i < Tree[p].ch.size(); i++)
    {
        makeRMQDFS(Tree[p].ch[i], height + 1);
        eulerSeq.push_back(p);
    }
}

void Graph::makeRMQ()
{
    //eulerSeq.clear();
    toRMQ.assign(nodenum, 0);
    //RMQIndex.clear();
    makeRMQDFS(0, 1);
    RMQIndex.push_back(eulerSeq);

    int m = eulerSeq.size();
    for (int i = 2, k = 1; i < m; i = i * 2, k++)
    {
        vector<int> tmp;
        //tmp.clear();
        tmp.assign(m, 0);
        for (int j = 0; j < m - i; j++)
        {
            int x = RMQIndex[k - 1][j], y = RMQIndex[k - 1][j + i / 2];
            if (Tree[x].height < Tree[y].height)
                tmp[j] = x;
            else tmp[j] = y;
        }
        RMQIndex.push_back(tmp);
    }
}

int Graph::LCAQuery(int _p, int _q)
{
    int p = toRMQ[_p], q = toRMQ[_q];
    if (p > q)
    {
        int x = p;
        p = q;
        q = x;
    }
    int len = q - p + 1;
    int i = 1, k = 0;
    while (i * 2 < len)
    {
        i *= 2;
        k++;
    }
    q = q - i + 1;
    if (Tree[RMQIndex[k][p]].height < Tree[RMQIndex[k][q]].height)
        return RMQIndex[k][p];
    else return RMQIndex[k][q];
}

void Graph::makeIndex()
{
    makeRMQ();

    //initialize
    vector<int> list; //list.clear();
    list.push_back(Tree[0].uniqueVertex);
    Tree[0].pos.clear();
    Tree[0].pos.push_back(0);

    for (int i : Tree[0].ch)
    {
        makeIndexDFS(i, list);
    }

}

void Graph::makeIndexDFS(int p, vector<int> &list)
{
    //initialize
    int NeiNum = Tree[p].vert.size();
    Tree[p].pos.assign(NeiNum + 1, 0);
    Tree[p].dis.assign(list.size(), INF);
    Tree[p].cnt.assign(list.size(), 0);
    Tree[p].FN.assign(list.size(), true);

    //pos
    //map<int,Nei> Nmap; Nmap.clear();//shortcut infor ordered by the pos ID
    for (int i = 0; i < NeiNum; i++)
    {
        for (int j = 0; j < list.size(); j++)
        {
            if (Tree[p].vert[i].first == list[j])
            {
                Tree[p].pos[i] = j;
                Tree[p].dis[j] = Tree[p].vert[i].second.first;
                Tree[p].cnt[j] = 1;
                break;
            }
        }
    }
    Tree[p].pos[NeiNum] = list.size();


    //dis
    for (int i = 0; i < NeiNum; i++)
    {
        int x = Tree[p].vert[i].first;
        int disvb = Tree[p].vert[i].second.first;
        int k = Tree[p].pos[i];//the kth ancestor is x

        for (int j = 0; j < list.size(); j++)
        {
            int y = list[j];//the jth ancestor is y

            int z;//the distance from x to y
            if (k != j)
            {
                if (k < j)
                    z = Tree[rank[y]].dis[k];
                else if (k > j)
                    z = Tree[rank[x]].dis[j];

                if (Tree[p].dis[j] > z + disvb)
                {
                    Tree[p].dis[j] = z + disvb;
                    Tree[p].FN[j] = false;
                    Tree[p].cnt[j] = 1;
                } else if (Tree[p].dis[j] == z + disvb)
                {
                    Tree[p].cnt[j] += 1;
                }
            }
        }
    }

    //nested loop
    list.push_back(Tree[p].uniqueVertex);
    for (int i : Tree[p].ch)
    {
        makeIndexDFS(i, list);
    }
    list.pop_back();
}

void Graph::readGraph1(const string &filename) {
    ifstream infile(filename);
    int roadId, lnode, rnode;
    int line_cnt = 0, w = 0, c = 0;

    while (infile >> roadId >> lnode >> rnode >> w >> c) {
        line_cnt += 1;
        if (line_cnt == 1) {
            nodenum = lnode + 1;
            edgenum = rnode;
            Neighbor.resize(nodenum);
        } else {
            Neighbor[lnode].push_back(make_pair(rnode, w));
        }
    }
    infile.close();
}

void Graph::readGraph(const string &filename) {
    ifstream IF(filename);
    if (!IF) {
        cout << "Cannot open Map " << filename << endl;
    }

    int lnode, rnode, w;

    unordered_map<int, int> capStat;
    for (string line; getline(IF, line);) {
        istringstream iss(line);
        vector<string> tokens;
        copy(istream_iterator<string>(iss),
             istream_iterator<string>(),
             back_inserter(tokens));

        if (tokens[0] == "%") {
            this->nodenum = stoi(tokens[1]) + 1;
            this->edgenum = stoi(tokens[2]);
            Neighbor.resize(nodenum);
        } else {
            lnode = stoi(tokens[0]);
            rnode = stoi(tokens[1]);
            w = stod(tokens[2]);

            Neighbor[lnode].push_back(make_pair(rnode, w));
        }
    }
    IF.close();
    cout << "finish loading graph" << endl;
}


void Graph::ReadGraph(const string &filename)
{
    cout << "starts reading file" << endl;
    ifstream IF(filename);
    if (!IF)
    {
        cout << "Cannot open Map " << filename << endl;
    }

    string _;
    IF >> _ >> nodenum >> edgenum;

    eNum = 0;
    nodenum = 264347;
    set<pair<int, int>> eSet;

    vector<pair<int, int>> vecp;
    vecp.clear();
    Neighbor.assign(nodenum, vecp);

    set<int> setp;
    setp.clear();

    //to avoid the redundant information
    set<pair<int, int>> EdgeRedun;

    int ID1, ID2, weight;
    for (int i = 0; i < edgenum; i++)
    {
        IF >> ID1 >> ID2 >> weight;
        if (EdgeRedun.find(make_pair(ID1, ID2)) == EdgeRedun.end())
        {
            Neighbor[ID1].push_back(make_pair(ID2, weight));
        }
        EdgeRedun.insert(make_pair(ID1, ID2));
    }
    cout << "finish loading graph" << endl;
}

//------------------------------------以下为添加的函数-----------------------//
int Graph::queryH2H(int ID1, int ID2)
{
    if (ID1 == ID2)
        return 0;

    int r1 = rank[ID1], r2 = rank[ID2];
    int LCA = LCAQuery(r1, r2);
    if (LCA == r1)
    {
        return Tree[r2].dis[Tree[r1].pos.back()];
    }
    else if (LCA == r2)
    {
        return Tree[r1].dis[Tree[r2].pos.back()];
    }
    else
    {
        int tmp = INF;
        int sum = 0;

        for (int i = Tree[LCA].pos.size() - 1; i >= 0; --i)
        {
            sum = Tree[r1].dis[Tree[LCA].pos[i]] + Tree[r2].dis[Tree[LCA].pos[i]];
            if (tmp > sum)
                tmp = sum;
        }
        return tmp;
    }
}

int Graph::DijkstraDis(int ID1, int ID2)
{
    if (ID1 == ID2)
        return 0;
    benchmark::heap<2, int, int> pqueue(nodenum);
    pqueue.update(ID1, 0);

    vector<bool> closed(nodenum, false);
    vector<int> distance(nodenum, INF);

    distance[ID1] = 0;
    int topNodeID, topNodeDis;
    int NNodeID, NWeigh;

    int d = INF; // initialize d to infinite for the unreachable case

    while (!pqueue.empty())
    {
        pqueue.extract_min(topNodeID, topNodeDis);
        if (topNodeID == ID2)
        {
            d = distance[ID2];
            break;
        }
        closed[topNodeID] = true;

        for (auto it = Neighbor[topNodeID].begin(); it != Neighbor[topNodeID].end(); it++)
        {
            NNodeID = (*it).first;
            NWeigh = (*it).second + topNodeDis;
            if (!closed[NNodeID])
            {
                if (distance[NNodeID] > NWeigh)
                {
                    distance[NNodeID] = NWeigh;
                    pqueue.update(NNodeID, NWeigh);
                }
            }
        }
    }
    return d;
}

void Graph::writeOrder(const string &basicString) {
    ofstream OF(basicString);
    if (!OF) {
        cout << "Cannot open Map " << basicString << endl;
    }

    for (int i = 0; i <= nodenum; i++) {
        OF << i << " " << vNodeOrder[i] << "\n";
    }
    OF << endl;
    OF.close();
}

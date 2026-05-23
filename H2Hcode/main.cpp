/*
 * H2Hmain.cpp
 *
 *  Created on: 28 Dec 2020
 *      Author: zhangmengxuan
 */
#include "head.h"

int main()
{
    string basepath2 = "/Users/xyh/Desktop/traffic-assignment/data/", map = basepath2 + "BJ_map.txt";
    Graph g;
    g.readGraph1(map);

    g.CHcons();
    string orderFile = basepath2 + "BJ_order.txt";
    g.writeOrder(orderFile);
}
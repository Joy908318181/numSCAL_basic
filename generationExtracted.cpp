
/////////////////////////////////////////////////////////////////////////////
/// Name:        generationExtracted.cpp
/// Purpose:     methods related to regular network construction (simple bond
///              or pore/throat netorks) are defined here.
/// Author:      Ahmed Hamdi Boujelben <ahmed.hamdi.boujelben@gmail.com>
/// Created:     2017
/// Copyright:   (c) 2018 Ahmed Hamdi Boujelben
/// Licence:     Attribution-NonCommercial 4.0 International
/////////////////////////////////////////////////////////////////////////////

#include "network.h"
#include "iterator.h"

#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>

namespace PNM {

using namespace std;

void network::setupExtractedModel()
{
    cout<<"Reading Extracted Network.."<<endl;
    loadExtractedNetwork();
    cout<<"Cleaning Network..."<<endl;
    cleanNetwork();
    cout<<"Assigning Conductivities..."<<endl;
    assignConductivities();
    cout<<"Setting Wettability..."<<endl;
    assignWettability();
    cout<<"Calculating Network Attributes..."<<endl;
    calculateNetworkAttributes();

    if(absolutePermeabilityCalculation){
        cout<<"Calculating Absolute permeabilty..."<<endl;
        solvePressures();
        updateFlows();
        calculatePermeabilityAndPorosity();
    }

    //Display network info on screen
    displayNetworkInfo();
}

void network::loadExtractedNetwork()
{
    maxConnectionNumber=0;
    xEdgeLength=0;
    yEdgeLength=0;
    zEdgeLength=0;

    ofstream filep("Results/Network_Description/pores_radii.txt");
    ofstream filen("Results/Network_Description/nodes_radii.txt");

    // Creating Nodes
    string pathNode1=extractedNetworkFolderPath+rockPrefix+"_node1.dat";
    ifstream node1(pathNode1.c_str());

    node1>>totalNodes>>xEdgeLength>>yEdgeLength>>zEdgeLength;

    totaEnabledNodes=totalNodes;

    tableOfNodes.resize(totalNodes);

    string line;
    getline(node1,line);

    double averageConnectionNumber(0);

    for(int i=0;i<totalNodes;++i)
    {
        int id;
        double x,y,z;
        int numberOfNeighboors;
        bool isInlet,isOutlet;

        node1>>id>>x>>y>>z>>numberOfNeighboors;

        if(numberOfNeighboors>maxConnectionNumber)maxConnectionNumber=numberOfNeighboors;

        tableOfNodes[i]= new node(x,y,z);
        node* n=tableOfNodes[i];
        n->setId(id);
        n->setAbsId(i);
        n->setConnectionNumber(numberOfNeighboors);
        averageConnectionNumber+=numberOfNeighboors;

        for(int j=0;j<numberOfNeighboors;++j){
            int neighboor;
            node1>>neighboor;
        }

        node1>>isInlet>>isOutlet;
        n->setInlet(isInlet);
        n->setOutlet(isOutlet);

        for(int j=0;j<numberOfNeighboors;++j){
            int poreId;
            node1>>poreId;
        }
    }

    cout<<"Average Connection Number: "<<averageConnectionNumber/totalNodes<<endl;

    string pathNode2=extractedNetworkFolderPath+rockPrefix+"_node2.dat";
    ifstream node2(pathNode2.c_str());

    for(int i=0;i<totalNodes;++i)
    {
        int id;
        double volume,radius,shapeFactor;

        node2>>id>>volume>>radius>>shapeFactor>>line;

        node* n=getNode(id-1);
        n->setVolume(volume);
        n->setEffectiveVolume(volume);
        n->setRadius(radius);
        n->setShapeFactor(shapeFactor);
        if(n->getShapeFactor()<=sqrt(3)/36.)
            n->setShapeFactorConstant(0.6);
        else if (n->getShapeFactor()<=1./16.)
            n->setShapeFactorConstant(0.5623);
        else
            n->setShapeFactorConstant(0.5);
        n->setEntryPressureCoefficient(1+2*sqrt(pi()*n->getShapeFactor()));
        n->setLength(volume/pow(radius,2)*(4*shapeFactor));

        filen<<n->getId()<<" "<<n->getRadius()<<endl;
    }

    //Creating Pores

    //override max/min radius and average length
    maxRadius=0;
    minRadius=1e10;
    length=0;

    string pathLink1=extractedNetworkFolderPath+rockPrefix+"_link1.dat";
    ifstream link1(pathLink1.c_str());

    link1>>totalPores;

    totalEnabledPores=totalPores;

    tableOfPores.resize(totalPores);

    getline(link1,line);

    for(int i=0;i<totalPores;++i)
    {
        int id,nodeIndex1,nodeIndex2;
        double radius,shapeFactor,poreLength;

        link1>>id>>nodeIndex1>>nodeIndex2>>radius>>shapeFactor>>poreLength;

        if(radius>maxRadius)maxRadius=radius;
        if(radius<minRadius)minRadius=radius;

        node* nodeIn;
        node* nodeOut;

        if(nodeIndex1==0 || nodeIndex2==0)
        {
            nodeIn=0;
            nodeOut=(nodeIndex1==0? getNode(nodeIndex2-1) : getNode(nodeIndex1-1));
        }

        else if(nodeIndex1==-1 || nodeIndex2==-1)
        {
            nodeOut=0;
            nodeIn=(nodeIndex1==-1? getNode(nodeIndex2-1) : getNode(nodeIndex1-1));
        }
        else
        {
            nodeOut=getNode(nodeIndex1-1);
            nodeIn=getNode(nodeIndex2-1);
        }

        tableOfPores[i]=new pore(nodeIn,nodeOut);
        pore* p=tableOfPores[i];

        if(p->getNodeOut()==0)
        {
            p->setInlet(true);
            inletPores.push_back(p);
        }
        if(p->getNodeIn()==0)
        {
            p->setOutlet(true);
            outletPores.push_back(p);
        }
        p->setId(id);
        p->setAbsId(i+totalNodes);
        p->setShapeFactor(shapeFactor);
        if(p->getShapeFactor()<=sqrt(3)/36.)
            p->setShapeFactorConstant(0.6);
        else if (p->getShapeFactor()<=1./16.)
            p->setShapeFactorConstant(0.5623);
        else
            p->setShapeFactorConstant(0.5);
        p->setEntryPressureCoefficient(1+2*sqrt(pi()*p->getShapeFactor()));
        p->setFullLength(poreLength);
        if(p->getNodeIn()!=0 && p->getNodeOut()!=0)
        {
            double length=sqrt(pow(p->getNodeIn()->getXCoordinate()-p->getNodeOut()->getXCoordinate(),2)+pow(p->getNodeIn()->getYCoordinate()-p->getNodeOut()->getYCoordinate(),2)+pow(p->getNodeIn()->getZCoordinate()-p->getNodeOut()->getZCoordinate(),2));
            p->setFullLength(length);
        }
        p->setRadius(radius);
        length+=poreLength;

        filep<<p->getId()<<" "<<p->getRadius()<<endl;
    }
    length=length/totalPores;

    string pathLink2=extractedNetworkFolderPath+rockPrefix+"_link2.dat";
    ifstream link2(pathLink2.c_str());

    for(int i=0;i<totalPores;++i)
    {
        int id,nodeIndex1,nodeIndex2;;
        double node1Length,node2Length,throatLength,volume;

        link2>>id>>nodeIndex1>>nodeIndex2>>node1Length>>node2Length>>throatLength>>volume>>line;

        pore* p=getPore(i);
        node* nodeIn=p->getNodeIn();

        if(nodeIndex1==0 || nodeIndex1==-1)
        {
            if(nodeIn==0)
                p->setNodeOutLength(node2Length);
            else
                p->setNodeInLength(node2Length);
        }
        else if(nodeIndex2==0 || nodeIndex2==-1)
        {
            if(nodeIn==0)
                p->setNodeOutLength(node1Length);
            else
                p->setNodeInLength(node1Length);
        }
        else
        {
            p->setNodeOutLength(node1Length);
            p->setNodeInLength(node2Length);
        }

        p->setLength(throatLength);
        p->setVolume(volume);
        p->setEffectiveVolume(volume);
    }

    //Assigning neighboors
    node1.seekg(0);

    string dummy;
    node1>>dummy>>dummy>>dummy>>dummy;
    getline(node1,dummy);

    for(int i=0;i<totalNodes;++i)
    {
        node* n=getNode(i);
        int numberOfNeighboors;

        node1>>dummy>>dummy>>dummy>>dummy>>numberOfNeighboors;
        for(int j=0;j<numberOfNeighboors;++j)
            node1>>dummy;
        node1>>dummy>>dummy;

        vector<element*> neighboors;
        for(int j=0;j<numberOfNeighboors;++j){
            int poreId;
            node1>>poreId;
            neighboors.push_back(getPore(poreId-1));
        }
        n->setNeighboors(neighboors);
    }

    for(pore* p : tableOfPores)
    {
        vector<element*> neighboors;
        if(p->getNodeIn()!=0)
            neighboors.push_back(p->getNodeIn());
        if(p->getNodeOut()!=0)
            neighboors.push_back(p->getNodeOut());
        p->setNeighboors(neighboors);
    }
}

}

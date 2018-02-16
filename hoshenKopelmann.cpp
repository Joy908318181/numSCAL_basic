/////////////////////////////////////////////////////////////////////////////
/// Name:        hoshenKopelmann.cpp
/// Purpose:     methods related to Hoshen-Kopelmann clustering algorithms
///              are defined here.
/// Author:      Ahmed Hamdi Boujelben <ahmed.hamdi.boujelben@gmail.com>
/// Created:     2017
/// Copyright:   (c) 2017 Ahmed Hamdi Boujelben
/// Licence:     Attribution-NonCommercial 4.0 International
/////////////////////////////////////////////////////////////////////////////

#include "network.h"

namespace PNM {

//clustering

int network::hkFind(int x, vector<int>& labels)
{
    int y = x;
    while (labels[y] != y)
      y = labels[y];

    while (labels[x] != x) {
      int z = labels[x];
      labels[x] = y;
      x = z;
    }
    return y;
}

int network::hkUnion(vector<int>& v,vector<int>& labels)
{
    //joins equivalence classes and returns the canonical label of the resulting class.
    for(unsigned i=1;i<v.size();++i)
        labels[hkFind(v[i],labels)] = hkFind(v[0],labels);
    return hkFind(v[0],labels);
}

int network::hkMakeSet(vector<int>& labels)
{
    //creates a new equivalence class and returns its label
    labels[0] ++;
    labels.push_back(labels[0]);
    return labels[0];
}

template<typename T>
void network::clusterPores(cluster*(pore::*getter)(void) const,void(pore::*setter)(cluster*),T(pore::*status)(void) const,T flag, vector<cluster*> &clustersList)
{
    if(!clustersList.empty())
        for (unsigned i = 0; i < clustersList.size(); ++i)
            delete clustersList[i];
    clustersList.clear();

    for(int i=0; i<totalPores; ++i)
    {
        pore* p=getPore(i);
        if((p->*status)()==flag)
            p->setClusterTemp(0);
    }

    vector<int> labels;
    labels.push_back(0);


    for(int i=0; i<totalPores;++i)
    {
        pore* p=getPore(i);
        if((p->*status)()==flag)
        {
            vector<int> neighboorsClusters;
            vector<pore*> neighboors=p->getConnectedPores();
            for(unsigned j=0;j<neighboors.size();j++)
            {
                if((neighboors[j]->*status)()==flag && neighboors[j]->getClusterTemp()!=0)
                    neighboorsClusters.push_back(neighboors[j]->getClusterTemp());
            }
            if(neighboorsClusters.empty())
                p->setClusterTemp(hkMakeSet(labels));
            else if(neighboorsClusters.size()==1)
                p->setClusterTemp(neighboorsClusters[0]);
            else
                p->setClusterTemp(hkUnion(neighboorsClusters,labels));
        }
    }

    // Create a mapping from the canonical labels determined by union/find into a new set of canonical labels, which are guaranteed to be sequential.

    vector<int> new_labels(labels.size(),0);

    for(int i=0; i<totalPores; ++i)
    {
        pore* p=getPore(i);
        if((p->*status)()==flag)
        {
            int x=hkFind(p->getClusterTemp(),labels);
            if (new_labels[x] == 0)
            {
                new_labels[0]++;
                new_labels[x] = new_labels[0];
                clustersList.push_back(new cluster(new_labels[0]));
            }
            (p->*setter)(clustersList[new_labels[x]-1]);
        }
    }

    //Identify sepecial clusters
    set<cluster*> inletClusters,outletClusters,spanningClusters;

    for(pore* p: inletPores)
        if((p->*status)()==flag)
            inletClusters.insert((p->*getter)());

    for(pore* p: outletPores)
        if((p->*status)()==flag)
            outletClusters.insert((p->*getter)());


    for (cluster* c: inletClusters)
        c->setInlet(true);

    for (cluster* c: outletClusters)
        c->setOutlet(true);

    set_intersection( inletClusters.begin(), inletClusters.end(), outletClusters.begin(), outletClusters.end(), std::inserter( spanningClusters, spanningClusters.begin() ) );

    for (cluster* c: spanningClusters)
        c->setSpanning(true);
}

void network::clusterWaterPores()
{
    cluster* (pore::*getter)() const =&pore::getClusterWater;
    void (pore::*setter)(cluster*) =&pore::setClusterWater;
    char (pore::*status)(void) const=&pore::getPhaseFlag;
    clusterPores(getter,setter,status,'w',waterClusters);

    isWaterSpanning=false;
    for(unsigned i=0;i<waterClusters.size();++i)
    {
        cluster* cls=waterClusters[i];
        if(cls->getSpanning())
        {
            isWaterSpanning=true;
            break;
        }
    }
}

void network::clusterOilPores()
{
    cluster* (pore::*getter)() const =&pore::getClusterOil;
    void (pore::*setter)(cluster*) =&pore::setClusterOil;
    char (pore::*status)(void) const=status=&pore::getPhaseFlag;
    clusterPores(getter,setter,status,'o',oilClusters);

    isOilSpanning=false;
    for(unsigned i=0;i<oilClusters.size();++i)
    {
        cluster* cls=oilClusters[i];
        if(cls->getSpanning())
        {
            isOilSpanning=true;
            break;
        }
    }
}

template<typename T>
void network::clusterElements(cluster *(element::*getter)() const, void (element::*setter)(cluster *), T(element::*status)() const, T flag, std::vector<cluster *> &clustersList)
{
    if(!clustersList.empty())
        for (unsigned i = 0; i < clustersList.size(); ++i)
            delete clustersList[i];
    clustersList.clear();

    for(int i=0; i<totalElements; ++i)
       if((getElement(i)->*status)()==flag)
           getElement(i)->setClusterTemp(0);

    vector<int> labels;
    labels.push_back(0);


    for(int i=0; i<totalElements;++i)
    {

        element* e=getElement(i);
        if((e->*status)()==flag)
        {
            vector<int> neighboorsClusters;
            vector<element*> neighboors=e->getNeighboors();
            for(unsigned j=0;j<neighboors.size();j++)
            {
                if((neighboors[j]->*status)()==flag && neighboors[j]->getClusterTemp()!=0)
                    neighboorsClusters.push_back(neighboors[j]->getClusterTemp());
            }
            if(neighboorsClusters.empty())
                e->setClusterTemp(hkMakeSet(labels));
            else if(neighboorsClusters.size()==1)
                e->setClusterTemp(neighboorsClusters[0]);
            else
                e->setClusterTemp(hkUnion(neighboorsClusters,labels));
        }
    }

    //Create a mapping from the canonical labels determined by union/find into a new set of canonical labels, which are guaranteed to be sequential.

    vector<int> new_labels(labels.size(),0);

    for(int i=0; i<totalElements; ++i)
    {
        if((getElement(i)->*status)()==flag)
        {
            int x=hkFind(getElement(i)->getClusterTemp(),labels);
            if (new_labels[x] == 0)
            {
                new_labels[0]++;
                new_labels[x] = new_labels[0];
                clustersList.push_back(new cluster(new_labels[0]));
            }
            (getElement(i)->*setter)(clustersList[new_labels[x]-1]);
        }
    }

    //Identify sepecial clusters
    set<cluster*> inletClusters,outletClusters,spanningClusters;

    for(pore* p: inletPores)
        if((p->*status)()==flag)
            inletClusters.insert((p->*getter)());

    for(pore* p: outletPores)
        if((p->*status)()==flag)
            outletClusters.insert((p->*getter)());


    for (cluster* c: inletClusters)
        c->setInlet(true);

    for (cluster* c: outletClusters)
        c->setOutlet(true);

    set_intersection( inletClusters.begin(), inletClusters.end(), outletClusters.begin(), outletClusters.end(), std::inserter( spanningClusters, spanningClusters.begin() ) );

    for (cluster* c: spanningClusters)
        c->setSpanning(true);
}

void network::clusterWaterWetElements()
{
    cluster* (element::*getter)() const =&element::getClusterWaterWet;
    void (element::*setter)(cluster*) =&element::setClusterWaterWet;
    char (element::*status)(void) const=&element::getWettabilityFlag;
    clusterElements(getter,setter,status,'w',waterWetClusters);
}

void network::clusterOilWetElements()
{
    cluster* (element::*getter)() const =&element::getClusterOilWet;
    void (element::*setter)(cluster*) =&element::setClusterOilWet;
    char (element::*status)(void) const=&element::getWettabilityFlag;
    clusterElements(getter,setter,status,'o',oilWetClusters);
}

void network::clusterWaterElements()
{
    cluster* (element::*getter)() const =&element::getClusterWater;
    void (element::*setter)(cluster*) =&element::setClusterWater;
    char (element::*status)(void) const=&element::getPhaseFlag;
    clusterElements(getter,setter,status,'w',waterClusters);
    isWaterSpanning=false;
    for(unsigned i=0;i<waterClusters.size();++i)
    {
        cluster* cls=waterClusters[i];
        if(cls->getSpanning())
        {
            isWaterSpanning=true;
            break;
        }
    }
}

void network::clusterOilElements()
{
    cluster* (element::*getter)() const =&element::getClusterOil;
    void (element::*setter)(cluster*) =&element::setClusterOil;
    char (element::*status)(void) const=&element::getPhaseFlag;
    clusterElements(getter,setter,status,'o',oilClusters);
    isOilSpanning=false;
    for(unsigned i=0;i<oilClusters.size();++i)
    {
        cluster* cls=oilClusters[i];
        if(cls->getSpanning())
        {
            isOilSpanning=true;
            break;
        }
    }
}

void network::clusterOilFlowingElements()
{
    cluster* (element::*getter)() const =&element::getClusterOilFilm;
    void (element::*setter)(cluster*) =&element::setClusterOilFilm;
    char (element::*status)(void) const=&element::getOilFlowing;
    clusterElements(getter,setter,status,'t',oilFilmClusters);

    isOilSpanning=false;
    for(unsigned i=0;i<oilFilmClusters.size();++i)
    {
        cluster* cls=oilFilmClusters[i];
        if(cls->getSpanning())
        {
            isOilSpanning=true;
            break;
        }
    }

}

void network::clusterWaterFlowingElements()
{
    cluster* (element::*getter)() const =&element::getClusterWaterFilm;
    void (element::*setter)(cluster*) =&element::setClusterWaterFilm;
    char (element::*status)(void) const=&element::getWaterFlowing;
    clusterElements(getter,setter,status,'t',waterFilmClusters);

    isWaterSpanning=false;
    for(unsigned i=0;i<waterFilmClusters.size();++i)
    {
        cluster* cls=waterFilmClusters[i];
        if(cls->getSpanning())
        {
            isWaterSpanning=true;
            break;
        }
    }
}

void network::clusterActiveElements()
{
    cluster* (element::*getter)() const =&element::getClusterActive;
    void (element::*setter)(cluster*) =&element::setClusterActive;
    char (element::*status)(void) const=&element::getActive;
    clusterElements(getter,setter,status,'t',activeClusters);

    isNetworkSpanning=false;
    for(unsigned i=0;i<activeClusters.size();++i)
    {
        cluster* cls=activeClusters[i];
        if(cls->getSpanning())
        {
            isNetworkSpanning=true;
            break;
        }
    }
}

}

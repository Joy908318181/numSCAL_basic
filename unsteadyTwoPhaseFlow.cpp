/////////////////////////////////////////////////////////////////////////////
/// Name:        unsteadyTwoPhaseFlowFastPT.cpp
/// Purpose:     methods related to unsteady-state drainage simulation
///              in pore/throat networks are defined here.
/// Author:      Ahmed Hamdi Boujelben <ahmed.hamdi.boujelben@gmail.com>
/// Created:     2017
/// Copyright:   (c) 2018 Ahmed Hamdi Boujelben
/// Licence:     Attribution-NonCommercial 4.0 International
/////////////////////////////////////////////////////////////////////////////

#include "network.h"

namespace PNM {

void network::runUSSDrainageModelPT()
{
    cout<<"Starting Unsteady State Drainage Model... "<<endl;

    double startTime,endTime;
    startTime=tools::getCPUTime();

    //post-processing
    if(videoRecording)
        record=true;

    initializeTwoPhaseSimulationPT();
    initializeTwoPhaseOutputs();
    setInitialFlagsPT();

    //initialise flags
    double timeSoFar(0);
    double injectedPVs(0);
    double injectThreshold(0.01);
    double outputPVs(0.0);
    int outputCount(0);
    bool solvePressure=true;
    bool waterSpanning=isWaterSpanning;
    deltaP=1;

    set<pore*> poresToCheck;
    set<node*> nodesToCheck;

    while(!cancel && timeSoFar<simulationTime)
    {
        //Update viscosities/conductivities/thetas and trapping conditions
        if(solvePressure)
        {
            setAdvancedTrappingPT();

            poresToCheck.clear();
            nodesToCheck.clear();
            updateCapillaryPropertiesPT(poresToCheck,nodesToCheck);

            //Solve pressure distribution and block counter imbibition flow
            solvePressureWithoutCounterImbibitionPT();
            solvePressure=false;
        }

        //Calculate timestep
        calculateTimeStepUSSPT(poresToCheck,nodesToCheck,waterSpanning);

        //Update Fluid fractions
        updateElementaryFluidFractionsPT(poresToCheck,nodesToCheck,solvePressure);

        //Update Fluid Flags
        updateElementaryFluidFlagsPT(poresToCheck, nodesToCheck);

        if(timeStep!=1e50)
        {
            timeSoFar+=timeStep;
            double injectedVolume=timeStep*flowRate/totalElementsVolume;
            injectedPVs+=injectedVolume;
            outputPVs+=injectedVolume;
        }

        // Write up data
        double waterSat=getWaterSaturation();
        if(outputPVs>injectThreshold)
        {
            outputTwoPhaseData(injectedPVs,outputCount, waterSat);
            outputPVs=0;
        }

        //Display notification
        std::ostringstream ss;
        ss << std::fixed << std::setprecision(2);
        ss << "Injected PVs: " << injectedPVs<<" / Sw(%): "<<waterSat*100;
        simulationNotification = ss.str();

        //Update simulation flags

        if(!waterSpanning)
        {
            clusterWaterElements();
            if(isWaterSpanning)
            {
                waterSpanning=true;
                cout<<"Water Breakthrough..."<<endl<<"Time at Water Breakthough: "<<timeSoFar<<endl<<"Injected PVs at Water Breakthough: "<<injectedPVs<<endl;
            }
        }

        emitPlotSignal();

        //Thread Management
        if(cancel)break;
    }

    //post-processing
    if(videoRecording)
    {
        record=false;
        extractVideo();
    }

    cout<<"Simulation Time: "<<timeSoFar<<" s"<<endl;
    cout<<"Injected PVs: "<<injectedPVs<<endl;
    endTime=tools::getCPUTime();
    cout<<"Fast Unsteady State Water Injection Time: "<<endTime-startTime<<" s"<<endl;
}

void network::setInitialFlagsPT()
{
    clusterWaterElements();

    for(pore* p : accessiblePores)
    {
        if(p->getInlet() && p->getPhaseFlag()==phase::oil) //create Pc in oil-filled inlet pores
        {
            p->setNodeOutOil(false);
            p->setNodeOutWater(true);
            p->setNodeInOil(true);
            p->setNodeInWater(false);
        }
    }

    for(node* p: accessibleNodes) //create Pc in oil-filled pores next to inlet-connected water
    {
        if(p->getPhaseFlag()==phase::water && p->getClusterWater()->getInlet())
        for(int i : p->getConnectedPores())
        {
            pore* neigh=getPore(i-1);
            if(!neigh->getClosed() && neigh->getPhaseFlag()==phase::oil && !p->getInlet() && !neigh->getInlet())
            {
                if(p==neigh->getNodeIn())
                {
                    neigh->setNodeInOil(false);
                    neigh->setNodeInWater(true);
                }

                if(p==neigh->getNodeOut())
                {
                    neigh->setNodeOutOil(false);
                    neigh->setNodeOutWater(true);
                }

                if(p==neigh->getNodeOut())
                {
                    neigh->setNodeOutOil(false);
                    neigh->setNodeOutWater(true);
                }

                if(p==neigh->getNodeIn())
                {
                    neigh->setNodeInOil(false);
                    neigh->setNodeInWater(true);
                }
            }
        }
    }
}

void network::setAdvancedTrappingPT()
{
    clusterOilElements();
    clusterWaterElements();
    clusterOilWetElements();

    vector<pore*> partiallyFilled;
    for(pore* p :accessiblePores)
    {
        if(p->getPhaseFlag()==phase::oil)
        {
            p->setOilTrapped(true);

            if(p->getOutlet())
                p->setOilTrapped(false);

            else if(p->getNodeInWater() || p->getNodeOutWater())
            {
                p->setPhaseFlag(phase::temp);
                partiallyFilled.push_back(p);
            }
        }
        if(p->getPhaseFlag()==phase::water)
        {
            p->setWaterTrapped(true);
            if(p->getClusterWater()->getInlet())
                    p->setWaterTrapped(false);
        }
    }

    for(node* p :accessibleNodes)
    {
        if(p->getPhaseFlag()==phase::oil)
        {
            p->setOilTrapped(true);
            if(p->getPhaseFlag()==phase::oil && p->getClusterOil()->getOutlet())
                p->setOilTrapped(false);

        }
        if(p->getPhaseFlag()==phase::water)
        {
            p->setWaterTrapped(true);
            if(p->getClusterWater()->getInlet())
                p->setWaterTrapped(false);
        }
    }

    clusterOilElements();

    for(pore* p :accessiblePores)
    {
        if(p->getPhaseFlag()==phase::oil && p->getClusterOil()->getOutlet())
            p->setOilTrapped(false);
    }

    //Identify oil-filled pores without bulk oil to outlet
    for(pore* p :partiallyFilled)
    {
        if(p->getNodeInOil() && p->getNodeIn()!=0 && !p->getNodeIn()->getClosed() && p->getNodeIn()->getPhaseFlag()==phase::oil && p->getNodeIn()->getClusterOil()->getOutlet())
            p->setOilTrapped(false);

        if(p->getNodeOutOil() && p->getNodeOut()!=0 && !p->getNodeOut()->getClosed() && p->getNodeOut()->getPhaseFlag()==phase::oil && p->getNodeOut()->getClusterOil()->getOutlet())
            p->setOilTrapped(false);

        //Allow slow filling of throats connecting water clusters (a workaround to mimic experimental observations)
        if(enhancedWaterConnectivity)
            if(!p->getNodeOutOil() && !p->getNodeInOil() && p->getWettabilityFlag()==wettability::oilWet && p->getClusterOilWet()->getOutlet())
                p->setOilTrapped(false);
    }

    for(pore* p: partiallyFilled)
        p->setPhaseFlag(phase::oil);
}

void network::updateCapillaryPropertiesPT(std::set<pore *> & poresToCheck, std::set<node *> &nodesToCheck)
{
    clusterOilElements();
    clusterWaterElements();

    for(node* p : accessibleNodes)
    {
        p->assignViscosity(oilViscosity, waterViscosity);

        p->setActive(true);
        if(p->getPhaseFlag()==phase::oil && p->getOilTrapped())
        {
            p->setConductivity(1e-200);
            p->setActive(false);
        }

        if(p->getPhaseFlag()==phase::water && p->getWaterTrapped())
        {
            p->setConductivity(1e-200);
            p->setActive(false);
        }
    }

    for(pore* p : accessiblePores)
    {
        p->assignViscosity(oilViscosity, waterViscosity);
        p->assignConductivity();

        p->setActive(true);
        p->setCapillaryPressure(0);
        if(p->getPhaseFlag()==phase::oil)
        {

            if(p->getOilTrapped())
            {
                p->setConductivity(1e-200);
                p->setActive(false);
            }
            else
            {
                if(p->getInlet() || (p->getNodeIn()!=0 && !p->getNodeIn()->getClosed() && p->getNodeIn()->getPhaseFlag()==phase::water && !p->getNodeIn()->getWaterTrapped()) || (p->getNodeOut()!=0 && !p->getNodeOut()->getClosed() &&  p->getNodeOut()->getPhaseFlag()==phase::water && !p->getNodeOut()->getWaterTrapped()))
                    poresToCheck.insert(p);

                //Allow slow filling of throats connecting water clusters (a workaround to mimic experimental observations)
                if(enhancedWaterConnectivity)
                    if(!p->getNodeOutOil() && !p->getNodeInOil() && p->getPhaseFlag()==phase::oil)
                        p->setConductivity(p->getConductivity()/200.);

                double capillaryPressure=0;

                if(!p->getInlet() && !p->getOutlet() && p->getNodeIn()!=0 && p->getNodeOut()!=0)
                {
                    if(p->getNodeOut()->getPhaseFlag()==phase::oil && p->getNodeIn()->getPhaseFlag()==phase::water)
                        capillaryPressure=2*OWSurfaceTension*cos(p->getTheta())/p->getRadius();
                }
                if(!p->getInlet() && !p->getOutlet() && p->getNodeIn()!=0 && p->getNodeOut()!=0)
                {
                    if(p->getNodeOut()->getPhaseFlag()==phase::water && p->getNodeIn()->getPhaseFlag()==phase::oil)
                        capillaryPressure=-2*OWSurfaceTension*cos(p->getTheta())/p->getRadius();
                }
                if(p->getInlet())
                {
                    if(p->getNodeIn()->getPhaseFlag()==phase::oil)
                        capillaryPressure=-2*OWSurfaceTension*cos(p->getTheta())/p->getRadius();
                }
                if(p->getOutlet())
                {
                    if(p->getNodeOut()->getPhaseFlag()==phase::water)
                        capillaryPressure=-2*OWSurfaceTension*cos(p->getTheta())/p->getRadius();
                }
                p->setCapillaryPressure(capillaryPressure);
            }
        }

        if(p->getPhaseFlag()==phase::water)
        {
            if(p->getWaterTrapped())
            {
                p->setConductivity(1e-200);
                p->setActive(false);
            }
            else
            {
                node* nodeIn=p->getNodeIn();
                node* nodeOut=p->getNodeOut();
                if(nodeIn!=0 && !nodeIn->getClosed() && nodeIn->getPhaseFlag()==phase::oil && !nodeIn->getOilTrapped())
                    nodesToCheck.insert(nodeIn);
                if(nodeOut!=0 && !nodeOut->getClosed() && nodeOut->getPhaseFlag()==phase::oil && !nodeOut->getOilTrapped())
                    nodesToCheck.insert(nodeOut);

                double capillaryPressure=0;

                if(!p->getInlet() && !p->getOutlet() && nodeIn!=0 && nodeOut!=0)
                {
                    //pore filling mechanism
                    int oilNeighboorsNumber(0);
                    for(element* n : nodeOut->getNeighboors())
                        if(!n->getClosed() && n->getPhaseFlag()==phase::oil)
                            oilNeighboorsNumber++;

                    if(nodeOut->getPhaseFlag()==phase::oil && nodeIn->getPhaseFlag()==phase::water)
                    {
                        if(p->getTheta()>tools::pi()/2)//drainage
                            capillaryPressure=2*OWSurfaceTension*cos(p->getTheta())/nodeOut->getRadius();
                        if(p->getTheta()<tools::pi()/2)//imbibition
                            capillaryPressure=2*OWSurfaceTension*cos(p->getTheta())/nodeOut->getRadius()-oilNeighboorsNumber*OWSurfaceTension/nodeOut->getRadius();
                    }
                }
                if(!p->getInlet() && !p->getOutlet() && nodeIn!=0 && nodeOut!=0)
                {
                    //pore filling mechanism
                    int oilNeighboorsNumber(0);
                    for(element* n : nodeIn->getNeighboors())
                        if(!n->getClosed() && n->getPhaseFlag()==phase::oil)
                            oilNeighboorsNumber++;

                    if(nodeOut->getPhaseFlag()==phase::water && nodeIn->getPhaseFlag()==phase::oil)
                    {
                        if(p->getTheta()>tools::pi()/2)//drainage
                            capillaryPressure=-2*OWSurfaceTension*cos(p->getTheta())/nodeIn->getRadius();
                        if(p->getTheta()<tools::pi()/2)//imbibition
                            capillaryPressure=-2*OWSurfaceTension*cos(p->getTheta())/nodeIn->getRadius()+oilNeighboorsNumber*OWSurfaceTension/nodeIn->getRadius();
                    }
                }
                if(p->getInlet())
                {
                    //pore filling mechanism
                    int oilNeighboorsNumber(0);
                    for(element* n : nodeIn->getNeighboors())
                        if(!n->getClosed() && n->getPhaseFlag()==phase::oil)
                            oilNeighboorsNumber++;

                    if(nodeIn->getPhaseFlag()==phase::oil)
                    {
                        if(p->getTheta()>tools::pi()/2)//drainage
                            capillaryPressure=-2*OWSurfaceTension*cos(p->getTheta())/nodeIn->getRadius();
                        if(p->getTheta()<tools::pi()/2)//imbibition
                            capillaryPressure=-2*OWSurfaceTension*cos(p->getTheta())/nodeIn->getRadius()+oilNeighboorsNumber*OWSurfaceTension/nodeIn->getRadius();
                    }
                }

                p->setCapillaryPressure(capillaryPressure);
            }
        }
    }
}

void network::solvePressureWithoutCounterImbibitionPT()
{
    bool stillMorePoresToClose=true;

    while(stillMorePoresToClose)
    {
        clusterActiveElements();
        for(pore* p : accessiblePores)
        {
            if(p->getActive() && p->getClusterActive()->getSpanning()==false)
            {
                p->setConductivity(1e-200);
                p->setCapillaryPressure(0);
                p->setActive(false);
            }
        }

        stillMorePoresToClose=false;
        double testQ=0;
        double testQ1,testQ2,deltaP1,deltaP2;
        deltaP1=deltaP*(1+0.5*(flowRate-testQ)/flowRate);
        pressureIn=deltaP1;
        pressureOut=0;
        solvePressuresWithCapillaryPressures();
        testQ1=updateFlowsWithCapillaryPressure();

        deltaP2=deltaP*(1-0.5*(flowRate-testQ)/flowRate);
        pressureIn=deltaP2;
        pressureOut=0;
        solvePressuresWithCapillaryPressures();
        testQ2=updateFlowsWithCapillaryPressure();

        while(testQ>1.01*flowRate || testQ<0.99*flowRate)
        {
            deltaP=deltaP2-(deltaP2-deltaP1)*(testQ2-flowRate)/(testQ2-testQ1);
            pressureIn=deltaP;
            pressureOut=0;
            solvePressuresWithCapillaryPressures();
            testQ=updateFlowsWithCapillaryPressure();
            testQ1=testQ2;
            testQ2=testQ;
            deltaP1=deltaP2;
            deltaP2=deltaP;

            //Thread Management
            if(cancel)break;
        }

        for(pore* p : accessiblePores)
        {
            if(p->getActive() && p->getNodeIn()!=0 && p->getNodeOut()!=0 && ((p->getFlow()>0 && p->getNodeOut()->getPhaseFlag()==phase::oil && p->getNodeIn()->getPhaseFlag()==phase::water) || (p->getFlow()<0 && p->getNodeOut()->getPhaseFlag()==phase::water && p->getNodeIn()->getPhaseFlag()==phase::oil)))
            {
                p->setConductivity(1e-200);
                p->setCapillaryPressure(0);
                p->setActive(false);
                stillMorePoresToClose=true;
            }
            if(p->getActive() && (p->getInlet() || p->getOutlet()) &&  p->getFlow()<0)
            {
                p->setConductivity(1e-200);
                p->setCapillaryPressure(0);
                p->setActive(false);
                stillMorePoresToClose=true;
            }
        }

        if(cancel)break;
        //file<<endl;
    }

    for(node* n : accessibleNodes)
    {
        n->setFlow(0);
    }
    for(pore* p : accessiblePores)
    {

        if(p->getPhaseFlag()==phase::water)
        {
            if(p->getFlow()>1e-24 && p->getActive())
            {
                node* n=p->getNodeIn();
                if(n!=0)
                {
                    n->setFlow((n->getFlow()+abs(p->getFlow())));
                }
            }
            if(p->getFlow()<-1e-24 && p->getActive())
            {
                node* n=p->getNodeOut();
                if(n!=0)
                {
                    n->setFlow((n->getFlow()+abs(p->getFlow())));
                }
            }
        }
    }
}

void network::calculateTimeStepUSSPT(std::set<pore *> & poresToCheck, std::set<node *> &nodesToCheck,bool includeWater)
{
    timeStep=1e50;
    for(pore* p : poresToCheck)
    {
        if(p->getActive() && abs(p->getFlow())>1e-24)
        {
            double step=p->getVolume()*p->getOilFraction()/abs(p->getFlow());
            if(step<timeStep)
            {
                timeStep=step;
            }
        }
    }

    for(node* p : nodesToCheck)
    {
        if(abs(p->getFlow())>1e-24 && p->getActive())
        {
            double step=p->getVolume()*p->getOilFraction()/abs(p->getFlow());
            if(step<timeStep)
            {
                timeStep=step;
            }
        }
    }

    if(includeWater) //if water is forming a spanning cluster
    {
        for(pore* p: accessiblePores)
        {
            if(p->getActive() && p->getPhaseFlag()==phase::water && abs(p->getFlow())>1e-24)
            {
                double step=p->getVolume()/abs(p->getFlow());
                if(step<timeStep)
                {
                    timeStep=step;
                }
            }
        }

        for(node* p : accessibleNodes)
        {
            if(p->getActive() && p->getPhaseFlag()==phase::water && abs(p->getFlow())>1e-24)
            {
                double step=p->getVolume()/abs(p->getFlow());
                if(step<timeStep)
                {
                    timeStep=step;
                }
            }
        }
    }

    if(timeStep==1e50)
    {
        cancel=true;
        cout<<"cancel: infinite time step "<<timeStep<<endl;
    }
}

double network::updateElementaryFluidFractionsPT(std::set<pore *> &poresToCheck, std::set<node *> &nodesToCheck, bool &solvePressure)
{
    double additionalWater(0);
    for(pore* p : poresToCheck)
    {
        if(p->getActive() && abs(p->getFlow())>1e-24)
        {
            if(p->getFlow()>0 && p->getNodeOutWater() || p->getFlow()<0 && p->getNodeInWater())
            {
                double incrementalWater=abs(p->getFlow())*timeStep;
                p->setWaterFraction(p->getWaterFraction()+incrementalWater/p->getVolume());
                p->setOilFraction(1-p->getWaterFraction());

                additionalWater+=incrementalWater;

                if(p->getWaterFraction()>1-1e-8)
                {
                    p->setPhaseFlag(phase::water);
                    p->setWaterFraction(1.0);
                    p->setOilFraction(0.0);
                    solvePressure=true;
                }

                //mass conservation check
                if(p->getOilFraction()>1.0001 || p->getOilFraction()<-0.0001) {cout<<"Something wrong: oil fraction >1 or <0 "<<p->getOilFraction()<<endl;cancel=true;}
                if(p->getWaterFraction()>1.0001 || p->getWaterFraction()<-0.0001) {cout<<"Something wrong: water fraction >1 or <0 "<<p->getWaterFraction()<<endl;cancel=true;}
            }
        }
    }

    for(node* p : nodesToCheck)
    {
        if(p->getActive() && abs(p->getFlow())>1e-24)
        {
            double incrementalWater=abs(p->getFlow())*timeStep;
            p->setWaterFraction(p->getWaterFraction()+incrementalWater/p->getVolume());
            p->setOilFraction(1-p->getWaterFraction());

            additionalWater+=incrementalWater;

            if(p->getWaterFraction()>1-1e-8)
            {
                p->setPhaseFlag(phase::water);
                p->setWaterFraction(1.0);
                p->setOilFraction(0.0);
                solvePressure=true;
            }

            //mass conservation check
            if(p->getOilFraction()>1.0001 || p->getOilFraction()<-0.0001) {cout<<"Something wrong: oil fraction >1 or <0 "<<p->getOilFraction()<<endl;cancel=true;}
            if(p->getWaterFraction()>1.0001 || p->getWaterFraction()<-0.0001) {cout<<"Something wrong: water fraction >1 or <0 "<<p->getWaterFraction()<<endl;cancel=true;}
        }
    }

    return additionalWater;
}

void network::updateElementaryFluidFlagsPT(std::set<pore *> &poresToCheck, std::set<node *> &nodesToCheck)
{
    for(pore* p: poresToCheck)
    {
        if(p->getPhaseFlag()==phase::water)
        {
            p->setNodeInOil(false);
            p->setNodeOutOil(false);
            p->setNodeInWater(true);
            p->setNodeOutWater(true);
        }

        node* n=0;
        if(p->getNodeInOil())
            n=p->getNodeIn();
        if(p->getNodeOutOil())
            n=p->getNodeOut();

        if(n!=0 && n->getPhaseFlag()==phase::water && n->getWaterTrapped())
        {
            for(node* nn: accessibleNodes)
            {
                if(nn->getPhaseFlag()==phase::water && nn->getClusterWater()==n->getClusterWater())
                for(auto j : nn->getConnectedPores())
                {
                    pore* nnn=getPore(j-1);
                    if(!nnn->getClosed() && nnn->getPhaseFlag()==phase::oil)
                    {
                        if(nnn->getNodeIn()==nn)
                        {
                            nnn->setNodeInOil(false);
                            nnn->setNodeInWater(true);
                        }

                        if(nnn->getNodeOut()==nn)
                        {
                            nnn->setNodeOutOil(false);
                            nnn->setNodeOutWater(true);
                        }
                    }
                }
            }
        }
    }

    for(node* p : nodesToCheck)
    {
        if(p->getPhaseFlag()==phase::water)
        {
            for(auto i : p->getConnectedPores())
            {
                pore* n=getPore(i-1);
                if(!n->getClosed() && n->getPhaseFlag()==phase::oil)
                {

                    if(n->getNodeIn()==p)
                    {
                        n->setNodeInOil(false);
                        n->setNodeInWater(true);
                    }

                    if(n->getNodeOut()==p)
                    {
                        n->setNodeOutOil(false);
                        n->setNodeOutWater(true);
                    }
                }

                if(!n->getClosed() && n->getPhaseFlag()==phase::water && n->getWaterTrapped())
                {
                    for(node* nn: accessibleNodes)
                    {
                        if(nn->getPhaseFlag()==phase::water && nn->getClusterWater()==n->getClusterWater())
                        for(auto j : nn->getConnectedPores())
                        {
                            pore* nnn=getPore(j-1);
                            if(!nnn->getClosed() && nnn->getPhaseFlag()==phase::oil)
                            {
                                if(nnn->getNodeIn()==nn)
                                {
                                    nnn->setNodeInOil(false);
                                    nnn->setNodeInWater(true);
                                }

                                if(nnn->getNodeOut()==nn)
                                {
                                    nnn->setNodeOutOil(false);
                                    nnn->setNodeOutWater(true);
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}

}

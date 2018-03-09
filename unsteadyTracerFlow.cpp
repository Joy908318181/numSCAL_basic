/////////////////////////////////////////////////////////////////////////////
/// Name:        unsteadyTracerFlow.cpp
/// Purpose:     methods related to tracer flow simulation
///              in pore/throat networks are defined here.
/// Author:      Ahmed Hamdi Boujelben <ahmed.hamdi.boujelben@gmail.com>
/// Created:     2017
/// Copyright:   (c) 2018 Ahmed Hamdi Boujelben
/// Licence:     Attribution-NonCommercial 4.0 International
/////////////////////////////////////////////////////////////////////////////

#include "network.h"

namespace PNM {

void network::runTracerFlowPT()
{
    cout<<"Starting Tracer Flow Model... "<<endl;

    double startTime,endTime;
    startTime=tools::getCPUTime();

    //post-processing
    if(videoRecording)
        record=true;

    initializeTwoPhaseSimulationPT();

    //initialise flags
    double timeSoFar(0);
    double injectedPVs(0);

    //Solve pressure in Oil
    clusterOilElements();
    if (isOilSpanning)
    {
        assignViscositiesWithMixedFluids();
        assignConductivities();
        for (pore* p :accessiblePores)
        {
            if(p->getPhaseFlag()==phase::water)
               p->setConductivity(1e-200);
            if((p->getPhaseFlag()==phase::oil && !p->getClusterOil()->getSpanning()) || (p->getNodeIn()!=0 && !p->getNodeIn()->getClosed() && p->getNodeIn()->getPhaseFlag()==phase::water) || (p->getNodeOut()!=0 && !p->getNodeOut()->getClosed() && p->getNodeOut()->getPhaseFlag()==phase::water))
               p->setConductivity(1e-200);
        }

        //Link pressure gradient to flow rate (Aker,1998)
        double Q1(0),Q2(0),A,B;

        pressureIn=1;
        pressureOut=0;
        solvePressures();
        updateFlows();
        Q1=getOutletFlow();

        pressureIn=2;
        pressureOut=0;
        solvePressures();
        updateFlows();
        Q2=getOutletFlow();

        B=(Q1-Q2*1/2)/(1-1/2);
        A=(Q1-B);

        deltaP=flowRate/A-B/A;

        pressureIn=deltaP;
        pressureOut=0;
        solvePressures();
        updateFlows();

        //Set timestep
        timeStep=1e50;

        for(pore* p : accessiblePores)
        {
            if(p->getPhaseFlag()==phase::oil && p->getClusterOil()->getSpanning())
            {  
                //Diffusion
                double sumDiffusionSource=0;
                for(element*e : p->getNeighboors())
                {
                    if(!e->getClosed() && e->getPhaseFlag()==phase::oil)
                    {
                        double area=min(e->getVolume()/e->getLength(), p->getVolume()/p->getLength());
                        sumDiffusionSource+=tracerDiffusionCoef/area;
                    }
                }

                //Convection
                if((abs(p->getFlow())/p->getVolume()+sumDiffusionSource)>1e-20)
                {
                    double step=1./(abs(p->getFlow())/p->getVolume()+sumDiffusionSource);
                    if(step<timeStep)
                    {
                        timeStep=step;
                    }
                }

                if(p->getFlow()>1e-24 && p->getConductivity()!=1e-200)
                {
                    node* n=p->getNodeIn();
                    if(n!=0)
                    {
                        n->setFlow((n->getFlow()+abs(p->getFlow())));
                    }
                }
                if(p->getFlow()<-1e-24 && p->getConductivity()!=1e-200)
                {
                    node* n=p->getNodeOut();
                    if(n!=0)
                    {
                        n->setFlow((n->getFlow()+abs(p->getFlow())));
                    }
                }
            }
        }

        for(node* p : accessibleNodes)
        {
            if(p->getPhaseFlag()==phase::oil && p->getClusterOil()->getSpanning())
            {
                //Diffusion
                double sumDiffusionSource=0;
                for(element*e : p->getNeighboors())
                {
                    if(!e->getClosed() && e->getPhaseFlag()==phase::oil)
                    {
                        double area=min(e->getVolume()/e->getLength(), p->getVolume()/p->getLength());
                        sumDiffusionSource+=tracerDiffusionCoef/area;
                    }
                }

                //Convection
                if((abs(p->getFlow())/p->getVolume()+sumDiffusionSource)>1e-20)
                {
                    double step=1./(abs(p->getFlow())/p->getVolume()+sumDiffusionSource);
                    if(step<timeStep)
                    {
                        timeStep=step;
                    }
                }
            }
        }
    }
    else
    {
        cancel=true;
        cout<<"ERROR: Tracer will only flow in the oil. Oil is not spanning in the current configuration."<<endl;
    }

    //define working concentration vector
    vector<double> newConcentration(totalElements);

    while(!cancel && timeSoFar<simulationTime)
    {       
        for(node* n: accessibleNodes)
        {
            if(n->getPhaseFlag()==phase::oil  && n->getClusterOil()->getSpanning())
            {
                //Convection
                double massIn=0;
                for(unsigned j : n->getConnectedPores())
                {
                    pore* p=getPore(j-1);
                    if(!p->getClosed() && p->getPhaseFlag()==phase::oil && p->getConductivity()!=1e-200)
                    {
                        if((p->getNodeIn()==n && p->getFlow()>1e-24) || (p->getNodeOut()==n && p->getFlow()<-1e-24))
                        {
                            massIn+=p->getConcentration()*abs(p->getFlow());
                        }
                    }
                }
                n->setMassFlow(massIn);

                //Diffusion
                double sumDiffusionIn=0;
                double sumDiffusionOut=0;
                for(element*e : n->getNeighboors())
                {
                    if(!e->getClosed() && e->getPhaseFlag()==phase::oil)
                    {
                        double area=min(e->getVolume()/e->getLength(), n->getVolume()/n->getLength());
                        sumDiffusionIn+=e->getConcentration()*tracerDiffusionCoef/area;
                        sumDiffusionOut+=n->getConcentration()*tracerDiffusionCoef/area;
                    }
                }

                //Load new concentration in a temporary vector
                newConcentration[n->getAbsId()]=(n->getConcentration()+(massIn-abs(n->getFlow())*n->getConcentration())*timeStep/n->getVolume()+sumDiffusionIn*timeStep-sumDiffusionOut*timeStep);
            }
        }

        for(pore* p : accessiblePores)
        { 
            if(p->getPhaseFlag()==phase::oil  && p->getClusterOil()->getSpanning())
            {
                double massIn=0;
                double flowIn=0;
                double sumDiffusionIn=0;
                double sumDiffusionOut=0;

                //Convection
                if(p->getInlet())
                {
                    if(abs(p->getFlow())>1e-24 && p->getConductivity()!=1e-200)
                    {
                        massIn=abs(p->getFlow());
                        flowIn=abs(p->getFlow());
                    }
                }
                else
                {
                    if(p->getFlow()>1e-24 && p->getConductivity()!=1e-200 && p->getNodeOut()->getPhaseFlag()==phase::oil)
                    {
                        massIn=p->getNodeOut()->getMassFlow();
                        flowIn=p->getNodeOut()->getFlow();
                    }
                    if(p->getFlow()<1e-24 && p->getConductivity()!=1e-200 && p->getNodeIn()->getPhaseFlag()==phase::oil)
                    {
                        massIn=p->getNodeIn()->getMassFlow();
                        flowIn=p->getNodeIn()->getFlow();
                    }
                }

                if(abs(p->getFlow())<1e-24 || flowIn<1e-24 || p->getConductivity()==1e-200)
                {
                    massIn=0;
                    flowIn=1;
                }

                //Diffusion
                for(element*e : p->getNeighboors())
                {
                    if(!e->getClosed() && e->getPhaseFlag()==phase::oil)
                    {
                        double area=min(e->getVolume()/e->getLength(), p->getVolume()/p->getLength());
                        sumDiffusionIn+=e->getConcentration()*tracerDiffusionCoef/area;
                        sumDiffusionOut+=p->getConcentration()*tracerDiffusionCoef/area;
                    }
                }

                //Load new concentration in a temporary vector
                newConcentration[p->getAbsId()]=(p->getConcentration()+(abs(p->getFlow())/flowIn*massIn-abs(p->getFlow())*p->getConcentration())*timeStep/p->getVolume()+sumDiffusionIn*timeStep-sumDiffusionOut*timeStep);
            }
        }

        //Update concentrations
        for(element* e: accessibleElements)
        {
            if(e->getPhaseFlag()==phase::oil && e->getClusterOil()->getSpanning())
            {
                e->setConcentration(newConcentration[e->getAbsId()]);
                if(e->getConcentration()<-0.00001 || e->getConcentration()>1.0001)
                {
                    cancel=true;
                    cout<<"ERROR: Concentration out of range: "<< e->getConcentration()<<endl;
                }
            }
        }

        if(timeStep!=1e50)
        {
            timeSoFar+=timeStep;
            injectedPVs+=timeStep*flowRate/totalElementsVolume;
        }

        //Display notification
        std::ostringstream ss;
        ss << std::fixed << std::setprecision(2);
        ss << "Injected PVs: " << injectedPVs;
        simulationNotification = ss.str();

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
    cout<<"Tracer Flow Time: "<<endTime-startTime<<" s"<<endl;
}

}

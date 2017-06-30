/////////////////////////////////////////////////////////////////////////////
/// Name:        worker.h
/// Purpose:     thread management functions are declared here.
/// Author:      Ahmed Hamdi Boujelben <ahmed.hamdi.boujelben@gmail.com>
/// Created:     2017
/// Copyright:   (c) 2017 Ahmed Hamdi Boujelben
/// Licence:     Attribution 4.0 International (CC BY 4.0)
/////////////////////////////////////////////////////////////////////////////

#ifndef WORKER_H
#define WORKER_H

#include <QObject>
#include <iostream>
#include "network.h"

class worker : public QObject
{
    Q_OBJECT
public:
    explicit worker(network* net, int j=0, QObject *parent = 0);

    int getJob() const;
    void setJob(int value);

public slots:
    void process();

signals:
    void finished();

private:
    network* n;
    int job;

};

#endif // WORKER_H

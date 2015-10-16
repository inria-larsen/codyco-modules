/*
 * Copyright (C) 2015 Fondazione Istituto Italiano di Tecnologia - Italian Institute of Technology
 * Author: Jorhabib Eljaik
 * email:  jorhabib.eljaik@iit.it
 * Permission is granted to copy, distribute, and/or modify this program
 * under the terms of the GNU General Public License, version 2 or any
 * later version published by the Free Software Foundation.
 *
 * A copy of the license can be found at
 * http://www.robotcub.org/icub/license/gpl.txt
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU General
 * Public License for more details
 */

#ifndef _WHOLEBODYESTIMATORTHREAD_H_
#define _WHOLEBODYESTIMATORTHREAD_H_

#include <bfl/filter/extendedkalmanfilter.h>
#include <bfl/model/linearanalyticsystemmodel_gaussianuncertainty.h>
#include <bfl/model/linearanalyticmeasurementmodel_gaussianuncertainty.h>
#include <bfl/pdf/analyticconditionalgaussian.h>
#include <bfl/pdf/linearanalyticconditionalgaussian.h>

#include <yarp/os/RateThread.h>
#include <yarp/os/Property.h>
#include <yarp/os/Bottle.h>
#include <yarp/os/BufferedPort.h>
#include <yarp/os/ResourceFinder.h>
#include <yarp/os/Time.h>
#include <yarp/sig/Vector.h>

#include "quaternionEKF.h"

#include <iomanip> //setw
#include <algorithm> //std::find for parsing lines
// #include "nonLinearAnalyticConditionalGaussian.h"
// #include "nonLinearMeasurementGaussianPdf.h"
#include <yarp/math/Math.h>

class WholeBodyEstimatorThread: public yarp::os::RateThread
{
private:
    IEstimator* quaternionEKFInstance;
public:
    WholeBodyEstimatorThread (yarp::os::ResourceFinder &rf, int period);
	bool threadInit();
	void run();
	void threadRelease();
};

#endif
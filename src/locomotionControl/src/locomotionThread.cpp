/* 
 * Copyright (C) 2013 CoDyCo
 * Author: Andrea Del Prete
 * email:  andrea.delprete@iit.it
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

#include <locomotion/locomotionThread.h>
#include <wbiy/wbiy.h>
#include <yarp/os/Time.h>
#include <yarp/math/SVD.h>
#include <Eigen/SVD>

using namespace locomotion;
using namespace yarp::math;
using namespace wbiy;

//*************************************************************************************************************************
LocomotionThread::LocomotionThread(string _name, string _robotName, int _period, ParamHelperServer *_ph, wholeBodyInterface *_wbi)
    :  RateThread(_period), name(_name), robotName(_robotName), paramHelper(_ph), robot(_wbi), dxc_comE(0), dxc_footE(0), dqcE(0,0)
{
    status = LOCOMOTION_OFF;
    printCountdown = 0;
}

//*************************************************************************************************************************
bool LocomotionThread::threadInit()
{
    assert(robot->getLinkId("r_foot", LINK_ID_RIGHT_FOOT));
    assert(robot->getLinkId("l_foot", LINK_ID_LEFT_FOOT));
    comLinkId           = iWholeBodyModel::COM_LINK_ID; // use 0 until Silvio implements COM kinematics

    // I must count the nonzero entries of activeJoints to know _n
    assert(paramHelper->linkParam(PARAM_ID_ACTIVE_JOINTS,       activeJoints.data()));
    // I must know the support phase to know the number of constraints
    assert(paramHelper->linkParam(PARAM_ID_SUPPORT_PHASE,       &supportPhase));
    numberOfJointsChanged();
    numberOfConstraintsChanged();
    
    // resize all Yarp vectors
    x_com.resize(DEFAULT_XDES_COM.size(), 0.0);         // measured pos
    x_foot.resize(DEFAULT_XDES_FOOT.size(), 0.0);       // measured pos

    xd_com.resize(DEFAULT_XDES_COM.size(), 0.0);        // desired pos
    xd_foot.resize(DEFAULT_XDES_FOOT.size(), 0.0);      // desired pos
    qd.resize(ICUB_DOFS, 0.0);                          // desired pos (all joints)
    //qdAllJoints.resize(ICUB_DOFS, 0.0);                 // desired pos (all joints)

    xr_com.resize(DEFAULT_XDES_COM.size(), 0.0);        // reference pos
    xr_foot.resize(DEFAULT_XDES_FOOT.size(), 0.0);      // reference pos
    qr.resize(ICUB_DOFS, 0.0);                          // reference pos

    dxr_com.resize(DEFAULT_XDES_COM.size(), 0.0);       // reference vel
    dxr_foot.resize(6, 0.0);                            // reference vel
    dqr.resize(ICUB_DOFS, 0.0);                         // reference vel

    dxc_com.resize(DEFAULT_XDES_COM.size(), 0.0);       // commanded vel
    dxc_foot.resize(6, 0.0);                            // commanded vel

    kp_com.resize(DEFAULT_XDES_COM.size(), 0.0);        // proportional gain
    kp_foot.resize(6, 0.0);                             // proportional gain
    kp_posture.resize(ICUB_DOFS, 0.0);                  // proportional gain
    H_w2b = eye(4,4);

    // map Yarp vectors to Eigen vectors
    new (&dxc_comE)     Map<Vector2d>(dxc_com.data());
    new (&dxc_footE)    Map<Vector6d>(dxc_foot.data());

    // link module rpc parameters to member variables
    assert(paramHelper->linkParam(PARAM_ID_KP_COM,              kp_com.data()));    // constant size
    assert(paramHelper->linkParam(PARAM_ID_KP_FOOT,             kp_foot.data()));
    assert(paramHelper->linkParam(PARAM_ID_KP_POSTURE,          kp_posture.data()));
    assert(paramHelper->linkParam(PARAM_ID_TRAJ_TIME_COM,       &tt_com));
    assert(paramHelper->linkParam(PARAM_ID_TRAJ_TIME_FOOT,      &tt_foot));
    assert(paramHelper->linkParam(PARAM_ID_TRAJ_TIME_POSTURE,   &tt_posture));
    assert(paramHelper->linkParam(PARAM_ID_PINV_DAMP,           &pinvDamp));
    // link module input streaming parameters to member variables
    assert(paramHelper->linkParam(PARAM_ID_XDES_COM,            xd_com.data()));
    assert(paramHelper->linkParam(PARAM_ID_XDES_FOOT,           xd_foot.data()));
    assert(paramHelper->linkParam(PARAM_ID_QDES,                qd.data()));        // constant size
    assert(paramHelper->linkParam(PARAM_ID_H_W2B,               H_w2b.data()));
    // link module output streaming parameters to member variables
    assert(paramHelper->linkParam(PARAM_ID_XREF_COM,            xr_com.data()));
    assert(paramHelper->linkParam(PARAM_ID_XREF_FOOT,           xr_foot.data()));
    assert(paramHelper->linkParam(PARAM_ID_QREF,                qr.data()));        // constant size
    assert(paramHelper->linkParam(PARAM_ID_X_COM,               x_com.data()));
    assert(paramHelper->linkParam(PARAM_ID_X_FOOT,              x_foot.data()));
    assert(paramHelper->linkParam(PARAM_ID_Q,                   q.data()));         // variable size
    
    // Register callbacks for some module parameters
    assert(paramHelper->registerParamCallback(PARAM_ID_TRAJ_TIME_COM,       this));
    assert(paramHelper->registerParamCallback(PARAM_ID_TRAJ_TIME_FOOT,      this));
    assert(paramHelper->registerParamCallback(PARAM_ID_TRAJ_TIME_POSTURE,   this));
    assert(paramHelper->registerParamCallback(PARAM_ID_ACTIVE_JOINTS,       this));
    assert(paramHelper->registerParamCallback(PARAM_ID_SUPPORT_PHASE,       this));

    // Register callbacks for some module commands
    assert(paramHelper->registerCommandCallback(COMMAND_ID_START,           this));
    assert(paramHelper->registerCommandCallback(COMMAND_ID_STOP,            this));

    // read robot status (to be done before initializing trajectory generators)
    if(!readRobotStatus(true))
        return false;

    // create and initialize trajectory generators
    trajGenCom      = new minJerkTrajGen(2,         getRate()*1e-3, DEFAULT_TT_COM);
    trajGenFoot     = new minJerkTrajGen(7,         getRate()*1e-3, DEFAULT_TT_FOOT);
    trajGenPosture  = new minJerkTrajGen(ICUB_DOFS, getRate()*1e-3, DEFAULT_TT_POSTURE);

    printf("\n\n");
    return true;
}

//*************************************************************************************************************************
void LocomotionThread::run()
{
    paramHelper->lock();
    paramHelper->readStreamParams();

    readRobotStatus();              // read encoders, compute positions and Jacobians
    if(status==LOCOMOTION_ON)
    {
        updateReferenceTrajectories();  // compute reference trajectories
    
        dxc_com     = dxr_com   +  kp_com        * (xr_com  - x_com);
        dxc_foot    =/*dxr_foot+*/ kp_foot       * compute6DError(x_foot, xr_foot);  // temporarely remove feedforward velocity because it is 7d (whereas it should be 6d)
        dqc         = S*dqr     +  (S*kp_posture)* (S*qr    - q);

#ifdef PRINT_X_FOOT
    printf("\n*********************************************************************\n");
    printf("x foot:            %s\n", x_foot.toString(2).c_str());
    printf("x ref foot:        %s\n", xr_foot.toString(2).c_str());
    printf("x des foot:        %s\n", xd_foot.toString(2).c_str());
    printf("dx ref foot:       %s\n", dxr_foot.toString(2).c_str());
    printf("dx foot commanded: %s\n", dxc_foot.toString(2).c_str());
#endif
        VectorXd dqMotors = solveTaskHierarchy();   // prioritized velocity control
        robot->setVelRef(dqMotors.data());          // send velocities to the joint motors

        cout<<"dx com            "<< (Jcom_2xN*dq).transpose().format(matrixPrintFormat)<< endl;    // dq in n, J is n+6 !!!!!
        cout<<"dx com commanded  "<< dxc_comE.transpose().format(matrixPrintFormat)<< endl;
    }

    paramHelper->sendStreamParams();
    paramHelper->unlock();

    printCountdown += getRate();
    if(printCountdown>= PRINT_PERIOD)
        printCountdown = 0;
}

//*************************************************************************************************************************
bool LocomotionThread::readRobotStatus(bool blockingRead)
{
    // read joint angles
    bool res = robot->getQ(q.data(), blockingRead);
    res = res && robot->getDq(dqJ.data(), -1.0, blockingRead);

    // select which foot to control
    footLinkId = supportPhase==SUPPORT_LEFT ? LINK_ID_RIGHT_FOOT : LINK_ID_LEFT_FOOT;
    // base orientation conversion
#define COMPUTE_WORLD_2_BASE_ROTOTRANSLATION
#ifdef COMPUTE_WORLD_2_BASE_ROTOTRANSLATION
    Vector7d zero7 = Vector7d::Zero();
    MatrixY H_base_leftFoot(4,4);       // rototranslation from robot base to left foot (i.e. world)
    robot->computeH(q.data(), zero7.data(), LINK_ID_LEFT_FOOT, H_base_leftFoot.data());
    H_w2b = SE3inv(H_base_leftFoot);    // rototranslation from world (i.e. left foot) to robot base
#endif
    //Vector qu = dcm2quaternion(H_w2b.submatrix(0,2,0,2));
    Vector qu = dcm2axis(H_w2b.submatrix(0,2,0,2));     // temporarely use angle/axis notation
    xBase[0]=H_w2b(0,3);    xBase[1]=H_w2b(1,3);    xBase[2]=H_w2b(2,3);
    xBase[3]=qu[0];         xBase[4]=qu[1];         xBase[5]=qu[2];         xBase[6]=qu[3];

    // forward kinematics
    robot->forwardKinematics(q.data(), xBase.data(), footLinkId,    x_foot.data());
    robot->forwardKinematics(q.data(), xBase.data(), comLinkId,     x_com.data());
    // compute Jacobians of both feet and CoM
    robot->computeJacobian(q.data(), xBase.data(), LINK_ID_RIGHT_FOOT,  JfootR.data());
    robot->computeJacobian(q.data(), xBase.data(), LINK_ID_LEFT_FOOT,   JfootL.data());
    robot->computeJacobian(q.data(), xBase.data(), comLinkId,           Jcom_6xN.data());
    // convert Jacobians
    Jcom_2xN = Jcom_6xN.topRows<2>();
    if(supportPhase==SUPPORT_DOUBLE){       Jfoot.setZero();    Jc.topRows<6>()=JfootR; Jc.bottomRows<6>()=JfootL; }
    else if(supportPhase==SUPPORT_LEFT){    Jfoot=JfootR;       Jc = JfootL; }
    else{                                   Jfoot=JfootL;       Jc = JfootR; }
    // estimate base velocity from joint velocities
    MatrixXd Jcb = Jc.leftCols<6>();
    JacobiSVD<MatrixXd> svd(Jcb, ComputeThinU | ComputeThinV);
    dq.head<6>() = svd.solve(Jc.rightCols(_n)*dqJ);
    dq.tail(_n) = dqJ;

    if(printCountdown==0)
    {
        cout<< "R foot vel: "<< (JfootR*dq).norm()<< endl; //.transpose().format(matrixPrintFormat)<< endl;
        cout<< "L foot vel: "<< (JfootL*dq).norm()<< endl; //transpose().format(matrixPrintFormat)<< endl;
        //cout<< "J foot 1:\n" << fixed<< setw(4)<< setprecision(1)<< Jfoot.block(0,0,6,5) <<endl;
        //cout<< "J foot 2:\n" << fixed<< setw(4)<< setprecision(1)<< Jfoot.block(0,5,6,15) <<endl;
    }
    return res;
}

//*************************************************************************************************************************
bool LocomotionThread::updateReferenceTrajectories()
{
    trajGenCom->computeNextValues(xd_com);
    trajGenFoot->computeNextValues(xd_foot);
    trajGenPosture->computeNextValues(qd);
    xr_com      = trajGenCom->getPos();
    xr_foot     = trajGenFoot->getPos();
    qr          = trajGenPosture->getPos();
    dxr_com     = trajGenCom->getVel();
    dxr_foot    = trajGenFoot->getVel();
    dqr         = trajGenPosture->getVel();
    return true;
}

//*************************************************************************************************************************
VectorXd LocomotionThread::solveTaskHierarchy()
{
    // allocate memory
    int k = _k, n = _n;
    MatrixXd Jc_pinv(n+6,k), Jcom_pinv(n+6,2), Jcom_pinvD(n+6,2), Jfoot_pinv(n+6,6), Jfoot_pinvD(n+6,6), Jposture_pinv(n+6,n), N(n+6,n+6);
    VectorXd dqDes(n+6), svJc(k), svJcom(2), svJfoot(6);
    // initialize variables
    dqDes.setZero();
    N.setIdentity();

    // *** CONTACT CONSTRAINTS
    pinvTrunc(Jc, PINV_TOL, Jc_pinv, &svJc);
    N -= Jc_pinv*Jc;

    // *** COM CTRL TASK
    pinvDampTrunc(Jcom_2xN*N, PINV_TOL, pinvDamp, Jcom_pinv, Jcom_pinvD, &svJcom);
    dqDes += Jcom_pinvD*dxc_comE;
#ifndef NDEBUG 
    assertEqual(Jc*N, MatrixXd::Zero(k,n+6), "Jc*Nc=0");
    assertEqual(Jc*dqDes, VectorXd::Zero(k), "Jc*dqCom=0");
#endif
    N -= Jcom_pinv*Jcom_2xN*N;

    // *** FOOT CTRL TASK
    pinvDampTrunc(Jfoot*N, PINV_TOL, pinvDamp, Jfoot_pinv, Jfoot_pinvD, &svJfoot);
    dqDes += Jfoot_pinvD*(dxc_footE - Jfoot*dqDes);
#ifndef NDEBUG 
    assertEqual(Jc*N, MatrixXd::Zero(k,n+6), "Jc*N=0");
    assertEqual(Jcom_2xN*N, MatrixXd::Zero(2,n+6), "Jcom_2xN*Ncom=0");
    assertEqual(Jc*dqDes, VectorXd::Zero(k), "Jc*dqFoot=0");
#endif
    N -= Jfoot_pinv*Jfoot*N;

    // *** POSTURE TASK
    pinvTrunc(Jposture*N, PINV_TOL, Jposture_pinv);
    dqDes += Jposture_pinv*(dqcE - Jposture*dqDes);  //Old implementation (should give same result): dqDes += N*(dqcE - dqDes);

#ifndef NDEBUG
    assertEqual(Jc*N, MatrixXd::Zero(k,n+6), "Jc*N=0");
    assertEqual(Jcom_2xN*N, MatrixXd::Zero(2,n+6), "Jcom_2xN*N=0");
    assertEqual(Jfoot*N, MatrixXd::Zero(6,n+6), "Jfoot*N=0");
    assertEqual(Jc*dqDes, VectorXd::Zero(k), "Jc*dqDes=0");
#endif

    return dqDes.block(0,0,n,1);
}

//*************************************************************************************************************************
void LocomotionThread::preStartOperations()
{
    // no need to lock because the mutex is already locked
    readRobotStatus(true);                  // update com, foot and joint positions
    trajGenCom->init(x_com);                // initialize trajectory generators
    trajGenFoot->init(x_foot);
    trajGenPosture->init(q);
    status = LOCOMOTION_ON;                 // set thread status to "on"
    robot->setControlMode(CTRL_MODE_VEL);   // set position control mode
}

//*************************************************************************************************************************
void LocomotionThread::preStopOperations()
{
    // no need to lock because the mutex is already locked
    VectorXd dqMotors = VectorXd::Zero(_n);
    robot->setVelRef(dqMotors.data());      // stop joint motors
    robot->setControlMode(CTRL_MODE_POS);   // set position control mode
    status = LOCOMOTION_OFF;                // set thread status to "off"
}

//*************************************************************************************************************************
void LocomotionThread::numberOfConstraintsChanged()
{
    _k = supportPhase==SUPPORT_DOUBLE ? 12 : 6;     // current number of constraints 
    Jc.resize(_k, _n+6);
}

//*************************************************************************************************************************
void LocomotionThread::numberOfJointsChanged()
{
    LocalId lid;
    LocalIdList currentActiveJoints = robot->getJointList();
    for(int i=0; i<activeJoints.size(); i++)
    {
        lid = ICUB_MAIN_JOINTS.globalToLocalId(i);
        if(currentActiveJoints.containsId(lid))
        {
            if(activeJoints[i]==0)
                robot->removeJoint(lid);
        }
        else
        {
            if(activeJoints[i]==1)
                robot->addJoint(lid);
        }
    }

    _n = robot->getJointList().size();
    Jcom_6xN.resize(NoChange, _n+6);
    Jcom_2xN.resize(NoChange, _n+6);
    Jfoot.resize(NoChange, _n+6);
    JfootR.resize(NoChange, _n+6);
    JfootL.resize(NoChange, _n+6);
    Jposture = MatrixXd::Identity(_n, _n+6);
    Jc.resize(NoChange, _n+6);

    q.resize(_n, 0.0);                                  // measured pos
    dq.resize(_n+6);                                    // measured vel (base + joints)
    dqJ.resize(_n);                                     // measured vel (joints only)
    dqc.resize(_n, 0.0);                                // commanded vel (Yarp vector)
    new (&dqcE) Map<VectorXd>(dqc.data(), _n);          // commanded vel (Eigen vector)
    kp_posture.resize(_n, 0.0);                         // proportional gain (rpc input parameter)
    // These three have constant size = ICUB_DOFS
    //qd.resize(_n, 0.0);                                 // desired pos (streaming input param)
    //qr.resize(_n, 0.0);                                 // reference pos
    //dqr.resize(_n, 0.0);                                // reference vel
    updateSelectionMatrix();
}

//*************************************************************************************************************************
void LocomotionThread::updateSelectionMatrix()
{
    S.resize(_n, ICUB_DOFS);
    S.zero();
    int j=0;
    for(int i=0; i<ICUB_DOFS; i++)
    {
        if(activeJoints[i] != 0.0)
        {
            S(j,i) = 1.0;
            j++;
        }
    }
}

//*************************************************************************************************************************
void LocomotionThread::threadRelease()
{
    if(trajGenCom)      delete trajGenCom;
    if(trajGenFoot)     delete trajGenFoot;
    if(trajGenPosture)  delete trajGenPosture;
}

//*************************************************************************************************************************
void LocomotionThread::parameterUpdated(const ParamDescription &pd)
{
    switch(pd.id)
    {
    case PARAM_ID_TRAJ_TIME_COM: 
        trajGenCom->setT(tt_com); break;
    case PARAM_ID_TRAJ_TIME_FOOT: 
        trajGenFoot->setT(tt_foot); break;
    case PARAM_ID_TRAJ_TIME_POSTURE: 
        trajGenPosture->setT(tt_posture); break;
    case PARAM_ID_ACTIVE_JOINTS: 
        numberOfJointsChanged(); break;
    case PARAM_ID_SUPPORT_PHASE: 
        numberOfConstraintsChanged(); break;
    default:
        sendMsg("A callback is registered but not managed for the parameter "+pd.name, MSG_WARNING);
    }
}

//*************************************************************************************************************************
void LocomotionThread::commandReceived(const CommandDescription &cd, const Bottle &params, Bottle &reply)
{
    switch(cd.id)
    {
    case COMMAND_ID_START:
        preStartOperations();
        break;
    case COMMAND_ID_STOP:
        preStopOperations();
        sendMsg("Stopping the controller.", MSG_INFO); 
        break;
    default:
        sendMsg("A callback is registered but not managed for the command "+cd.name, MSG_WARNING);
    }
}

//*************************************************************************************************************************
void LocomotionThread::sendMsg(const string &s, MsgType type)
{
    if(type>=MSG_DEBUG)
        printf("[LocomotionThread] %s\n", s.c_str());
}
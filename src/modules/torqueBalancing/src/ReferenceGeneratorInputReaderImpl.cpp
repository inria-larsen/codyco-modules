/**
 * Copyright (C) 2014 CoDyCo
 * @author: Francesco Romano
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

#include "ReferenceGeneratorInputReaderImpl.h"
#include "config.h"
#include <wbi/wholeBodyInterface.h>

namespace codyco {
    namespace torquebalancing {
        
#pragma mark - HandsPositionReader implementation
        EndEffectorPositionReader::EndEffectorPositionReader(wbi::wholeBodyInterface& robot, std::string endEffectorLinkName)
        : m_robot(robot)
        , m_jointsPosition(totalDOFs)
        , m_jointsVelocity(totalDOFs)
        , m_outputSignal(7)
        , m_outputSignalDerivative(7)
        , m_jacobian(7, totalDOFs)
        {
            m_robot.getLinkId(endEffectorLinkName.c_str(), m_endEffectorLinkID);
            initializer();
        }
        
        EndEffectorPositionReader::EndEffectorPositionReader(wbi::wholeBodyInterface& robot, int linkID)
        : m_robot(robot)
        , m_endEffectorLinkID(linkID)
        , m_jointsPosition(totalDOFs)
        , m_jointsVelocity(totalDOFs)
        , m_outputSignal(7)
        , m_outputSignalDerivative(7)
        , m_jacobian(7, totalDOFs)
        {
            initializer();
        }
        
        EndEffectorPositionReader::~EndEffectorPositionReader() {}
        
        void EndEffectorPositionReader::initializer()
        {
            m_robot.getLinkId("l_sole", m_leftFootLinkID);
            m_leftFootToBaseRotationFrame.R = wbi::Rotation(0, 0, 1,
                                                            0, -1, 0,
                                                            1, 0, 0);
        }
        
        void EndEffectorPositionReader::updateStatus()
        {
            m_robot.getEstimates(wbi::ESTIMATE_JOINT_POS, m_jointsPosition.data());
            m_robot.getEstimates(wbi::ESTIMATE_JOINT_VEL, m_jointsVelocity.data());
            
            //update world to base frame
            m_robot.computeH(m_jointsPosition.data(), wbi::Frame(), m_leftFootLinkID, m_world2BaseFrame);
            m_world2BaseFrame = m_world2BaseFrame * m_leftFootToBaseRotationFrame;
            m_world2BaseFrame.setToInverse();

            
            m_robot.forwardKinematics(m_jointsPosition.data(), m_world2BaseFrame, m_endEffectorLinkID, m_outputSignal.data());
            m_robot.computeJacobian(m_jointsPosition.data(), m_world2BaseFrame, m_endEffectorLinkID, m_jacobian.topRows(7).data());
            m_outputSignalDerivative = m_jacobian * m_jointsVelocity;
        }
        
        const Eigen::VectorXd& EndEffectorPositionReader::getSignal()
        {
            updateStatus();
            return m_outputSignal;
        }
        
        const Eigen::VectorXd& EndEffectorPositionReader::getSignalDerivative()
        {
            updateStatus();
            return m_outputSignalDerivative;
        }
        
        int EndEffectorPositionReader::signalSize() const { return 7; }
        
#pragma mark - COMReader implementation
        COMReader::COMReader(wbi::wholeBodyInterface& robot)
        : EndEffectorPositionReader(robot, wbi::wholeBodyInterface::COM_LINK_ID)
        , m_outputSignal(3)
        , m_outputSignalDerivative(3) {}

        COMReader::~COMReader() {}
        
        const Eigen::VectorXd& COMReader::getSignal()
        {
            m_outputSignal = EndEffectorPositionReader::getSignal().head(3);
            return m_outputSignal;
        }
        
        const Eigen::VectorXd& COMReader::getSignalDerivative()
        {
            m_outputSignalDerivative = EndEffectorPositionReader::getSignalDerivative().head(3);
            return m_outputSignalDerivative;
        }
        
        int COMReader::signalSize() const { return 3; }
        
#pragma mark - HandsForceReader implementation
        EndEffectorForceReader::EndEffectorForceReader(wbi::wholeBodyInterface& robot)
        : m_robot(robot)
        , m_jointsPosition(totalDOFs)
        , m_jointsVelocity(totalDOFs)
        , m_outputSignal(6)
        , m_outputSignalDerivative(6) {}
        
        EndEffectorForceReader::~EndEffectorForceReader() {}
        
        void EndEffectorForceReader::updateStatus()
        {
            m_robot.getEstimates(wbi::ESTIMATE_JOINT_POS, m_jointsPosition.data());
            m_robot.getEstimates(wbi::ESTIMATE_JOINT_VEL, m_jointsVelocity.data());
            //TODO: read forces at end-effector
        }
        
        const Eigen::VectorXd& EndEffectorForceReader::getSignal()
        {
            updateStatus();
            return m_outputSignal;
        }
        
        const Eigen::VectorXd& EndEffectorForceReader::getSignalDerivative()
        {
            updateStatus();
            return m_outputSignalDerivative;
        }
        
        int EndEffectorForceReader::signalSize() const { return 6; }
    }
}
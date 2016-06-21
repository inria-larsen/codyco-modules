#ifndef TRANSFORM_H
#define TRANSFORM_H

#include <iDynTree/Core/Transform.h>

namespace kinematics {
    class Transform;
}

namespace iDynTree {
    class Position;
    class Rotation;
    class Transform;
}

class kinematics::Transform {
public:
    enum TransformType {
        ConstraintTypePosition,
        ConstraintTypeRotation,
        ConstraintTypeTransform
    };

private:
    Transform(std::string frameName, TransformType type);

    TransformType m_type;
    iDynTree::Transform m_transform;
    std::string m_frameName;

public:
    static Transform positionConstraint(std::string frameName, const iDynTree::Position &position);
    static Transform rotationConstraint(std::string frameName, const iDynTree::Rotation &rotation);
    static Transform transformConstraint(std::string frameName, const iDynTree::Position &position, const iDynTree::Rotation &rotation);
    static Transform transformConstraint(std::string frameName, const iDynTree::Transform &transform);

    unsigned getSize() const;

    Transform::TransformType getType() const;
    bool hasPositionConstraint() const;
    bool hasRotationConstraint() const;

    const iDynTree::Position& getPosition() const;
    const iDynTree::Rotation& getRotation() const;
    const iDynTree::Transform& getTransform() const;
    const std::string& getFrameName() const;
    
};

#endif /* end of include guard: TRANSFORM_H */

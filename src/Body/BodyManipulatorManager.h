#ifndef CNOID_BODY_BODY_MANIPULATOR_MANAGER_H
#define CNOID_BODY_BODY_MANIPULATOR_MANAGER_H

#include <cnoid/Referenced>
#include <cnoid/EigenTypes>
#include <memory>
#include "exportdecl.h"

namespace cnoid {

class Body;
class Link;
class JointPath;
class JointPathConfigurationHandler;
class ManipulatorFrameSet;
class BodyManipulatorManagerImpl;

class CNOID_EXPORT BodyManipulatorManager : public Referenced
{
public:
    static BodyManipulatorManager* getOrCreateManager(Body* body, Link* base = nullptr, Link* end = nullptr);
    
    ~BodyManipulatorManager();

    Body* body();
    std::shared_ptr<JointPath> jointPath();
    std::shared_ptr<JointPathConfigurationHandler> jointPathConfigurationHandler();

    void setFrameSet(ManipulatorFrameSet* frameSet);
    ManipulatorFrameSet* frameSet();

    int currentConfiguration() const;
    std::string configurationName(int index) const;

protected:
    BodyManipulatorManager();
    BodyManipulatorManager(const BodyManipulatorManager& org) = delete;

private:
    BodyManipulatorManagerImpl* impl;
};

typedef ref_ptr<BodyManipulatorManager> BodyManipulatorManagerPtr;

}

#endif
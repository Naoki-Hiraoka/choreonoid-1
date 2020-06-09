#include "MprProgramItemBase.h"
#include "MprMultiVariableListItem.h"
#include "MprControllerItemBase.h"
#include "MprPositionListView.h"
#include "MprMultiVariableListView.h"
#include <cnoid/Plugin>

using namespace cnoid;

namespace {
  
class ManipulatorPlugin : public Plugin
{
public:
    ManipulatorPlugin() : Plugin("Manipulator")
    {
        require("Body");
    }

    virtual bool initialize()
    {
        MprControllerItemBase::initializeClass(this);
        MprProgramItemBase::initializeClass(this);
        MprMultiVariableListItem::initializeClass(this);

        MprPositionListView::initializeClass(this);
        MprMultiVariableListView::initializeClass(this);
        
        return true;
    }
};

}

CNOID_IMPLEMENT_PLUGIN_ENTRY(ManipulatorPlugin);

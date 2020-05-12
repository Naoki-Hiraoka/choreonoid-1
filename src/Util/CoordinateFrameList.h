#ifndef CNOID_UTIL_COORDINATE_FRAME_LIST_H
#define CNOID_UTIL_COORDINATE_FRAME_LIST_H

#include "CloneableReferenced.h"
#include "CoordinateFrame.h"
#include "Signal.h"
#include <string>
#include "exportdecl.h"

namespace cnoid {

class CNOID_EXPORT CoordinateFrameList : public CloneableReferenced
{
public:
    CoordinateFrameList();
    CoordinateFrameList(const CoordinateFrameList& org);
    ~CoordinateFrameList();

    CoordinateFrameList& operator=(const CoordinateFrameList& rhs);

    enum FrameType { Base, Offset };
    void setFrameType(int type) { frameType_ = type; }
    int frameType() const { return frameType_; }
    bool isForBaseFrames() const { return frameType_ == Base; }
    bool isForOffsetFrames() const { return frameType_ == Offset; }
    
    const std::string& name() const;
    void setName(const std::string& name);

    /**
       This function set the flag to treat the first element as a special element called default frame.
       In general, the default frame corresponds to the origin frame, and it is always kept even if
       the clear function is executed. In addition, the default frame is not stored to the archive,
       and it is forcibly restored by the system.
    */
    void setFirstElementAsDefaultFrame(bool on = true);
    bool hasFirstElementAsDefaultFrame() const { return hasFirstElementAsDefaultFrame_; }
    bool isDefaultFrame(CoordinateFrame* frame) const;
    
    /**
       Keep the default frame if it exists
    */
    void clear();

    int numFrames() const;
    CoordinateFrame* frameAt(int index) const;
    CoordinateFrame* findFrame(const GeneralId& id) const;
    int indexOf(CoordinateFrame* frame) const;

    bool insert(int index, CoordinateFrame* frame);
    bool append(CoordinateFrame* frame);
    void removeAt(int index);

    SignalProxy<void(int index)> sigFrameAdded();
    SignalProxy<void(int index, CoordinateFrame* frame)> sigFrameRemoved();
    SignalProxy<void(int index)> sigFrameAttributeChanged();
    SignalProxy<void(int index)> sigFramePositionChanged();
    void notifyFrameAttributeChange(int index);

    /**
       @return true if the id is successfully changed. false if the id is not
       changed because anther coordinate frame with the same id is exists.
    */
    bool resetId(CoordinateFrame* frame, const GeneralId& newId);

    /**
       Reset the internal id counter used by the createNextId function.
    */
    void resetIdCounter(int id = 0);
    GeneralId createNextId(int prevId = -1);

    bool read(const Mapping& archive);
    void write(Mapping& archive) const;
    void writeHeader(Mapping& archive) const;
    void writeFrames(Mapping& archive) const;

protected:
    virtual Referenced* doClone(CloneMap* cloneMap) const override;
    
private:
    // Called from the CoordinateFrame implementation
    void notifyFramePositionChange(int index);
    
    class Impl;
    Impl* impl;
    int frameType_;
    bool hasFirstElementAsDefaultFrame_;

    friend class CoordinateFrame;
};

typedef ref_ptr<CoordinateFrameList> CoordinateFrameListPtr;

}

#endif

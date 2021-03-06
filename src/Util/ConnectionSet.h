/**
   @author Shin'ichiro Nakaoka
*/

#ifndef CNOID_BASE_CONNECTION_SET_H
#define CNOID_BASE_CONNECTION_SET_H

#include "Signal.h"
#include <vector>
#include "exportdecl.h"

namespace cnoid {
    
class CNOID_EXPORT ConnectionSet
{
public:
    ConnectionSet();
    ConnectionSet(const ConnectionSet& org);
    ConnectionSet(ConnectionSet&& org);
    ConnectionSet& operator=(const ConnectionSet& org);
    ~ConnectionSet();

    bool empty() const {
        return connections.empty();
    }
        
    size_t numConnections() const { return connections.size(); }

    void add(const Connection& connection);
    void add(const ConnectionSet& connections);
    void block();
    void block(int index);
    void unblock();
    void unblock(int index);
    void disconnect();
        
private:
    std::vector<Connection> connections;
};


class ScopedConnectionSet : private ConnectionSet
{
public:
    ScopedConnectionSet() { }
    ScopedConnectionSet(ScopedConnectionSet&& org)
        : ConnectionSet(std::move(org)) { }
    ~ScopedConnectionSet() { ConnectionSet::disconnect(); }

    ScopedConnectionSet& operator=(const ScopedConnection&) = delete;
    
    bool empty() const { return ConnectionSet::empty(); }
    size_t numConnections() const { return ConnectionSet::numConnections(); }
    void add(const Connection& connection) { ConnectionSet::add(connection); }
    void add(const ConnectionSet& connections) { ConnectionSet::add(connections); }
    void block() { ConnectionSet::block(); }
    void block(int index) { ConnectionSet::block(index); }
    void unblock() { ConnectionSet::unblock(); }
    void unblock(int index) { ConnectionSet::unblock(index); }
    void disconnect() { ConnectionSet::disconnect(); }

private:
    ScopedConnectionSet(const ScopedConnection&);
};

}
        
#endif


// -*- LSST-C++ -*-
/*
 * LSST Data Management System
 * Copyright 2014 LSST Corporation.
 *
 * This product includes software developed by the
 * LSST Project (http://www.lsst.org/).
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the LSST License Statement and
 * the GNU General Public License along with this program.  If not,
 * see <http://www.lsstcorp.org/LegalNotices/>.
 */

#ifndef LSST_QSERV_CCONTROL_USERQUERY_H
#define LSST_QSERV_CCONTROL_USERQUERY_H
/**
  * @file
  *
  * @brief Umbrella container for user query state
   *
  * @author Daniel L. Wang, SLAC
  */

// Third-party headers
#include "boost/shared_ptr.hpp"
#include "boost/utility.hpp"

// Local headers
#include "ccontrol/QueryState.h"
#include "qproc/ChunkSpec.h"

// Forward decl
namespace lsst {
namespace qserv {
namespace qdisp {
class Executive;
class MessageStore;
}
namespace qproc {
class QuerySession;
class SecondaryIndex;
}
namespace rproc {
class InfileMerger;
class InfileMergerConfig;
}}}

namespace lsst {
namespace qserv {
namespace ccontrol {

class UserQueryFactory;

/// UserQuery : top-level class for user query data. Not thread-safe, although
/// its delegates are thread-safe as appropriate.
class UserQuery : public boost::noncopyable {
public:
    typedef boost::shared_ptr<UserQuery> Ptr;

    friend class UserQueryFactory;

    // All UserQuery instances are tracked in a single session manager.
    // This facilitates having a handle-oriented Python API (as defined
    // in UserQueryProxy.h)
    static Ptr get(int session);

    // Accessors

    /// @return a non-empty string describing the current error state
    /// Returns an empty string if no errors have been detected.
    std::string getError() const;

    /// @return a description of the current execution state.
    std::string getExecDesc() const;

    /// Add a chunk for later execution
    void addChunk(qproc::ChunkSpec const& cs);

    /// Begin execution of the query over all ChunkSpecs added so far.
    void submit();

    /// Wait until the query has completed execution.
    /// @return the final execution state.
    QueryState join();

    /// Stop a query in progress (for immediate shutdowns)
    void kill();

    /// Release resources related to user query
    void discard();

    // Delegate objects
    boost::shared_ptr<qdisp::Executive> getExecutive() {
        return _executive; }
    boost::shared_ptr<qdisp::MessageStore> getMessageStore() {
        return _messageStore; }

private:
    explicit UserQuery(boost::shared_ptr<qproc::QuerySession> qs);

    void _setupMerger();
    void _discardMerger();
    void _setupChunking();

    // Delegate classes
    boost::shared_ptr<qdisp::Executive> _executive;
    boost::shared_ptr<qdisp::MessageStore> _messageStore;
    boost::shared_ptr<qproc::QuerySession> _qSession;
    boost::shared_ptr<rproc::InfileMergerConfig> _infileMergerConfig;
    boost::shared_ptr<rproc::InfileMerger> _infileMerger;
    boost::shared_ptr<qproc::SecondaryIndex> _secondaryIndex;

    class SessionManager;
    static SessionManager _sessionManager;
    int _sessionId; ///< External reference number

    int _sequence; ///< Sequence number for subtask ids
    std::string _errorExtra; ///< Additional error information

};

}}} // namespace lsst::qserv:ccontrol

#endif // LSST_QSERV_CCONTROL_USERQUERY_H

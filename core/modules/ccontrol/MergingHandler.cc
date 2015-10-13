// -*- LSST-C++ -*-
/*
 * LSST Data Management System
 * Copyright 2014-2015 AURA/LSST.
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

// Class header
#include "ccontrol/MergingHandler.h"

// System headers
#include <cassert>

// LSST headers
#include "lsst/log/Log.h"

// Qserv headers
#include "global/Bug.h"
#include "global/debugUtil.h"
#include "global/MsgReceiver.h"
#include "log/msgCode.h"
#include "proto/ProtoHeaderWrap.h"
#include "proto/ProtoImporter.h"
#include "proto/WorkerResponse.h"
#include "qdisp/JobQuery.h"
#include "rproc/InfileMerger.h"
#include "util/common.h"
#include "util/StringHash.h"

using lsst::qserv::proto::ProtoImporter;
using lsst::qserv::proto::ProtoHeader;
using lsst::qserv::proto::Result;
using lsst::qserv::proto::WorkerResponse;

namespace lsst {
namespace qserv {
namespace ccontrol {
////////////////////////////////////////////////////////////////////////
// MergingRequester public
////////////////////////////////////////////////////////////////////////
MergingHandler::MergingHandler(
    std::shared_ptr<MsgReceiver> msgReceiver,
    std::shared_ptr<rproc::InfileMerger> merger,
    std::string const& tableName)
    : _msgReceiver{msgReceiver}, _infileMerger{merger}, _tableName{tableName},
      _response{new WorkerResponse()} {
    _initState();
}

MergingHandler::~MergingHandler() {
    LOGF_DEBUG("~MergingHandler()");
}

const char* MergingHandler::getStateStr(MsgState const& state) {
    switch(state) {
    case MsgState::INVALID:          return "INVALID";
    case MsgState::HEADER_SIZE_WAIT: return "HEADER_SIZE_WAIT";
    case MsgState::RESULT_WAIT:      return "RESULT_WAIT";
    case MsgState::RESULT_RECV:      return "RESULT_RECV";
    case MsgState::RESULT_EXTRA:     return "RESULT_EXTRA";
    case MsgState::HEADER_ERR:       return "HEADER_ERR";
    case MsgState::RESULT_ERR:       return "RESULT_ERR";
    }
    return "unknown";
}

bool MergingHandler::flush(int bLen, bool& last) {
    LOGF_INFO("From:%4% flush state=%1% blen=%2% last=%3%" %
              getStateStr(_state) % bLen % last % _wName);
    if((bLen < 0) || (bLen != (int)_buffer.size())) {
        if(_state != MsgState::RESULT_EXTRA) {
            LOGF_ERROR("MergingRequester size mismatch: expected %1% got %2%"
                       % _buffer.size() % bLen);
            // Worker sent corrupted data, or there is some other error.
        }
    }
    switch(_state) {
    case MsgState::HEADER_SIZE_WAIT:
        _response->headerSize = static_cast<unsigned char>(_buffer[0]);
        if (!proto::ProtoHeaderWrap::unwrap(_response, _buffer)) {
            std::string s = "From:" + _wName + "Error decoding proto header for " + getStateStr(_state);
            _setError(log::MSG_RESULT_DECODE, s);
            _state = MsgState::HEADER_ERR;
            return false;
        }
        if (_wName == "~") {
            _wName = _response->protoHeader.wname();
        }
        LOGF_DEBUG("HEADER_SIZE_WAIT: From:%1% Resizing buffer to %2%" % _wName % _response->protoHeader.size());
        _buffer.resize(_response->protoHeader.size());
        _state = MsgState::RESULT_WAIT;
        return true;

    case MsgState::RESULT_WAIT:
        if(!_verifyResult()) { return false; }
        if(!_setResult()) { return false; }
        LOGF_INFO("From:%1% _buffer %2%" % _wName % util::prettyCharList(_buffer, 5));
        {
            bool msgContinues = _response->result.continues();
            _buffer.resize(0); // Nothing further needed
            _state = MsgState::RESULT_RECV;
            if(msgContinues) {
                LOGF_INFO("Messages continues, waiting for next header.");
                _state = MsgState::RESULT_EXTRA;
                _buffer.resize(proto::ProtoHeaderWrap::PROTO_HEADER_SIZE);
            } else {
                LOGF_INFO("Messages ends, setting last=true");
                last = true;
            }
            LOGF_INFO("Flushed msgContinues=%1% last=%2% for tableName=%3%" %
                    msgContinues % last % _tableName);

            auto success = _merge();
            if(msgContinues) {
                _response.reset(new WorkerResponse());
            }
            return success;
        }
    case MsgState::RESULT_EXTRA:
        if (!proto::ProtoHeaderWrap::unwrap(_response, _buffer)) {
            _setError(log::MSG_RESULT_DECODE,
                    std::string("Error decoding proto header for ") + getStateStr(_state));
            _state = MsgState::HEADER_ERR;
            return false;
        }
        LOGF_INFO("RESULT_EXTRA: Resizing buffer to %1%" % _response->protoHeader.size());
        _buffer.resize(_response->protoHeader.size());
        _state = MsgState::RESULT_WAIT;
        return true;
    case MsgState::RESULT_RECV:
        // We shouldn't wind up here. _buffer.size(0) and last=true should end communication.
        // fall-through
    case MsgState::HEADER_ERR:
    case MsgState::RESULT_ERR:
         {
            std::ostringstream eos;
            eos << "Unexpected message From:" << _wName << " flush state=" << getStateStr(_state) << " last=" << last;
            LOGF_ERROR("%1%" % eos.str());
            _setError(log::MSG_RESULT_ERROR, eos.str());
         }
        return false;
    default:
        break;
    }
    _setError(log::MSG_RESULT_ERROR, "Unexpected message (invalid)");
    return false;
}

void MergingHandler::errorFlush(std::string const& msg, int code) {
    _setError(code, msg);
    // Might want more info from result service.
    // Do something about the error. FIXME.
    LOGF_ERROR("Error receiving result.");
}

bool MergingHandler::finished() const {
    return _flushed;
}

bool MergingHandler::reset() {
    // If we've pushed any bits to the merger successfully, we have to undo them
    // to reset to a fresh state. For now, we will just fail if we've already
    // begun merging. If we implement the ability to retract a partial result
    // merge, then we can use it and do something better.
    if(_flushed) {
        return false; // Can't reset if we have already pushed state.
    }
    _initState();
    return true;
}

std::ostream& MergingHandler::print(std::ostream& os) const {
    return os << "MergingRequester(" << _tableName << ", flushed="
              << (_flushed ? "true)" : "false)") ;
}
////////////////////////////////////////////////////////////////////////
// MergingRequester private
////////////////////////////////////////////////////////////////////////

void MergingHandler::_initState() {
    _buffer.resize(proto::ProtoHeaderWrap::PROTO_HEADER_SIZE);
    _state = MsgState::HEADER_SIZE_WAIT;
    _setError(0, "");
}

bool MergingHandler::_merge() {
    if (auto job = getJobQuery().lock()) {
        if(job->isCancelled()) {
            LOGF_INFO("MergingRequester::_merge(), but already cancelled");
            return false;
        }
        if(_flushed) {
            throw Bug("MergingRequester::_merge : already flushed");
        }
        bool success = _infileMerger->merge(_response);
        if(!success) {
            rproc::InfileMergerError const& err = _infileMerger->getError();
            _setError(log::MSG_RESULT_ERROR, err.getMsg());
            _state = MsgState::RESULT_ERR;
        }
        _response.reset();
        return success;
    }

    LOGF_ERROR("MergingHandler::_merge() failed, jobQuery was NULL");
    return false;
}

void MergingHandler::_setError(int code, std::string const& msg) {
    LOGF_INFO("setError: code: %1%, message: %2%" % code % msg);
    std::lock_guard<std::mutex> lock(_errorMutex);
    _error = Error(code, msg);
}

bool MergingHandler::_setResult() {
    if(!ProtoImporter<proto::Result>::setMsgFrom(_response->result, &_buffer[0], _buffer.size())) {
        _setError(log::MSG_RESULT_DECODE, "Error decoding result msg");
        _state = MsgState::RESULT_ERR;
        return false;
    }
    return true;
}
bool MergingHandler::_verifyResult() {
    if(_response->protoHeader.md5() != util::StringHash::getMd5(_buffer.data(), _buffer.size())) {
        _setError(log::MSG_RESULT_MD5, "Result message MD5 mismatch");
        _state = MsgState::RESULT_ERR;
        return false;
    }
    return true;
}

}}} // lsst::qserv::ccontrol
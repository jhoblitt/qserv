/* 
 * LSST Data Management System
 * Copyright 2012 LSST Corporation.
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
// QueryTemplate houses the implementation of QueryTemplate, which is
// a object that can be used to generate concrete queries from a
// template, given certain parameters (e.g. chunk/subchunk).
#include "lsst/qserv/master/QueryTemplate.h"
#include <sstream>
#include <iostream>
#include "lsst/qserv/master/sqltoken.h" // sqlShouldSeparate
#include "lsst/qserv/master/ColumnRef.h"
#include "lsst/qserv/master/TableRefN.h"

namespace qMaster=lsst::qserv::master;
using lsst::qserv::master::QueryTemplate;

namespace { // File-scope helpers
struct SpacedOutput {
    SpacedOutput(std::ostream& os_, std::string sep_=" ") 
        : os(os_), sep(sep_), count(0) {}
    void operator()(std::string const& s) {
        if(s.empty()) return;
        if(qMaster::sqlShouldSeparate(last, *(last.end()-1), s[0]))  {
            os << sep;
        }
        os << s;
        last = s;
    }
    void operator()(boost::shared_ptr<QueryTemplate::Entry> e) {
        assert(e.get());
        //if(e->isDynamic()) { os << "(" << count << ")"; }
        (*this)(e->getValue());
        ++count;
    }

    std::ostream& os;
    std::string last;
    std::string sep;
    int count;
};

template <typename C>
std::string outputString(C& c) {
    std::stringstream ss;
    SpacedOutput so(ss, " ");
    std::for_each(c.begin(), c.end(), so);
    return ss.str();
}
struct MappingWrapper {
    MappingWrapper(QueryTemplate::EntryMapping const& em_, QueryTemplate& qt_) 
        : em(em_), qt(qt_) {}
    void operator()(boost::shared_ptr<QueryTemplate::Entry> e) {
            qt.append(em.mapEntry(*e));
        }
    QueryTemplate::EntryMapping const& em;
    QueryTemplate& qt;
};
} // anonymous namespace
////////////////////////////////////////////////////////////////////////
// QueryTemplate::Entry subclasses
////////////////////////////////////////////////////////////////////////
class TableEntry : public qMaster::QueryTemplate::Entry {
public:
    TableEntry(qMaster::TableRefN const& tr) 
        : db(tr.getDb()), table(tr.getTable()) {
    }
    virtual std::string getValue() const { 
        std::stringstream ss; 
        if(!db.empty()) { ss << db << "."; }
        ss << table;
        return ss.str();
    }
    virtual bool isDynamic() const { return true; }

    std::string db;
    std::string table;
};
class ColumnEntry : public QueryTemplate::Entry {
public:
    ColumnEntry(qMaster::ColumnRef const& cr) 
        : db(cr.db), table(cr.table), column(cr.column) {        
    }
    virtual std::string getValue() const { 
        std::stringstream ss; 
        if(!db.empty()) { ss << db << "."; }
        if(!table.empty()) { ss << table << "."; }
        ss << column;
        return ss.str();
    }
    virtual bool isDynamic() const { return true; }

    std::string db;
    std::string table;
    std::string column;
};
class StringEntry : public QueryTemplate::Entry {
public:
    StringEntry(std::string const& s_) : s(s_) {}
    virtual std::string getValue() const { return s; } 
    std::string s;
};
struct EntryMerger {
    EntryMerger() {}

    void operator()(boost::shared_ptr<QueryTemplate::Entry> e) {
        if(!_candidates.empty()) {
            if(!_checkMergeable(_candidates.back(), e)) {
                _mergeCurrent();
            }
        }
        _candidates.push_back(e);
    }
    void pack() { _mergeCurrent(); }
    bool _checkMergeable(boost::shared_ptr<QueryTemplate::Entry> left,
                         boost::shared_ptr<QueryTemplate::Entry> right) {
        return !((left->isDynamic() || right->isDynamic()));
    }
    void _mergeCurrent() {
        if(_candidates.size() > 1) {
            boost::shared_ptr<QueryTemplate::Entry> e;
            e.reset(new StringEntry(outputString(_candidates)));
            _entries.push_back(e);
            _candidates.clear();
        } else if(!_candidates.empty()) {
            // Only one entry.
            _entries.push_back(_candidates.back());
            _candidates.pop_back();
        }
    }
    std::list<boost::shared_ptr<QueryTemplate::Entry> > _candidates;
    std::list<boost::shared_ptr<QueryTemplate::Entry> > _entries;
};
// Wraps up the EntryMerger, so that for_each uses persistent state.
struct EntryMergerWrapper {
    EntryMergerWrapper(EntryMerger& em_) : em(em_) {}

    void operator()(boost::shared_ptr<QueryTemplate::Entry> e) {
        em(e);
    }
    EntryMerger& em;
};

////////////////////////////////////////////////////////////////////////
// QueryTemplate
////////////////////////////////////////////////////////////////////////
std::string qMaster::QueryTemplate::dbgStr() const {
    _optimize(); // FIXME: needs to be moved to the right place
    return outputString(_entries);
}
void qMaster::QueryTemplate::append(std::string const& s) {
    boost::shared_ptr<Entry> e(new StringEntry(s));
    _entries.push_back(e);
}
void qMaster::QueryTemplate::append(qMaster::ColumnRef const& cr) {
    boost::shared_ptr<Entry> e(new ColumnEntry(cr));
    _entries.push_back(e);
}

void qMaster::QueryTemplate::append(qMaster::TableRefN const& tr) {
    boost::shared_ptr<Entry> e(new TableEntry(tr));
    _entries.push_back(e);
}
void qMaster::QueryTemplate::append(boost::shared_ptr<QueryTemplate::Entry> e) {
    _entries.push_back(e);
}
std::string qMaster::QueryTemplate::generate(EntryMapping const& em) const {
    QueryTemplate newQt;
    std::for_each(_entries.begin(), _entries.end(), MappingWrapper(em, newQt));
    return outputString(newQt._entries);
}
void qMaster::QueryTemplate::clear() {
    _entries.clear();
}

////////////////////////////////////////////////////////////////////////
// QueryTemplate (private)
////////////////////////////////////////////////////////////////////////
void qMaster::QueryTemplate::_optimize() const {
    EntryMerger em;
    EntryMergerWrapper emw(em);
    std::for_each(_entries.begin(), _entries.end(), emw);
    em.pack();
#if 0 // Debugging.
    std::cout << "merged " << _entries.size() << " entries to "
              << em._entries.size() << std::endl;
    std::cout << "was: " << outputString(_elements) << std::endl;
    std::cout << "now: " << outputString(em._entries) << std::endl;
#endif
}

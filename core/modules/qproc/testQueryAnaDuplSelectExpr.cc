// -*- LSST-C++ -*-
/*
 * LSST Data Management System
 * Copyright 2009-2015 AURA/LSST.
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

 /**
  * @file
  *
  * @brief Test C++ parsing and query analysis logic for select expressions
  *
  *
  * @author Fabrice Jammes, IN2P3/SLAC
  */

// System headers
#include <algorithm>
#include <iostream>
#include <iterator>
#include <map>
#include <sstream>
#include <string>

// Third-party headers
#include "boost/algorithm/string.hpp"
#include "boost/format.hpp"

// Boost unit test header
#define BOOST_TEST_MODULE QueryAnaDuplicateSelectExpr
#include "boost/test/included/unit_test.hpp"

// LSST headers
#include "lsst/log/Log.h"

// Qserv headers
#include "css/Facade.h"
#include "global/stringTypes.h"
#include "parser/ParseException.h"
#include "parser/parseExceptions.h"
#include "parser/SelectParser.h"
#include "qana/AnalysisError.h"
#include "qana/DuplSelectExprPlugin.h"
#include "qdisp/ChunkMeta.h"
#include "qproc/QuerySession.h"
#include "query/Constraint.h"
#include "query/QsRestrictor.h"
#include "query/QueryContext.h"
#include "query/SelectList.h"
#include "query/SelectStmt.h"
#include "query/typedefs.h"
#include "testQueryAna.h"
#include "util/Error.h"
#include "util/MultiError.h"


using lsst::qserv::parser::SelectParser;
using lsst::qserv::parser::UnknownAntlrError;
using lsst::qserv::qana::DuplSelectExprPlugin;
using lsst::qserv::qdisp::ChunkMeta;
using lsst::qserv::qproc::ChunkQuerySpec;
using lsst::qserv::qproc::ChunkSpec;
using lsst::qserv::qproc::QuerySession;
using lsst::qserv::query::Constraint;
using lsst::qserv::query::ConstraintVec;
using lsst::qserv::query::ConstraintVector;
using lsst::qserv::query::QsRestrictor;
using lsst::qserv::query::QueryContext;
using lsst::qserv::query::SelectList;
using lsst::qserv::query::SelectStmt;
using lsst::qserv::query::ValueExprPtrVector;
using lsst::qserv::util::Error;
using lsst::qserv::util::MultiError;
using lsst::qserv::StringPair;
using lsst::qserv::StringVector;

/**
 * Reproduce exception message caused by a duplicated select field
 *
 * @param n     number of occurences found
 * @param name  name of the duplicated field
 * @param pos   position of the occurences found
 */
std::string build_exception_msg(std::string n, std::string name, std::string pos) {
    MultiError multiError;
    boost::format dupl_err_msg = boost::format(DuplSelectExprPlugin::ERR_MSG) %
                                               name % pos;

    Error error(Error::DUPLICATE_SELECT_EXPR, dupl_err_msg.str());
    multiError.push_back(error);
    std::string err_msg = "AnalysisError:" + DuplSelectExprPlugin::EXCEPTION_MSG +
                          multiError.toOneLineString();
    return err_msg;
}



////////////////////////////////////////////////////////////////////////
// CppParser basic tests
////////////////////////////////////////////////////////////////////////
BOOST_FIXTURE_TEST_SUITE(DuplSelectExpr, ParserFixture)

BOOST_AUTO_TEST_CASE(Alias) {
    std::string sql = "select chunkId as f1, pm_declErr AS f1 from LSST.Object where bMagF > 20.0 GROUP BY chunkId;";

    std::string expected_err_msg = build_exception_msg("2", "f1", " 1 2");

    std::shared_ptr<QuerySession> qs = buildQuerySession(qsTest, sql, expected_err_msg);
    std::shared_ptr<QueryContext> context = qs->dbgGetContext();
    BOOST_CHECK(context);
}

BOOST_AUTO_TEST_CASE(CaseInsensitive) {
    std::string sql = "select chunkId, CHUNKID from LSST.Object where bMagF > 20.0 GROUP BY chunkId;";

    std::string expected_err_msg = build_exception_msg("2", "chunkid", " 1 2");

    std::shared_ptr<QuerySession> qs = buildQuerySession(qsTest, sql, expected_err_msg);
    std::shared_ptr<QueryContext> context = qs->dbgGetContext();
    BOOST_CHECK(context);
}

BOOST_AUTO_TEST_CASE(Function) {
    std::string sql = "select sum(pm_declErr), chunkId as f1, chunkId AS f1, avg(pm_declErr) from LSST.Object where bMagF > 20.0 GROUP BY chunkId;";

    std::string expected_err_msg = build_exception_msg("2", "f1", " 2 3");

    std::shared_ptr<QuerySession> qs = buildQuerySession(qsTest, sql, expected_err_msg);
    std::shared_ptr<QueryContext> context = qs->dbgGetContext();
    BOOST_CHECK(context);
}

BOOST_AUTO_TEST_CASE(Simple) {
    std::string sql = "select pm_declErr, chunkId, ra_Test from LSST.Object where bMagF > 20.0 GROUP BY chunkId;";

     std::shared_ptr<QuerySession> qs = buildQuerySession(qsTest, sql);
     std::shared_ptr<QueryContext> context = qs->dbgGetContext();
     BOOST_CHECK(context);
}

BOOST_AUTO_TEST_CASE(SameNameDifferentTable) {
    std::string sql = "SELECT o1.objectId, o2.objectId, scisql_angSep(o1.ra_PS, o1.decl_PS, o2.ra_PS, o2.decl_PS) AS distance "
            "FROM Object o1, Object o2 "
            "WHERE scisql_angSep(o1.ra_PS, o1.decl_PS, o2.ra_PS, o2.decl_PS) < 0.05 "
            "AND  o1.objectId <> o2.objectId;";

    std::string expected_err_msg = build_exception_msg("2", "objectid", " 1 2");

     std::shared_ptr<QuerySession> qs = buildQuerySession(qsTest, sql, expected_err_msg);
     std::shared_ptr<QueryContext> context = qs->dbgGetContext();
     BOOST_CHECK(context);
}

BOOST_AUTO_TEST_SUITE_END()

#include <sys/time.h> 
#include <sstream>
#include <iostream>
#include <boost/format.hpp>
#include "lsst/qserv/master/TableMerger.h"
#include "lsst/qserv/master/sql.h"
using lsst::qserv::master::SqlConfig;
using lsst::qserv::master::SqlConnection;
using lsst::qserv::master::TableMerger;
using lsst::qserv::master::TableMergerError;
using lsst::qserv::master::TableMergerConfig;

namespace { // File-scope helpers

std::string getTimeStampId() {
    struct timeval now;
    int rc = gettimeofday(&now, NULL);
    assert(rc == 0);
    std::stringstream s;
    s << (now.tv_sec % 10000) << now.tv_usec;
    return s.str();
    // Use the lower digits as pseudo-unique (usec, sec % 10000)
    // FIXME: is there a better idea?
}

boost::shared_ptr<SqlConfig> makeSqlConfig(TableMergerConfig const& c) {
    boost::shared_ptr<SqlConfig> sc(new SqlConfig());
    assert(sc.get());
    sc->username = c.user;
    sc->dbName = c.targetDb;
    sc->socket = c.socket;
    return sc;
}
    
} // anonymous namespace

std::string const TableMerger::_dropSql("DROP TABLE IF EXISTS %s;");
std::string const TableMerger::_createSql("CREATE TABLE IF NOT EXISTS %s SELECT * FROM %s;");
std::string const TableMerger::_createFixSql("CREATE TABLE IF NOT EXISTS %s SELECT %s FROM %s %s;");
std::string const TableMerger::_insertSql("INSERT INTO %s SELECT * FROM %s;");
std::string const TableMerger::_cleanupSql("DROP TABLE IF EXISTS %s;");
std::string const TableMerger::_cmdBase("%1% --socket=%2% -u %3% %4%");


////////////////////////////////////////////////////////////////////////
// public
////////////////////////////////////////////////////////////////////////
TableMerger::TableMerger(TableMergerConfig const& c) 
    : _config(c),
      _sqlConfig(makeSqlConfig(c)),
      _tableCount(0) {
    _fixupTargetName();
    _loadCmd = (boost::format(_cmdBase)
		% c.mySqlCmd % c.socket % c.user % c.targetDb).str();    
}

bool TableMerger::merge(std::string const& dumpFile, 
			std::string const& tableName) {
    bool isOk = true;
    std::string sql;
    _importResult(dumpFile); 
    {
	boost::lock_guard<boost::mutex> g(_countMutex);
	++_tableCount;
	if(_tableCount == 1) {
	    sql = _buildMergeSql(tableName, true);
            isOk = _applySql(sql);
            if(!isOk) {
                --_tableCount; // We failed merging the table.
            }
	    return isOk; // must happen first.
	}
    }
    // No locking needed if not first, after updating the counter.
    sql = _buildMergeSql(tableName, false); 
    return _applySql(sql);
}

bool TableMerger::finalize() {
    if(_mergeTable != _config.targetTable) {
	std::string cleanup = (boost::format(_cleanupSql) % _mergeTable).str();

	// Need to perform fixup for aggregation.
	std::string sql = (boost::format(_createFixSql) 
			   % _config.targetTable 
			   % _config.fixupSelect
			   % _mergeTable 
			   % _config.fixupPost).str() + cleanup;
	return _applySql(sql);
    }
    return true;
}
////////////////////////////////////////////////////////////////////////
// private
////////////////////////////////////////////////////////////////////////
bool TableMerger::_applySql(std::string const& sql) {

    FILE* fp;
    {
	boost::lock_guard<boost::mutex> m(_popenMutex);
	fp = popen(_loadCmd.c_str(), "w"); // check error
    }
    if(fp == NULL) {
	_error.status = TableMergerError::MYSQLOPEN;
	_error.errorCode = 0;
	_error.description = "Error starting mysql process.";
	return false;
    }
    int written = fwrite(sql.c_str(), sql.size(), 
			 sizeof(std::string::value_type), fp);
    if(written != (sql.size()*sizeof(std::string::value_type))) {
	_error.status = TableMergerError::MERGEWRITE;
	_error.errorCode = written;
	_error.description = "Error writing sql to mysql process..";
	{
	    boost::lock_guard<boost::mutex> m(_popenMutex);
	    pclose(fp); // cleanup
	}
	return false;
    }
    int r;
    {
	boost::lock_guard<boost::mutex> m(_popenMutex);
	r = pclose(fp);
    }
    if(r == -1) {
	_error.status = TableMergerError::TERMINATE;
	_error.errorCode = r;
	_error.description = "Error finalizing merge step..";
	return false;
    }
    return true;
}

bool TableMerger::_applySqlLocal(std::string const& sql) {
    SqlConnection sc(*_sqlConfig);
    if(!sc.connectToDb()) {
	std::stringstream ss;
	_error.status = TableMergerError::MYSQLCONNECT;
	_error.errorCode = sc.getMySqlErrno();
	ss << "Code:" << _error.errorCode << " "
	   << sc.getMySqlError();
	_error.description = "Error connecting to db." + ss.str();
	return false;
    }
    if(!sc.apply(sql)) {
	std::stringstream ss;
	_error.status = TableMergerError::MYSQLEXEC;
	_error.errorCode = sc.getMySqlErrno();
	ss << "Code:" << _error.errorCode << " "
	   << sc.getMySqlError();
	_error.description = "Error applying sql." + ss.str();
	return false;
    }

    return true;
}

std::string TableMerger::_buildMergeSql(std::string const& tableName, 
					bool create) {
    std::string cleanup = (boost::format(_cleanupSql) % tableName).str();
    
    if(create) {
	return (boost::format(_dropSql) % _mergeTable).str() 
	    + (boost::format(_createSql) % _mergeTable 
	       % tableName).str() + cleanup;
    } else {
	return (boost::format(_insertSql) %  _mergeTable 
		% tableName).str() + cleanup;
    }
}

void TableMerger::_fixupTargetName() {
    if(_config.targetTable.empty()) {
	assert(!_config.targetDb.empty());
	_config.targetTable = (boost::format("%1%.result_%2%") 
			       % _config.targetDb % getTimeStampId()).str();
    }
    if(!_config.fixupSelect.empty()) {
	// Set merging temporary if needed.
	_mergeTable = _config.targetTable + "_m"; 
    } else {
	_mergeTable = _config.targetTable;
    }
}

bool TableMerger::_importResult(std::string const& dumpFile) {
    int rc = system((_loadCmd + "<" + dumpFile).c_str());
    if(rc != 0) {
	_error.status = TableMergerError::IMPORT;
	_error.errorCode = rc;
	_error.description = "Error importing result db.";
	return false;	
    }
    return true;
}


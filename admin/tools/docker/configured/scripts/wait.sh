#!/bin/sh

# LSST Data Management System
# Copyright 2014-2015 LSST Corporation.
#
# This product includes software developed by the
# LSST Project (http://www.lsst.org/).
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the LSST License Statement and
# the GNU General Public License along with this program.  If not,
# see <http://www.lsstcorp.org/LegalNotices/>.

# Docker utility:
# return when all Qserv services are up and running

# @author  Fabrice JAMMES, IN2P3

set -e

QSERV_RUN_DIR=/qserv/run

# Wait for Qserv services to be up and running
while ! "$QSERV_RUN_DIR"/bin/qserv-status.sh > /dev/null
do
    echo "Wait for Qserv to start on $(hostname)"
    sleep 2
done

echo "Qserv is up on $(hostname)"

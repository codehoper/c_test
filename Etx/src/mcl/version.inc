#
# Copyright (c) Microsoft Corporation
#
# This file is part of the Microsoft Research Mesh Connectivity Layer.
# You should have received a copy of the Microsoft Research Shared Source
# license agreement (MSR-SSLA) for this software; see the file "license.txt".
# If not, please see http://research.microsoft.com/mesh/license.htm,
# or write to Microsoft Research, One Microsoft Way, Redmond, WA 98052-6399.
#
#
# version.inc
# 
# Converts version-related environment variables to C_DEFINES.
# If environment vars are not set, verion defaults to 1.0.0.0.
#
# To automatically set the buildnum to a YMMDD jdate, add the following
# line to your %INIT%\setenv.cmd or other environment script:
#
#	set BUILDNUM=%DATE:~13,1%%DATE:~4,2%%DATE:~7,2%
#

!IF "$(BUILDMAJORVER)"==""
BUILDMAJORVER=1
!ENDIF
!IF "$(BUILDMINORVER)"==""
BUILDMINORVER=0
!ENDIF
!IF "$(BUILDNUM)"==""
BUILDNUM=0
!ENDIF
!IF "$(BUILDREV)"==""
BUILDREV=0
!ENDIF

C_DEFINES=$(C_DEFINES) -DBUILDMAJORVER=$(BUILDMAJORVER) -DBUILDMINORVER=$(BUILDMINORVER) -DBUILDNUM=$(BUILDNUM) -DBUILDREV=$(BUILDREV) -DBUILDSTR="""$(BUILDMAJORVER).$(BUILDMINORVER).$(BUILDNUM).$(BUILDREV)"""

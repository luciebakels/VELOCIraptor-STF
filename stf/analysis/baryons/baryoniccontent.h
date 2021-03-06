/*! \file baryoniccontent.h
 *  \brief header file for the code
 */

#ifndef BARYONS_H
#define BARYONS_H

#include "allvars.h"
#include "proto.h"

#define BARYONICCONTENT 0.1

#endif

/*!
\mainpage  Reference documentation for BaryonicContent
\author Pascal J. Elahi \n
\version 0.1
\section license License
This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 2 of
the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful, but
WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
General Public License for more details at
http://www.gnu.org/copyleft/gpl.html

\section Description Description
This program is designed to calculate various baryonic properties associated with dark matter
structures identified by STF/VElOCIraptor. NOTE: that at the moment, it is coded such that it assumes
baryons are always associated with a dark matter structure if the ibaryon flag in the options structure
is set to 1. That is, the files are split in two structure files and associated baryon files. However,
if the output is not split into separate files, then the structures can be composed of ANY type of particle


*/

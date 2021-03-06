#ifndef SRC_LIB_MONGOBACKEND_MONGOREGISTRATIONAUX_H_
#define SRC_LIB_MONGOBACKEND_MONGOREGISTRATIONAUX_H_

/*
*
* Copyright 2019 Telefonica Investigacion y Desarrollo, S.A.U
*
* This file is part of Orion Context Broker.
*
* Orion Context Broker is free software: you can redistribute it and/or
* modify it under the terms of the GNU Affero General Public License as
* published by the Free Software Foundation, either version 3 of the
* License, or (at your option) any later version.
*
* Orion Context Broker is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero
* General Public License for more details.
*
* You should have received a copy of the GNU Affero General Public License
* along with Orion Context Broker. If not, see http://www.gnu.org/licenses/.
*
* For those usages not covered by this license please contact with
* iot_support at tid dot es
*
* Author: Fermin Galan, Ken Zangelin
*/
#include "mongo/client/dbclient.h"                         // mongo::BSONObj

#include "apiTypesV2/Registration.h"                       // ngsiv2::Registration



/* ****************************************************************************
*
* mongo set functions
*/
extern void mongoSetRegistrationId(ngsiv2::Registration* regP, const mongo::BSONObj* rP);
extern void mongoSetDescription(ngsiv2::Registration* regP,    const mongo::BSONObj* rP);
extern void mongoSetProvider(ngsiv2::Registration* regP,       const mongo::BSONObj* rP);
extern void mongoSetEntities(ngsiv2::Registration* regP,       const mongo::BSONObj& cr0);
extern void mongoSetAttributes(ngsiv2::Registration* regP,     const mongo::BSONObj& cr0);
extern void mongoSetExpires(ngsiv2::Registration* regP,        const mongo::BSONObj& r);
extern void mongoSetStatus(ngsiv2::Registration* regP,         const mongo::BSONObj* rP);
extern bool mongoSetDataProvided(ngsiv2::Registration* regP,   const mongo::BSONObj* rP, bool arrayAllowed);

#endif  // SRC_LIB_MONGOBACKEND_MONGOREGISTRATIONAUX_H_

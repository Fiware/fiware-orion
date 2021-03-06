/*
*
* Copyright 2019 FIWARE Foundation e.V.
*
* This file is part of Orion-LD Context Broker.
*
* Orion-LD Context Broker is free software: you can redistribute it and/or
* modify it under the terms of the GNU Affero General Public License as
* published by the Free Software Foundation, either version 3 of the
* License, or (at your option) any later version.
*
* Orion-LD Context Broker is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero
* General Public License for more details.
*
* You should have received a copy of the GNU Affero General Public License
* along with Orion-LD Context Broker. If not, see http://www.gnu.org/licenses/.
*
* For those usages not covered by this license please contact with
* orionld at fiware dot org
*
* Author: Ken Zangelin
*/
#include "mongo/client/dbclient.h"                             // mongo legacy driver

#include "logMsg/logMsg.h"                                     // LM_*
#include "logMsg/traceLevels.h"                                // Lmt*

#include "orionld/mongoCppLegacy/mongoCppLegacyDbObjectFieldGet.h"   // Own interface



// -----------------------------------------------------------------------------
//
// mongoCppLegacyDbObjectFieldGet -
//
bool mongoCppLegacyDbObjectFieldGet(const mongo::BSONObj* boP, const char* fieldName, mongo::BSONObj* objectP)
{
  bool present = boP->hasField(fieldName);

  if (present == false)
  {
    LM_E(("Runtime Error (field '%s' is missing in BSONObj '%s'", fieldName, boP->toString().c_str()));
    return false;
  }

  mongo::BSONType type = boP->getField(fieldName).type();

  if (type == mongo::Object)
  {
    *objectP = boP->getObjectField(fieldName);
    return true;
  }

  LM_E(("Runtime Error (field '%s' was supposed to be a string but type=%d", fieldName, type));
  return false;
}

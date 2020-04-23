/*
*
* Copyright 2018 FIWARE Foundation e.V.
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
* Author: Ken Zangelin and Gabriel Quaresma
*/
#include <string>
#include <vector>

#include "logMsg/logMsg.h"                                       // LM_*
#include "logMsg/traceLevels.h"                                  // Lmt*

#include "rest/ConnectionInfo.h"                                 // ConnectionInfo

#include "orionld/common/orionldState.h"                         // orionldState
#include "orionld/common/orionldErrorResponse.h"                 // orionldErrorResponseCreate
#include "orionld/common/urlCheck.h"                             // urlCheck
#include "orionld/common/urnCheck.h"                             // urnCheck
#include "orionld/db/dbConfiguration.h"                          // dbEntityDelete, dbEntityLookup
#include "orionld/serviceRoutines/orionldDeleteEntity.h"         // Own Interface



// ----------------------------------------------------------------------------
//
// orionldDeleteEntity -
//
bool orionldDeleteEntity(ConnectionInfo* ciP)
{
  LM_T(LmtServiceRoutine, ("In orionldDeleteEntity"));

  // Check that the Entity ID is a valid URI
  char* details;

  if ((urlCheck(orionldState.wildcard[0], &details) == false) && (urnCheck(orionldState.wildcard[0], &details) == false))
  {
    orionldErrorResponseCreate(OrionldBadRequestData, "Invalid Entity ID", details);
    orionldState.httpStatusCode = SccBadRequest;
    return false;
  }

  char* entityId = orionldState.wildcard[0];

  LM_W(("orionldDeleteEntity: Entity ID -> %s", entityId));

  if (dbEntityLookup(entityId) == NULL)
  {
    orionldErrorResponseCreate(OrionldResourceNotFound, "The requested entity has not been found. Check type and id", entityId);
    // HTTP Response Code is 404 - Resource not found
    orionldState.httpStatusCode = SccNotFound;
    return false;
  }

  if (dbEntityDelete(entityId) == false)
  {
    LM_E(("dbEntityDelete returned false"));
    // HTTP Response Code is 400 - Bad request
    orionldState.httpStatusCode = SccBadRequest;
    return false;
  }

  // HTTP Response Code is 204 - No Content
  orionldState.httpStatusCode = SccNoContent;

  return true;
}
